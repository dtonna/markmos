// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_pool.hpp"

#include <atomic>
#include <cassert>

#include "rpmalloc.h"

GlobalPoolAllocator      g_pool_allocator;

static std::atomic<bool> g_pool_initialized{false};

// ----------------------------------------------------------------------------
// PoolInit
// ----------------------------------------------------------------------------

void                     PoolInit() noexcept {

    bool expected = false;

    if (!g_pool_initialized.compare_exchange_strong(expected, true)) {
        assert(false && "PoolInit called twice");
        return;
    }

    // rpmalloc v2.0.2 requires a memory interface pointer (NULL for default).
    rpmalloc_initialize(nullptr);

    g_pool_allocator.init(

        // aligned alloc
        [](size_t size, size_t alignment) -> void                     *{ return rpaligned_alloc(alignment, size); },

        // free
        [](void *ptr, size_t) noexcept { rpfree(ptr); });
}

// ----------------------------------------------------------------------------
// PoolShutdown
// ----------------------------------------------------------------------------

void PoolShutdown() noexcept {

    if (!g_pool_initialized.load()) {
        return;
    }

    g_pool_allocator.shutdown();

    rpmalloc_finalize();

    g_pool_initialized.store(false);
}

// ----------------------------------------------------------------------------
// pool_alloc
// ----------------------------------------------------------------------------

void *pool_alloc(size_t size) noexcept {

    assert(g_pool_initialized.load() && "Pool allocator not initialized");

    if (size == 0) {
        size = 1;
    }

    if (size <= 4096) {

        if (void *ptr = g_pool_allocator.alloc(size)) {
            return ptr;
        }
    }

    // fallback large alloc
    return rpmalloc(size);
}

// ----------------------------------------------------------------------------
// Per-thread allocator setup
// ----------------------------------------------------------------------------
//
// rpmalloc gives EVERY thread its own heap, and a thread that never calls
// rpmalloc_thread_initialize() has none. pool_alloc() falls through to
// rpmalloc() for anything over 4096 bytes, and that dereferences the thread's
// null heap pointer — a segfault at a small offset inside address 0, on the
// thread that happens to do it, some time after boot.
//
// This is not hypothetical and it is not new: it is what a released rpmalloc
// does, and it is why every thread entry in this repo must open with
// pool_thread_initialize() and close with pool_thread_finalize().
//
// It stayed invisible for a long time because the vendored rpmalloc was an
// unreleased master state that did not behave this way, so pinning a real
// release (1.4.2) is what exposed it. The save worker was the one that
// faulted, from inside Vfs::read_file on the solved-games write.

void pool_thread_initialize() noexcept {
    // Asserted rather than assumed: without PoolInit() the rpmalloc globals do
    // not exist, and thread_initialize on them is the same fault one frame later,
    // with no stack that points here. Three freecell tests hit exactly that the
    // first time a worker thread grew a heap.
    assert(g_pool_initialized.load() && "pool_thread_initialize without PoolInit()");
    rpmalloc_thread_initialize();
}

void pool_thread_finalize() noexcept { rpmalloc_thread_finalize(); }

// ----------------------------------------------------------------------------
// pool_free
// ----------------------------------------------------------------------------

void pool_free(void *ptr) noexcept {

    if (!ptr) {
        return;
    }

    // slow path: O(blocks)
    // prefer pool_free_sized()

    if (!g_pool_allocator.free(ptr)) {
        rpfree(ptr);
    }
}

// ----------------------------------------------------------------------------
// pool_free_sized
// ----------------------------------------------------------------------------

void pool_free_sized(void *ptr, size_t size) noexcept {

    if (!ptr) {
        return;
    }

    if (size <= 4096) {

        if (g_pool_allocator.free_sized(ptr, size)) {
            return;
        }
    }

    rpfree(ptr);
}

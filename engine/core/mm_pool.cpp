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

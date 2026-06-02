// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

// ============================================================================
// RingBuffer — lock-free single-producer single-consumer queue
// ----------------------------------------------------------------------------
// - Fixed-size power-of-2 ring buffer
// - Wait-free fast path for SPSC workloads
// - No heap allocations after construction
// - Cache-friendly contiguous storage
// - O(1) push/pop via atomic cursors
//
// Memory model:
// - Producer owns write cursor
// - Consumer owns read cursor
// - acquire/release synchronization between producer/consumer
//
// Performance rationale:
// - Power-of-2 capacity enables cheap index masking instead of modulo
// - Contiguous storage improves cache locality and hardware prefetching
// - Cursor cache-line separation reduces false sharing
//
// Constraints:
// - Single producer thread only
// - Single consumer thread only
// - reset()/clear() require external synchronization
//
// Notes:
// T must tolerate move-out + immediate destruction semantics
// - This implementation is NOT safe for MPMC usage
// - MPMC requires per-slot sequencing or separate reservation/commit cursors
// ============================================================================
template <typename T, size_t Capacity> class RingBuffer {

    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 greater than 0");
    // half-range rule for unsigned cursor arithmetic
    static_assert(Capacity <= (1ull << 31));

    static_assert(std::is_nothrow_copy_constructible_v<T>);
    static_assert(std::is_nothrow_move_constructible_v<T>);
    static_assert(std::is_nothrow_move_assignable_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);

    static constexpr size_t MASK      = Capacity - 1;

    static constexpr size_t CACHELINE = 64;

    struct alignas(CACHELINE) Cursor {
        std::atomic<uint32_t> value{0};
    };
    Cursor write;
    char   pad1[64];
    Cursor read;
    char   pad2[64];

    alignas(alignof(T)) std::byte storage[sizeof(T) * Capacity];

    T       *ptr(size_t i) noexcept { return std::launder(reinterpret_cast<T *>(storage + i * sizeof(T))); }

    const T *ptr(size_t i) const noexcept { return std::launder(reinterpret_cast<const T *>(storage + i * sizeof(T))); }

  public:
    RingBuffer()                              = default;
    RingBuffer(const RingBuffer &)            = delete;
    RingBuffer &operator=(const RingBuffer &) = delete;

    RingBuffer(RingBuffer &&)                 = delete;
    RingBuffer &operator=(RingBuffer &&)      = delete;

    ~RingBuffer() noexcept { clear(); }

    bool push(const T &item) noexcept {

        uint32_t w = write.value.load(std::memory_order_relaxed);
        uint32_t r = read.value.load(std::memory_order_acquire);

        if (w - r >= Capacity) {
            return false;
        }

        ::new (ptr(w & MASK)) T(item);

        write.value.store(w + 1, std::memory_order_release);

        return true;
    }

    bool push(T &&item) noexcept {

        uint32_t w = write.value.load(std::memory_order_relaxed);
        uint32_t r = read.value.load(std::memory_order_acquire);

        if (w - r >= Capacity) {
            return false;
        }

        ::new (ptr(w & MASK)) T(std::move(item));

        write.value.store(w + 1, std::memory_order_release);

        return true;
    }

    bool pop(T &out) noexcept {

        uint32_t r = read.value.load(std::memory_order_relaxed);
        uint32_t w = write.value.load(std::memory_order_acquire);

        if (r == w) {
            return false;
        }

        T *p = ptr(r & MASK);

        out  = std::move(*p);

        p->~T();

        read.value.store(r + 1, std::memory_order_release);

        return true;
    }

    // REQUIRES:
    // producer and consumer threads must be stopped externally
    void clear() noexcept {
        uint32_t r = read.value.load(std::memory_order_relaxed);
        uint32_t w = write.value.load(std::memory_order_relaxed);

        while (r != w) {
            ptr(r & MASK)->~T();
            ++r;
        }

        read.value.store(w, std::memory_order_relaxed);
    }

    // reset() requires full producer/consumer stop synchronization
    void reset() noexcept {
        clear();

        write.value.store(0, std::memory_order_relaxed);
        read.value.store(0, std::memory_order_relaxed);
    }

    // approximate under concurrent access
    size_t count() const noexcept {

        uint32_t w = write.value.load(std::memory_order_acquire);
        uint32_t r = read.value.load(std::memory_order_acquire);

#ifndef NDEBUG
        assert((w - r) <= Capacity);
#endif

        return static_cast<size_t>(w - r);
    }

    bool empty() const noexcept { return count() == 0; }

    bool full() const noexcept { return count() == Capacity; }
};

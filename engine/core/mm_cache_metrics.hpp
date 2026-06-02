// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include <atomic>

// Cache Metrics — instrumentation counters for zero-alloc / cache behavior validation
// @cache_reason Non-instrumented paths compile to zero overhead; counters are cache-line padded
// @fallback Instrumentation compiles out entirely in release (ENGINE_ENABLE_ASSERT=OFF)
// @instrumentation Tracks allocations/frame, slotmap lookups in hot loop, pool overflow

#if defined(ENGINE_ENABLE_ASSERT) && ENGINE_ENABLE_ASSERT

struct CacheCounters {
    alignas(64) std::atomic<uint32_t> allocations_this_frame{0};
    alignas(64) std::atomic<uint32_t> slotmap_lookups_in_hot_loop{0};
    alignas(64) std::atomic<uint32_t> pool_overflows{0};

    void reset_frame() noexcept {
        allocations_this_frame.store(0, std::memory_order_relaxed);
        slotmap_lookups_in_hot_loop.store(0, std::memory_order_relaxed);
    }

    void track_alloc() noexcept {
        allocations_this_frame.fetch_add(1, std::memory_order_relaxed);
    }

    void track_hot_lookup() noexcept {
        slotmap_lookups_in_hot_loop.fetch_add(1, std::memory_order_relaxed);
    }

    void track_pool_overflow() noexcept {
        pool_overflows.fetch_add(1, std::memory_order_relaxed);
    }

    uint32_t allocs() const noexcept { return allocations_this_frame.load(std::memory_order_relaxed); }
    uint32_t hot_lookups() const noexcept { return slotmap_lookups_in_hot_loop.load(std::memory_order_relaxed); }
    uint32_t overflows() const noexcept { return pool_overflows.load(std::memory_order_relaxed); }
};

inline CacheCounters g_cache_counters;

#define TRACK_ALLOC()         g_cache_counters.track_alloc()
#define TRACK_HOT_LOOKUP()    g_cache_counters.track_hot_lookup()
#define TRACK_POOL_OVERFLOW() g_cache_counters.track_pool_overflow()

#else

#define TRACK_ALLOC()
#define TRACK_HOT_LOOKUP()
#define TRACK_POOL_OVERFLOW()

#endif

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

// ============================================================================
// Pool — fixed-size slab allocator
// ----------------------------------------------------------------------------
// - O(1) alloc/free via intrusive freelist
// - Fixed-size slots
// - Backed by user-supplied aligned allocator
// - Not thread-safe
// - Designed for ECS / renderer / game-engine workloads
// ============================================================================

class Pool {
  public:
    using AllocFn                 = void *(*)(size_t size, size_t alignment);
    using FreeFn                  = void (*)(void *ptr, size_t size);

    Pool() noexcept               = default;

    Pool(const Pool &)            = delete;
    Pool &operator=(const Pool &) = delete;

    // ------------------------------------------------------------------------
    // init
    // ------------------------------------------------------------------------
    void  init(size_t slot_size, size_t slots_per_block, AllocFn alloc_fn, FreeFn free_fn, size_t alignment = alignof(std::max_align_t)) noexcept {
        assert(slot_size > 0);
        assert(slots_per_block > 0);

        assert(alloc_fn);
        assert(free_fn);

        assert((alignment & (alignment - 1)) == 0 && "alignment must be power-of-two");

        assert(heap_alloc_ == nullptr && "Pool already initialized");

        heap_alloc_      = alloc_fn;
        heap_free_       = free_fn;

        alignment_       = (alignment < alignof(void *)) ? alignof(void *) : alignment;

        // intrusive freelist requires pointer-sized slot minimum
        slot_size_       = (slot_size < sizeof(FreeNode)) ? sizeof(FreeNode) : slot_size;

        // align slot size
        slot_size_       = (slot_size_ + alignment_ - 1) & ~(alignment_ - 1);

        slots_per_block_ = slots_per_block;
    }

    // ------------------------------------------------------------------------
    // alloc
    // ------------------------------------------------------------------------
    [[nodiscard]]
    void *alloc() noexcept {
        if (!free_head_) {
            if (!grow()) {
                return nullptr;
            }
        }

        FreeNode *node = free_head_;
        free_head_     = node->next;

        ++allocated_;

#ifndef NDEBUG
        std::memset(node, 0xCD, slot_size_);
#endif

        return node;
    }

    // ------------------------------------------------------------------------
    // free
    // ------------------------------------------------------------------------
    void free(void *ptr) noexcept {
        if (!ptr) {
            return;
        }

#ifndef NDEBUG
        assert(owns(ptr) && "pointer does not belong to this pool");

        assert(allocated_ > 0 && "double free / corrupted pool");

        auto *dbg = static_cast<FreeNode *>(ptr);

        assert(dbg->magic != kFreeMagic && "double free detected");

        std::memset(ptr, 0xDD, slot_size_);

        dbg->magic = kFreeMagic;
#endif

        auto *node = static_cast<FreeNode *>(ptr);

        node->next = free_head_;
        free_head_ = node;

        --allocated_;
    }

    // ------------------------------------------------------------------------
    // shutdown
    // ------------------------------------------------------------------------
    void shutdown() noexcept {
#ifndef NDEBUG
        assert(allocated_ == 0 && "pool leak detected");
#endif

        Block *b = blocks_;

        while (b) {
            Block *next = b->next;

            heap_free_(b, b->block_size);

            b = next;
        }

        blocks_     = nullptr;
        free_head_  = nullptr;

        capacity_   = 0;
        allocated_  = 0;

        heap_alloc_ = nullptr;
        heap_free_  = nullptr;
    }

    // ------------------------------------------------------------------------
    // owns (debug path)
    // O(blocks)
    // ------------------------------------------------------------------------
    [[nodiscard]]
    bool owns(void *ptr) const noexcept {
        auto *p = static_cast<std::byte *>(ptr);

        for (Block *b = blocks_; b; b = b->next) {

            auto *start = reinterpret_cast<std::byte *>(b) + aligned_header_size();

            auto *end   = reinterpret_cast<std::byte *>(b) + b->block_size;

            if (p >= start && p < end) {

                size_t offset = static_cast<size_t>(p - start);

                return (offset % slot_size_) == 0;
            }
        }

        return false;
    }

    // ------------------------------------------------------------------------
    // stats
    // ------------------------------------------------------------------------
    [[nodiscard]]
    size_t slot_size() const noexcept {
        return slot_size_;
    }

    [[nodiscard]]
    size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]]
    size_t allocated() const noexcept {
        return allocated_;
    }

    [[nodiscard]]
    size_t free_count() const noexcept {
        return capacity_ - allocated_;
    }

  private:
#ifndef NDEBUG
    static constexpr uint32_t kFreeMagic = 0xDEADBEEF;
    static constexpr uint32_t kUsedMagic = 0xCAFEBABE;
#endif

    // ------------------------------------------------------------------------
    // intrusive freelist node
    // ------------------------------------------------------------------------
    struct FreeNode {
        FreeNode *next;

#ifndef NDEBUG
        uint32_t magic;
#endif
    };

    // ------------------------------------------------------------------------
    // block header
    // ------------------------------------------------------------------------
    struct Block {
        Block *next;
        size_t block_size;
    };

    // ------------------------------------------------------------------------
    // grow pool
    // ------------------------------------------------------------------------
    bool grow() noexcept {

        if (slots_per_block_ == 0) {
            return false;
        }

        if (slot_size_ > SIZE_MAX / slots_per_block_) {
            return false;
        }

        const size_t header_size = aligned_header_size();

        const size_t slots_size  = slot_size_ * slots_per_block_;

        if (header_size > SIZE_MAX - slots_size) {
            return false;
        }

        const size_t total_size = header_size + slots_size;

        void        *mem        = heap_alloc_(total_size, alignment_);

        if (!mem) {
            return false;
        }

        // allocator MUST honor alignment
        assert((reinterpret_cast<uintptr_t>(mem) & (alignment_ - 1)) == 0);

        auto *block        = new (mem) Block{};

        block->next        = blocks_;
        block->block_size  = total_size;

        blocks_            = block;

        auto     *start    = reinterpret_cast<std::byte *>(mem) + header_size;

        // prepend new freelist to old freelist
        FreeNode *old_head = free_head_;

        for (size_t i = 0; i < slots_per_block_; ++i) {

            auto *node = reinterpret_cast<FreeNode *>(start + i * slot_size_);

#ifndef NDEBUG
            node->magic = kFreeMagic;
#endif

            if (i == slots_per_block_ - 1) {
                node->next = old_head;
            } else {
                node->next = reinterpret_cast<FreeNode *>(start + (i + 1) * slot_size_);
            }

#ifndef NDEBUG
            std::memset(node, 0xDD, slot_size_);
#endif
        }

        free_head_  = reinterpret_cast<FreeNode *>(start);

        capacity_  += slots_per_block_;

        return true;
    }

    [[nodiscard]]
    size_t aligned_header_size() const noexcept {
        return (sizeof(Block) + alignment_ - 1) & ~(alignment_ - 1);
    }

  private:
    AllocFn   heap_alloc_      = nullptr;
    FreeFn    heap_free_       = nullptr;

    size_t    slot_size_       = 0;
    size_t    alignment_       = alignof(void *);
    size_t    slots_per_block_ = 0;

    size_t    capacity_        = 0;
    size_t    allocated_       = 0;

    FreeNode *free_head_       = nullptr;
    Block    *blocks_          = nullptr;
};

// ============================================================================
// Global Multi-Class Pool Allocator
// ============================================================================

struct PoolConfig {
    size_t slot_size;
    size_t slots_per_block;
};

static constexpr PoolConfig kPoolConfigs[] = {
    {32, 256}, {48, 256}, {96, 128}, {256, 64}, {4096, 32},
};

static constexpr size_t kPoolClassCount = sizeof(kPoolConfigs) / sizeof(kPoolConfigs[0]);

class GlobalPoolAllocator {
  public:
    void init(Pool::AllocFn alloc_fn, Pool::FreeFn free_fn) noexcept {
        for (size_t i = 0; i < kPoolClassCount; ++i) {

            pools_[i].init(kPoolConfigs[i].slot_size, kPoolConfigs[i].slots_per_block, alloc_fn, free_fn);
        }
    }

    void shutdown() noexcept {
        for (auto &p : pools_) {
            p.shutdown();
        }
    }

    [[nodiscard]]
    void *alloc(size_t size) noexcept {

        const size_t idx = size_to_class(size);

        if (idx >= kPoolClassCount) {
            return nullptr;
        }

        return pools_[idx].alloc();
    }

    bool free(void *ptr) noexcept {

        for (auto &p : pools_) {

            if (p.owns(ptr)) {
                p.free(ptr);
                return true;
            }
        }

        return false;
    }

    [[nodiscard]]
    Pool *pool_for_size(size_t size) noexcept {

        const size_t idx = size_to_class(size);

        if (idx >= kPoolClassCount) {
            return nullptr;
        }

        return &pools_[idx];
    }

    static size_t size_to_class(size_t size) noexcept {

        if (size <= 32) {
            return 0;
        }
        if (size <= 48) {
            return 1;
        }
        if (size <= 96) {
            return 2;
        }
        if (size <= 256) {
            return 3;
        }
        if (size <= 4096) {
            return 4;
        }

        return kPoolClassCount;
    }

    bool free_sized(void *ptr, size_t size) noexcept {

        const size_t idx = size_to_class(size);

        if (idx >= kPoolClassCount) {
            return false;
        }

#ifndef NDEBUG
        assert(pools_[idx].owns(ptr));
#endif

        pools_[idx].free(ptr);

        return true;
    }

  private:
    Pool pools_[kPoolClassCount];
};

// ============================================================================
// Global Pool API
// ============================================================================

extern GlobalPoolAllocator g_pool_allocator;

void                       PoolInit() noexcept;
void                       PoolShutdown() noexcept;

void                      *pool_alloc(size_t size) noexcept;

void                       pool_free(void *ptr) noexcept;

void                       pool_free_sized(void *ptr, size_t size) noexcept;

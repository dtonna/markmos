// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "mm_handle.hpp"
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

// Slotmap — chunk-based generational handle map
//
// @cache_reason    contiguous chunk arrays = linear memory walk on iteration;
//                  no per-node heap allocation unlike std::unordered_map;
//                  LIFO freelist keeps recently-freed (hot) slots at front
// @zero_virtual    no virtual methods; Iter::next() is a plain inline loop
// @handle_strategy SlotHandle{id, gen} — generation mismatch = stale handle;
//                  hot loop receives resolved dense indices, not handles
// @pool_bound      grows by doubling chunks; each chunk = ChunkSize slots;
//                  max capacity = UINT32_MAX slots (id field width)
// @instrumentation size() / capacity() / empty() for Tracy overlay
// @api_compat      C++20 (std::countr_zero, std::construct_at); no POSIX deps

template <typename T, uint32_t ChunkSize = 64> class Slotmap {
    static_assert(ChunkSize > 0 && (ChunkSize & (ChunkSize - 1)) == 0, "ChunkSize must be a power of two");

    // Slot layout: value first so &slot->value == slot_ptr (no offset math).
    // gen starts at 1: invalid() handle also carries gen=1, but id=INVALID_ID
    // prevents it from ever matching a real slot.
    // gen is uint16_t: wraps at 65536 free/reuse cycles (vs uint8_t = 256).
    struct Slot {
        T        value;
        uint16_t gen       = 1;
        bool     active    = false;
        uint8_t  pad       = 0;        // explicit pad — no compiler surprise
        uint32_t next_free = NULL_IDX; // freelist link; valid only when !active
    };

    static constexpr uint32_t NULL_IDX     = 0xFFFF'FFFFu;
    static constexpr uint32_t CHUNK_MASK   = ChunkSize - 1;
    // std::countr_zero: C++20, portable across Clang/GCC/MSVC
    // replaces __builtin_ctz which is GCC/Clang only
    static constexpr uint32_t CHUNK_SHIFT  = std::countr_zero(ChunkSize);

    Slot                    **chunks_      = nullptr;
    uint32_t                  chunk_count_ = 0;
    uint32_t                  capacity_    = 0;
    uint32_t                  size_        = 0;
    uint32_t                  free_head_   = NULL_IDX;
    uint32_t                  free_tail_   = NULL_IDX; // O(1) freelist append on grow()

    // ── Internal helpers ─────────────────────────────────────────────────────

    Slot                     *slot_at(uint32_t idx) noexcept { return &chunks_[idx >> CHUNK_SHIFT][idx & CHUNK_MASK]; }
    const Slot               *slot_at(uint32_t idx) const noexcept { return &chunks_[idx >> CHUNK_SHIFT][idx & CHUNK_MASK]; }

    // grow — allocate one or more new chunks; link their slots into freelist.
    // Returns false on allocation failure; existing state is preserved.
    bool                      grow() noexcept {
        const uint32_t new_chunk_count = chunk_count_ == 0 ? 1 : chunk_count_ * 2;

        // Resize the chunk pointer array.
        // realloc is safe here: Slot* is trivially copyable.
        auto         **new_chunks      = static_cast<Slot **>(std::realloc(chunks_, new_chunk_count * sizeof(Slot *)));
        if (!new_chunks) {
            return false;
        }
        chunks_                = new_chunks;

        const uint32_t old_cap = capacity_;

        // Allocate each new chunk. On partial failure, free already-allocated
        // new chunks and leave the slotmap in its pre-grow state.
        for (uint32_t i = chunk_count_; i < new_chunk_count; ++i) {
            // std::aligned_alloc: C11/C++17, portable on all three targets.
            // Size must be a multiple of alignment — verified by static_assert.
            static_assert((ChunkSize * sizeof(Slot)) % 64 == 0 || sizeof(Slot) % 64 == 0 || ChunkSize % (64 / sizeof(Slot) + 1) == 0,
                          "chunk byte size must be a multiple of 64 for aligned_alloc");
            void *mem = std::aligned_alloc(64, ChunkSize * sizeof(Slot));
            if (!mem) {
                // Partial failure: free newly-allocated chunks, restore count.
                for (uint32_t j = chunk_count_; j < i; ++j) {
                    std::free(chunks_[j]);
                }
                // Note: chunks_ pointer was already updated; chunk_count_ and
                // capacity_ are still the old values, so slotmap stays valid.
                return false;
            }
            auto *s = static_cast<Slot *>(mem);
            // Initialise slots with placement-new so non-trivial T gets proper
            // default construction. gen=1 matches SlotHandle convention.
            for (uint32_t j = 0; j < ChunkSize; ++j) {
                ::new (&s[j]) Slot{}; // value default-init; gen=1, active=false
            }
            chunks_[i] = s;
        }

        chunk_count_             = new_chunk_count;
        capacity_                = new_chunk_count * ChunkSize;

        // Link all newly-added slots into a forward chain.
        // first_new is the global index of the first slot in the first new chunk.
        const uint32_t first_new = old_cap;
        for (uint32_t j = 0; j < (capacity_ - old_cap) - 1; ++j) {
            slot_at(first_new + j)->next_free = first_new + j + 1;
        }
        slot_at(capacity_ - 1)->next_free = NULL_IDX; // tail sentinel

        // Append new chain to freelist in O(1) using free_tail_.
        if (free_head_ == NULL_IDX) {
            free_head_ = first_new;
        } else {
            slot_at(free_tail_)->next_free = first_new;
        }
        free_tail_ = capacity_ - 1;

        return true;
    }

  public:
    // ── Lifecycle ────────────────────────────────────────────────────────────

    Slotmap() noexcept = default;

    ~Slotmap() noexcept {
        // Explicitly destroy active values (T may be non-trivially destructible).
        for (uint32_t i = 0; i < capacity_; ++i) {
            Slot *s = slot_at(i);
            if (s->active) {
                s->value.~T();
            }
            s->~Slot(); // destroy Slot shell (gen, active, next_free are trivial)
        }
        for (uint32_t i = 0; i < chunk_count_; ++i) {
            std::free(chunks_[i]);
        }
        std::free(chunks_);
    }

    Slotmap(const Slotmap &)            = delete;
    Slotmap &operator=(const Slotmap &) = delete;

    Slotmap(Slotmap &&o) noexcept
        : chunks_(o.chunks_), chunk_count_(o.chunk_count_), capacity_(o.capacity_), size_(o.size_), free_head_(o.free_head_),
          free_tail_(o.free_tail_) { // FIX: free_tail_ must be moved too
        o.chunks_      = nullptr;
        o.chunk_count_ = 0;
        o.capacity_    = 0;
        o.size_        = 0;
        o.free_head_   = NULL_IDX; // FIX: NULL_IDX not 0 (0 = first slot index)
        o.free_tail_   = NULL_IDX;
    }

    Slotmap &operator=(Slotmap &&o) noexcept {
        if (this != &o) {
            this->~Slotmap();
            ::new (this) Slotmap(std::move(o));
        }
        return *this;
    }

    // ── Mutating operations ──────────────────────────────────────────────────

    // emplace — construct T in-place; returns a valid SlotHandle on success,
    // SlotHandle::invalid() if grow() fails (allocation error).
    template <typename... Args> [[nodiscard]] SlotHandle emplace(Args &&...args) noexcept {
        if (free_head_ == NULL_IDX) {
            if (!grow()) {
                return SlotHandle::invalid();
            }
        }

        const uint32_t idx  = free_head_;
        Slot          *slot = slot_at(idx);

        free_head_          = slot->next_free;
        if (free_head_ == NULL_IDX) {
            free_tail_ = NULL_IDX; // list now empty
        }

        std::construct_at(&slot->value, std::forward<Args>(args)...);
        slot->active = true;
        ++size_;

        return SlotHandle{idx, slot->gen};
    }

    // free — invalidate handle and destroy value.
    // Returns false for stale/invalid handles (double-free safe).
    bool free(SlotHandle h) noexcept {
        if (!h.is_valid()) {
            return false;
        }

        const uint32_t idx = h.id;
        if (idx >= capacity_) {
            return false;
        }

        Slot *slot = slot_at(idx);
        if (!slot->active || slot->gen != h.gen) {
            return false;
        }

        slot->value.~T();
        slot->active = false;
        ++slot->gen; // stale handles (same id, old gen) now fail validation

        --size_;

        // LIFO push: recently-freed slot goes to front of freelist.
        // Keeps the hot (cache-warm) slot at the front for the next emplace().
        slot->next_free = free_head_;
        if (free_head_ == NULL_IDX) {
            free_tail_ = idx; // was empty; update tail
        }
        free_head_ = idx;

        return true;
    }

    // ── Lookup ───────────────────────────────────────────────────────────────

    // get — O(1) validated lookup.
    // Returns nullptr for invalid, stale, or out-of-range handles.
    // Hot-path callers should resolve to a direct index array before looping.
    [[nodiscard]] T *get(SlotHandle h) noexcept {
        if (!h.is_valid()) {
            return nullptr;
        }
        const uint32_t idx = h.id;
        if (idx >= capacity_) {
            return nullptr;
        }
        Slot *slot = slot_at(idx);
        if (!slot->active || slot->gen != h.gen) {
            return nullptr;
        }
        return &slot->value;
    }

    [[nodiscard]] const T *get(SlotHandle h) const noexcept {
        if (!h.is_valid()) {
            return nullptr;
        }
        const uint32_t idx = h.id;
        if (idx >= capacity_) {
            return nullptr;
        }
        const Slot *slot = slot_at(idx);
        if (!slot->active || slot->gen != h.gen) {
            return nullptr;
        }
        return &slot->value;
    }

    // ── Iteration ────────────────────────────────────────────────────────────

    // Iter::next() — linear scan; returns nullptr when exhausted.
    // Cache-friendly: walks chunk arrays sequentially.
    //
    // For SIMD/batch update, use collect_dense_indices() to get a flat
    // index array first, then process with span<uint32_t>.
    struct Iter {
        Slotmap *map;
        uint32_t idx = 0;

        T       *next() noexcept {
            while (idx < map->capacity_) {
                Slot *s = map->slot_at(idx++);
                if (s->active) {
                    return &s->value;
                }
            }
            return nullptr;
        }
    };

    [[nodiscard]] Iter iter() noexcept { return Iter{this, 0}; }

    // collect_dense_indices — fill caller-provided buffer with indices of all
    // active slots; returns count written.
    // Hot loops should call this once per frame, then iterate the index array
    // directly (no handle validation per element).
    uint32_t           collect_dense_indices(uint32_t *out, uint32_t out_cap) const noexcept {
        assert(out != nullptr);
        uint32_t count = 0;
        for (uint32_t i = 0; i < capacity_ && count < out_cap; ++i) {
            if (slot_at(i)->active) {
                out[count++] = i;
            }
        }
        return count;
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    [[nodiscard]] uint32_t size() const noexcept { return size_; }
    [[nodiscard]] uint32_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool     empty() const noexcept { return size_ == 0; }
};

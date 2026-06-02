// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

// FrameArena — linear bump allocator, reset per frame
//
// @cache_reason      bump pointer = sequential alloc → sequential access → L1 friendly;
//                    no free() overhead, no fragmentation, single pointer write per alloc
// @zero_virtual      no virtual methods, no inheritance, no std::function
// @handle_strategy   raw pointer return; caller owns lifetime within frame scope only
// @pool_bound        single contiguous buffer; reset() reclaims entire capacity in O(1)
// @instrumentation   marker()/rewind() for scoped temp allocs; used_pct() for Tracy overlay
// @api_compat        C++23; no platform-specific intrinsics — compiles on all three targets
//
// alignas(64): when multiple thread-local arenas coexist (render thread + game thread),
// each arena header occupies exactly one cache line, preventing false sharing.
// static_assert below verifies the struct stays within that budget.

class alignas(64) FrameArena {
    std::byte *base_     = nullptr;
    size_t     capacity_ = 0;
    size_t     offset_   = 0;

  public:
    // Marker: opaque save-point for rewind(); a plain offset under the hood.
    using Marker          = size_t;

    // ── Lifecycle ────────────────────────────────────────────────────────────

    FrameArena() noexcept = default;

    explicit FrameArena(void *buffer, size_t size) noexcept { init(buffer, size); }

    // Copy: deleted — an arena owns its backing buffer; copying is meaningless.
    FrameArena(const FrameArena &)            = delete;
    FrameArena &operator=(const FrameArena &) = delete;

    // Move: allowed for engine init phase (e.g. returning from a factory).
    // After move, source is left in a valid-but-empty state (base_=nullptr).
    FrameArena(FrameArena &&o) noexcept : base_(o.base_), capacity_(o.capacity_), offset_(o.offset_) {
        o.base_     = nullptr;
        o.capacity_ = 0;
        o.offset_   = 0;
    }

    FrameArena &operator=(FrameArena &&o) noexcept {
        if (this != &o) {
            base_       = o.base_;
            capacity_   = o.capacity_;
            offset_     = o.offset_;
            o.base_     = nullptr;
            o.capacity_ = 0;
            o.offset_   = 0;
        }
        return *this;
    }

    // init — attach arena to an externally-owned buffer.
    //
    // The buffer MUST be at least 16-byte aligned; otherwise SIMD alloc
    // requests (align=16/32) cannot be satisfied — the internal math is
    // correct but the base itself would be misaligned.
    void init(void *buffer, size_t size) noexcept {
        assert(buffer != nullptr && "FrameArena::init — buffer must not be null");
        assert(size > 0 && "FrameArena::init — size must be > 0");
        assert((reinterpret_cast<uintptr_t>(buffer) & 15u) == 0 && "FrameArena::init — buffer must be at least 16-byte aligned "
                                                                   "(required for SIMD alloc requests)");

        base_     = static_cast<std::byte *>(buffer);
        capacity_ = size;
        offset_   = 0;
    }

    // ── Core allocation ──────────────────────────────────────────────────────

    // alloc — aligned bump allocation; O(1), no lock, no free.
    //
    // align must be a power-of-two >= 1.
    // Returns nullptr (+ fires assert in debug) on overflow.
    // In release builds the assert is stripped — caller must check the return value.
    [[nodiscard]]
    void *alloc(size_t size, size_t align = 16) noexcept {
        assert(base_ != nullptr && "FrameArena::alloc — arena not initialised; call init() first");
        assert(align >= 1 && (align & (align - 1)) == 0 && "FrameArena::alloc — align must be a power-of-two >= 1");

        // Guard: (offset_ + align - 1) can wrap size_t when offset_ is near
        // SIZE_MAX.  Check before the addition, not after.
        if (offset_ > SIZE_MAX - (align - 1)) {
            assert(false && "FrameArena::alloc — alignment padding would overflow size_t");
            return nullptr;
        }

        const size_t start = (offset_ + align - 1) & ~(align - 1);

        // Single capacity check covers both (start > capacity_) and
        // (start + size > capacity_) without a redundant branch.
        // Using subtraction form avoids a second addition overflow.
        if (size > capacity_ - start) {
            assert(false && "FrameArena::alloc — arena capacity exhausted");
            return nullptr;
        }

        offset_ = start + size;
        return base_ + start;
    }

    // make<T> — placement-new a single object in the arena.
    //
    // Constraints (both enforced at compile time):
    //   is_trivially_destructible — reset() never calls destructors
    //   is_nothrow_constructible  — function is noexcept; a throwing ctor
    //                               would call std::terminate instead of
    //                               propagating — a silent crash.
    template <typename T, typename... Args>
    [[nodiscard]]
    T *make(Args &&...args) noexcept {
        static_assert(std::is_trivially_destructible_v<T>, "FrameArena::make — T must be trivially destructible "
                                                           "(reset() does not invoke destructors)");
        static_assert(std::is_nothrow_constructible_v<T, Args...>, "FrameArena::make — T constructor must be noexcept "
                                                                   "(a throwing ctor inside a noexcept function calls std::terminate)");

        void *mem = alloc(sizeof(T), alignof(T));
        if (!mem) {
            return nullptr;
        }

        // std::launder: not strictly required here because we are constructing
        // into raw storage (not reusing memory that previously held a different
        // object with const/reference members). Included for explicitness and
        // forward-compatibility should the usage pattern ever change.
        return std::launder(::new (mem) T(std::forward<Args>(args)...));
    }

    // alloc_array<T> — uninitialised array; no constructor is called.
    //
    // The name is intentionally kept as alloc_array (not alloc_uninit_array)
    // because the static_assert already communicates the trivial-only semantic.
    template <typename T>
    [[nodiscard]]
    T *alloc_array(size_t count) noexcept {
        static_assert(std::is_trivially_destructible_v<T>, "FrameArena::alloc_array — T must be trivially destructible");

        // Overflow-safe: check before multiplication.
        if (count > SIZE_MAX / sizeof(T)) {
            assert(false && "FrameArena::alloc_array — count * sizeof(T) overflows size_t");
            return nullptr;
        }

        return static_cast<T *>(alloc(count * sizeof(T), alignof(T)));
    }

    // ── Lifetime control ─────────────────────────────────────────────────────

    // reset — reclaim entire arena in O(1); no destructors called.
    void reset() noexcept { offset_ = 0; }

    // marker / rewind — lightweight scoped temporary allocations.
    //
    // Typical pattern (render-graph command building):
    //
    //   auto m = arena.marker();
    //   T* tmp = arena.alloc_array<T>(n);   // short-lived scratch
    //   ... build commands using tmp ...
    //   arena.rewind(m);                     // reclaim tail; earlier allocs intact
    //
    // Only safe for trivially destructible types — no destructors are called.
    [[nodiscard]]
    Marker marker() const noexcept {
        return offset_;
    }

    void rewind(Marker m) noexcept {
        assert(m <= offset_ && "FrameArena::rewind — marker is ahead of current offset (double rewind?)");
        offset_ = m;
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    [[nodiscard]] size_t used() const noexcept { return offset_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] size_t remaining() const noexcept {
        // offset_ <= capacity_ is invariant; assert catches logic errors early.
        assert(offset_ <= capacity_ && "FrameArena: offset_ > capacity_ — arena invariant violated");
        return capacity_ - offset_;
    }

    // used_pct — returns fill ratio in [0.0, 1.0].
    // Intended for Tracy / debug overlay only; float division makes it
    // inappropriate for the hot allocation path.
    // Guarded by MM_DEBUG so it compiles away completely in release.
#ifdef MM_DEBUG
    [[nodiscard]]
    float used_pct() const noexcept {
        return capacity_ ? static_cast<float>(offset_) / static_cast<float>(capacity_) : 0.0f;
    }
#endif
};

// Structural invariants — checked at compile time.
static_assert(std::is_move_constructible_v<FrameArena>, "FrameArena must be move-constructible (required by engine init phase)");
static_assert(!std::is_copy_constructible_v<FrameArena>, "FrameArena must not be copy-constructible (owns backing buffer)");
// sizeof must be a multiple of 64 (alignas(64) pads to the next multiple).
// Using <= 64 would fail the moment a single field is added — use modulo instead.
static_assert(sizeof(FrameArena) % 64 == 0, "FrameArena must be a multiple of one cache line (64 bytes); "
                                            "add padding if new fields push it past the current boundary");

// ─────────────────────────────────────────────────────────────────────────────
// DoubleArena — double-buffered FrameArena for CPU-write / GPU-read safety
//
// Frame N:  CPU writes to current(); GPU reads previous() (written in frame N-1).
// swap():   1. flip active_           → old current becomes new previous (GPU safe)
//           2. reset new current      → fresh slate for frame N+1 CPU writes
//
// WARNING — order matters in swap():
//   Resetting BEFORE flipping would wipe the buffer the GPU is still reading.
//   Do not reorder the two lines inside swap().
//
// @cache_reason  two arenas in one contiguous allocation; swap is XOR + reset
// @zero_virtual  no virtual methods; all calls resolve at compile time
// ─────────────────────────────────────────────────────────────────────────────

class DoubleArena {
    FrameArena arenas_[2];
    uint8_t    active_ = 0;

  public:
    // init — attach both halves to caller-owned buffers.
    // Both buffers MUST be the same size for correct ping-pong behaviour.
    void init(void *a, size_t as, void *b, size_t bs) noexcept {
        assert(a != nullptr && b != nullptr && "DoubleArena::init — buffers must not be null");
        assert(as == bs && "DoubleArena::init — both halves must have identical capacity; "
                           "mismatched sizes cause the smaller half to overflow first "
                           "with no diagnostic");

        arenas_[0].init(a, as);
        arenas_[1].init(b, bs);
    }

    // current()  — arena the CPU writes to this frame.
    // previous() — arena the GPU is reading from last frame; never reset while in flight.
    [[nodiscard]] FrameArena       &current() noexcept { return arenas_[active_]; }
    [[nodiscard]] const FrameArena &current() const noexcept { return arenas_[active_]; }
    [[nodiscard]] FrameArena       &previous() noexcept { return arenas_[active_ ^ 1]; }
    [[nodiscard]] const FrameArena &previous() const noexcept { return arenas_[active_ ^ 1]; }

    // swap — call once per frame, before any CPU writes for the new frame.
    // Step 1: flip  — previous() now holds what CPU just finished writing (GPU reads it).
    // Step 2: reset — new current() was previous()-from-last-frame; GPU is done with it.
    void                            swap() noexcept {
        active_ ^= 1;             // step 1: flip (do NOT move below reset)
        arenas_[active_].reset(); // step 2: reset new current (safe — GPU finished)
    }
};

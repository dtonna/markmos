// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include "core/mm_types.h"
#include <cstddef>
#include <cassert>
#include <concepts>
#include <type_traits>

// Draw Call Sort Key — u64 packed sort key
// Cache reason: single u64 comparison, hardware-accelerated sort (O(n log n))
// No pointer indirection, no virtual dispatch, sort by integer key
// 
// Layer bits ensure correct front-to-back ordering for 2D:
//   BACKGROUND(0) → GRID(1) → PIECES(2) → EFFECTS(3) → UI(4) → OVERLAY(5)
// Within same layer: sort by pipeline (batch), then material, then depth
//
// Packing: key = (layer << 56) | (pipeline << 44) | (material << 28) | (depth << 0)
//   layer:    8 bits  (256 layers — more than enough)
//   pipeline: 12 bits (4096 pipeline variants)
//   material: 16 bits (65536 material instances)
//   depth:    28 bits (268M depth values in fixed-point)

static constexpr u8 LAYER_BACKGROUND = 0;
static constexpr u8 LAYER_GRID       = 1;
static constexpr u8 LAYER_PIECES     = 2;
static constexpr u8 LAYER_EFFECTS    = 3;
static constexpr u8 LAYER_UI         = 4;
static constexpr u8 LAYER_OVERLAY    = 5;

struct SortKey {
    u64 key;

    constexpr SortKey() : key(0) {}

    constexpr SortKey(u8 layer, u16 pipeline, u16 material, f32 depth)
        : key(pack(layer, pipeline, material, depth)) {}

    static constexpr u64 pack(u8 layer, u16 pipeline, u16 material, f32 depth) noexcept {
        // Convert f32 depth to fixed-point 28-bit. Compute in DOUBLE: 2^28 - 1
        // (= 268435455) is not representable in float32, so `(u32)(1.0f *
        // 268435455.0f)` rounds up to 2^28 - past the field, bleeding a bit into
        // the material bits and giving depth() == 0 for depth == 1.0. This was an
        // overflow, not a packing slip.
        u32 d = static_cast<u32>(clamp01(depth) * 268435455.0);
        // Fields are packed without widening: a value past its width bleeds into
        // the bits below it. That must be a caught mistake, not a silent layer/material
        // corruption later in the sort order.
        assert((pipeline & ~(0x0FFF)) == 0 && "SortKey::pack - pipeline needs 12 bits");
        assert((material & ~(0xFFFF)) == 0 && "SortKey::pack - material needs 16 bits");
        if (d > 0x0FFFFFFFu) {
            d = 0x0FFFFFFFu;
        }
        return (static_cast<u64>(layer)     << 56) |
               (static_cast<u64>(pipeline)  << 44) |
               (static_cast<u64>(material)  << 28) |
               static_cast<u64>(d);
    }

    static constexpr f32 clamp01(f32 v) noexcept {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    u8  layer()    const noexcept { return static_cast<u8>(key >> 56); }
    u16 pipeline() const noexcept { return static_cast<u16>((key >> 44) & 0xFFF); }
    u16 material() const noexcept { return static_cast<u16>((key >> 28) & 0xFFFF); }
    u32 depth()    const noexcept { return static_cast<u32>(key & 0xFFFFFFF); }

    static constexpr SortKey min() noexcept { SortKey sk; sk.key = 0; return sk; }
    static constexpr SortKey max() noexcept { SortKey sk; sk.key = 0xFFFF'FFFF'FFFF'FFFFull; return sk; }

    bool operator<(SortKey o) const noexcept { return key < o.key; }
    bool operator>(SortKey o) const noexcept { return key > o.key; }
    bool operator==(SortKey o) const noexcept { return key == o.key; }
};

static_assert(sizeof(SortKey) == 8, "SortKey must be 8 bytes");
static_assert(std::is_trivially_copyable_v<SortKey>);

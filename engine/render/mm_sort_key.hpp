#pragma once
#include <cstdint>
#include <cstddef>
#include <concepts>
#include <type_traits>

// Draw Call Sort Key — uint64_t packed sort key
// Cache reason: single uint64_t comparison, hardware-accelerated sort (O(n log n))
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

static constexpr uint8_t LAYER_BACKGROUND = 0;
static constexpr uint8_t LAYER_GRID       = 1;
static constexpr uint8_t LAYER_PIECES     = 2;
static constexpr uint8_t LAYER_EFFECTS    = 3;
static constexpr uint8_t LAYER_UI         = 4;
static constexpr uint8_t LAYER_OVERLAY    = 5;

struct SortKey {
    uint64_t key;

    constexpr SortKey() : key(0) {}

    constexpr SortKey(uint8_t layer, uint16_t pipeline, uint16_t material, float depth)
        : key(pack(layer, pipeline, material, depth)) {}

    static constexpr uint64_t pack(uint8_t layer, uint16_t pipeline, uint16_t material, float depth) noexcept {
        // Convert float depth to fixed-point 28-bit
        // Normalize: clamp to [0, 1], map to 28-bit integer
        uint32_t d = static_cast<uint32_t>(clamp01(depth) * 268435455.0f);
        return (static_cast<uint64_t>(layer)     << 56) |
               (static_cast<uint64_t>(pipeline)  << 44) |
               (static_cast<uint64_t>(material)  << 28) |
               static_cast<uint64_t>(d);
    }

    static constexpr float clamp01(float v) noexcept {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    uint8_t  layer()    const noexcept { return static_cast<uint8_t>(key >> 56); }
    uint16_t pipeline() const noexcept { return static_cast<uint16_t>((key >> 44) & 0xFFF); }
    uint16_t material() const noexcept { return static_cast<uint16_t>((key >> 28) & 0xFFFF); }
    uint32_t depth()    const noexcept { return static_cast<uint32_t>(key & 0xFFFFFFF); }

    bool operator<(SortKey o) const noexcept { return key < o.key; }
    bool operator>(SortKey o) const noexcept { return key > o.key; }
    bool operator==(SortKey o) const noexcept { return key == o.key; }
};

static_assert(sizeof(SortKey) == 8, "SortKey must be 8 bytes");
static_assert(std::is_trivially_copyable_v<SortKey>);

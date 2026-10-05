// SpriteBatch device-pixel snap tests — masked layers land corners on
// the device grid, unmasked layers pass through, mask 0 disables.
// Plain main() + assert(), no framework. Headless: pure CPU.
#include "../render/mm_sprite_batch.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static u16 F(f32 v) noexcept {
    return SpriteBatch::float_to_f16(v);
}

static f32 Snapped(f32 v, f32 s) noexcept {
    return __builtin_roundf(v * s) / s;
}

int main() {
    // ─── Masked layer: corners land on the device grid ───
    {
        SpriteBatch b;
        b.init();
        b.snap_layer_mask = static_cast<u8>((1u << LAYER_GRID) | (1u << LAYER_PIECES));
        b.snap_scale      = 2.0f;
        // Fractional center AND fractional size (the center-only trap).
        b.add(10.3f, 20.7f, 87.5f, 40.0f, 0.0f, 0xFFFFFFFF, LAYER_GRID);
        SpriteVertex verts[8] = {};
        u32     n        = b.generate_vertices(verts, 8, 0.0f, 0.0f, 1.0f);
        assert(n == 6);
        // Corner 0 = bl = (cx-hx, cy-hy).
        assert(verts[0].x == F(Snapped(10.3f - 43.75f, 2.0f)));
        assert(verts[0].y == F(Snapped(20.7f - 20.0f, 2.0f)));
        // Corner 4 = tr = (cx+hx, cy+hy).
        assert(verts[4].x == F(Snapped(10.3f + 43.75f, 2.0f)));
        assert(verts[4].y == F(Snapped(20.7f + 20.0f, 2.0f)));
        // Sanity: snapped values really sit on device px (no .25/.75 tails).
        assert(Snapped(10.3f - 43.75f, 2.0f) * 2.0f == __builtin_roundf(Snapped(10.3f - 43.75f, 2.0f) * 2.0f));
    }

    // ─── Unmasked layer passes through untouched ───
    {
        SpriteBatch b;
        b.init();
        b.snap_layer_mask = static_cast<u8>((1u << LAYER_GRID) | (1u << LAYER_PIECES));
        b.snap_scale      = 2.0f;
        b.add(10.3f, 20.7f, 87.5f, 40.0f, 0.0f, 0xFFFFFFFF, LAYER_OVERLAY);
        SpriteVertex verts[8] = {};
        u32     n        = b.generate_vertices(verts, 8, 0.0f, 0.0f, 1.0f);
        assert(n == 6);
        assert(verts[0].x == F(10.3f - 43.75f));
        assert(verts[0].y == F(20.7f - 20.0f));
    }

    // ─── Mask 0 (default): no snapping anywhere ───
    {
        SpriteBatch b;
        b.init();
        assert(b.snap_layer_mask == 0);
        b.snap_scale = 2.0f;
        b.add(10.3f, 20.7f, 87.5f, 40.0f, 0.0f, 0xFFFFFFFF, LAYER_GRID);
        SpriteVertex verts[8] = {};
        u32     n        = b.generate_vertices(verts, 8, 0.0f, 0.0f, 1.0f);
        assert(n == 6);
        assert(verts[0].x == F(10.3f - 43.75f));
    }

    // ─── reset() preserves the snap config (per-frame resets must not drop it) ───
    {
        SpriteBatch b;
        b.init();
        b.snap_layer_mask = static_cast<u8>(1u << LAYER_GRID);
        b.snap_scale      = 2.0f;
        b.reset();
        assert(b.snap_layer_mask == static_cast<u8>(1u << LAYER_GRID));
        assert(b.snap_scale == 2.0f);
    }

    printf("[snap] all tests passed\n");
    return 0;
}

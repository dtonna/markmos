// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_mipmaps.hpp — CPU-side RGBA8 mipmap chain generation (header-only).
// Used for textures that minify on screen (card atlas: column-overlap
// compression, drag stacks, small windows). GPU generate-mipmap would need
// a new RHI call on both backends; update_texture() already takes a mip
// level on both, so CPU box-filter + per-level upload needs no RHI change.
// Font/text atlases must NOT use this (minified text would blur).
#pragma once
#include <cstdint>
#include "core/mm_types.h"
#include <cstdlib>

// Full chain length for w,h down to 1x1 (level 0 included).
inline u16 mip_level_count(u32 w, u32 h) noexcept {
    u32 m = w > h ? w : h;
    u16 n = 1;
    while (m > 1) {
        m >>= 1;
        ++n;
    }
    return n;
}

// One 2x2 box-filter step: dst is (sw/2)x(sh/2), sw/sh need not be even
// (edge texels replicate). Straight-alpha average; caller owns buffers.
inline void downsample_rgba8_box(u8 *dst, u32 dw, u32 dh, const u8 *src, u32 sw, u32 sh) noexcept {
    for (u32 y = 0; y < dh; ++y) {
        u32 sy  = y * 2;
        if (sy >= sh) {
            sy = sh - 1;
        }
        u32 sy1 = (sy + 1 < sh) ? sy + 1 : sy;
        for (u32 x = 0; x < dw; ++x) {
            u32       sx  = x * 2;
            if (sx >= sw) {
                sx = sw - 1;
            }
            u32       sx1 = (sx + 1 < sw) ? sx + 1 : sx;
            const u8 *p00 = src + (sy * sw + sx) * 4;
            const u8 *p10 = src + (sy * sw + sx1) * 4;
            const u8 *p01 = src + (sy1 * sw + sx) * 4;
            const u8 *p11 = src + (sy1 * sw + sx1) * 4;
            u8       *d  = dst + (y * dw + x) * 4;
            for (u32 c = 0; c < 4; ++c) {
                d[c] = static_cast<u8>((static_cast<u32>(p00[c]) + p10[c] + p01[c] + p11[c] + 2u) >> 2);
            }
        }
    }
}

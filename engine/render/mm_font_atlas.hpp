// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../rhi/mm_rhi_concept.hpp"
#include "core/mm_types.h"
#include "../core/mm_handle.hpp"
#include "stb_truetype.h"
#include <cstdint>
#include <cstddef>

static constexpr u16 FONT_ATLAS_W = 512;
static constexpr u16 FONT_ATLAS_H = 512;
static constexpr u8  FONT_FIRST_CHAR = 32;
static constexpr u8  FONT_NUM_CHARS  = 96;  // ASCII 32..127

struct FontAtlas {
    u8   pixels[FONT_ATLAS_W * FONT_ATLAS_H]{};
    f32     baked_chars[FONT_NUM_CHARS * 4];  // x0,y0,x1,y1 (UV) per char
    f32     xadvance[FONT_NUM_CHARS];
    f32     xoff[FONT_NUM_CHARS];
    f32     yoff[FONT_NUM_CHARS];
    u16  w[FONT_NUM_CHARS];
    u16  h[FONT_NUM_CHARS];
    u8   line_height;
    f32     inv_atlas_w, inv_atlas_h;

    void bake(const unsigned char* ttf_data, int ttf_size, f32 pixel_height) noexcept;
    // Bake Thai-range codepoints into the SAME pixel buffer, appending after start_y.
    // chardata_out receives raw stbtt_bakedchar values (y coords relative to real buffer).
    // Returns the next y-offset for chaining.
    int bake_range(const unsigned char* ttf_data, f32 pixel_height,
                   int first_char, int count, int start_y,
                   stbtt_bakedchar* chardata_out) const noexcept;
    void get_quad(u8 c, f32& x, f32& y,
                  f32& u0, f32& v0, f32& u1, f32& v1,
                  f32& gw, f32& gh) const noexcept;
};

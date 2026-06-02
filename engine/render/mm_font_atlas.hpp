// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../rhi/mm_rhi_concept.hpp"
#include "../core/mm_handle.hpp"
#include "stb_truetype.h"
#include <cstdint>
#include <cstddef>

static constexpr uint16_t FONT_ATLAS_W = 512;
static constexpr uint16_t FONT_ATLAS_H = 512;
static constexpr uint8_t  FONT_FIRST_CHAR = 32;
static constexpr uint8_t  FONT_NUM_CHARS  = 96;  // ASCII 32..127

struct FontAtlas {
    uint8_t   pixels[FONT_ATLAS_W * FONT_ATLAS_H]{};
    float     baked_chars[FONT_NUM_CHARS * 4];  // x0,y0,x1,y1 (UV) per char
    float     xadvance[FONT_NUM_CHARS];
    float     xoff[FONT_NUM_CHARS];
    float     yoff[FONT_NUM_CHARS];
    uint16_t  w[FONT_NUM_CHARS];
    uint16_t  h[FONT_NUM_CHARS];
    uint8_t   line_height;
    float     inv_atlas_w, inv_atlas_h;

    void bake(const unsigned char* ttf_data, int ttf_size, float pixel_height) noexcept;
    // Bake Thai-range codepoints into the SAME pixel buffer, appending after start_y.
    // chardata_out receives raw stbtt_bakedchar values (y coords relative to real buffer).
    // Returns the next y-offset for chaining.
    int bake_range(const unsigned char* ttf_data, float pixel_height,
                   int first_char, int count, int start_y,
                   stbtt_bakedchar* chardata_out) const noexcept;
    void get_quad(uint8_t c, float& x, float& y,
                  float& u0, float& v0, float& u1, float& v1,
                  float& gw, float& gh) const noexcept;
};

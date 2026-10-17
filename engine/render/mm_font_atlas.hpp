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

// ─── Appending font sets to one atlas ────────────────────────────
//
// Every font set (Karla, Thai, suits, the undo glyph) is appended below into a
// single texture, so each has to hand its end row to the next. These two are
// the production path for that, kept out of Renderer::init() so they can be
// tested headlessly — FontAtlas needs no backend.
//
// Both return the next row to append at, or start_y unchanged when nothing
// could be baked. Neither ever writes above start_y.

// Bulk bake of a contiguous codepoint range. If the range spans codepoints the
// font does not have, stbtt returns < 0 and nothing is placed — the caller
// decides whether to retry per glyph.
inline int AppendFontRange(FontAtlas& fa, const unsigned char* ttf, f32 pixel_height,
                           int first_char, int count, int start_y,
                           stbtt_bakedchar* chardata_out) noexcept {
    const int ret = fa.bake_range(ttf, pixel_height, first_char, count, start_y, chardata_out);
    return (ret > start_y) ? ret : start_y;
}

// Per-glyph fallback for a range the bulk bake rejected. ADVANCES THE CURSOR
// on every iteration: reusing one start_y makes each glyph overwrite the one
// before it, which is invisible until the font set is actually switched on.
inline int AppendFontRangeEach(FontAtlas& fa, const unsigned char* ttf, f32 pixel_height,
                               int first_char, int count, int start_y,
                               stbtt_bakedchar* chardata_out) noexcept {
    int cursor = start_y;
    for (int i = 0; i < count; ++i) {
        const int r = fa.bake_range(ttf, pixel_height, first_char + i, 1, cursor, &chardata_out[i]);
        if (r > cursor) {
            cursor = r;
        }
    }
    return cursor;
}

// Append an ALREADY-RASTERISED glyph, for the case where shipping the TTF to
// bake one character costs more than the character does. The bitmap is R8
// coverage exactly as stbtt_BakeFontBitmap produced it, and x0/y0/x1/y1 are
// relative to the glyph row origin (start_y == 0), which is how
// tools/build_undo_glyph.cpp emits them.
//
// Same contract as the two above: returns the next row to append at, and never
// writes above start_y.
inline int AppendBakedGlyph(FontAtlas& fa,
                            const unsigned char* bitmap, int gw, int gh,
                            int x0, int y0, int x1, int y1,
                            int xoff, int yoff, int xadvance, int start_y,
                            stbtt_bakedchar* out) noexcept {
    if (!bitmap || gw <= 0 || gh <= 0 || y1 <= y0) {
        return start_y;
    }
    const int dst_y0 = start_y + y0;
    if (dst_y0 < start_y || start_y + y1 >= FONT_ATLAS_H) {
        return start_y;  // would fall off the atlas; place nothing
    }
    for (int row = 0; row < gh; ++row) {
        unsigned char* dst = &fa.pixels[static_cast<u32>(dst_y0 + row) * FONT_ATLAS_W + x0];
        const unsigned char* src = &bitmap[static_cast<u32>(row) * gw];
        for (int col = 0; col < gw; ++col) {
            dst[col] = src[col];
        }
    }
    out->x0       = static_cast<int>(static_cast<short>(x0));
    out->y0       = static_cast<int>(static_cast<short>(dst_y0));
    out->x1       = static_cast<int>(static_cast<short>(x1));
    out->y1       = static_cast<int>(static_cast<short>(start_y + y1));
    out->xoff     = xoff;
    out->yoff     = yoff;
    out->xadvance = xadvance;
    return start_y + y1;
}

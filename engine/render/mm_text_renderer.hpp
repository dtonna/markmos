// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_handle.hpp"
#include "core/mm_types.h"
#include "../render/mm_sprite_batch.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

static constexpr u16 MAX_GLYPHS_EN  = 256;
static constexpr u16 MAX_GLYPHS_TH  = 128;
static constexpr u16 MAX_GLYPHS_SYM = 16;
static constexpr u16 MAX_GLYPH_BUF  = 1024;
static constexpr u8  SDF_RANGE     = 4;

struct GlyphInfo {
    u16 u, v;
    u16 w, h;
    i8   bearing_x;
    i8   bearing_y;
    u8  advance;
};

struct BitmapFont {
    TextureHandle atlas;
    GlyphInfo     glyphs[MAX_GLYPHS_EN];
    GlyphInfo     glyphs_th[MAX_GLYPHS_TH];
    GlyphInfo     glyphs_sym[MAX_GLYPHS_SYM];
    u8       line_height;
    u8       base_size;
    u16      atlas_w, atlas_h;

    void          init(TextureHandle tex, u8 size) noexcept {
        atlas       = tex;
        base_size   = size;
        line_height = static_cast<u8>(size * 1.2f);
        memset(glyphs, 0, sizeof(glyphs));
        memset(glyphs_th, 0, sizeof(glyphs_th));
        memset(glyphs_sym, 0, sizeof(glyphs_sym));
    }

    const GlyphInfo *get_glyph(u32 codepoint) const noexcept {
        if (codepoint < MAX_GLYPHS_EN) {
            return &glyphs[codepoint];
        }
        if (codepoint >= 0x0E01 && codepoint < 0x0E01 + MAX_GLYPHS_TH) {
            return &glyphs_th[codepoint - 0x0E01];
        }
        if (codepoint >= 0x2660 && codepoint < 0x2668) {
            return &glyphs_sym[codepoint - 0x2660];
        }
        if (codepoint == 0x238C) {
            if (glyphs_sym[8].w > 0 && glyphs_sym[8].h > 0) {
                return &glyphs_sym[8];
            }
            return nullptr;
        }
        return nullptr;
    }
};

struct SdfFont {
    TextureHandle atlas;
    GlyphInfo     glyphs[MAX_GLYPHS_EN + MAX_GLYPHS_TH];
    u16      atlas_w, atlas_h;
    u8       line_height;
    f32         sdf_range;

    void          init(TextureHandle tex) noexcept {
        atlas       = tex;
        sdf_range   = SDF_RANGE;
        line_height = 24;
        memset(glyphs, 0, sizeof(glyphs));
    }

    const GlyphInfo *get_glyph(u32 codepoint) const noexcept {
        if (codepoint < MAX_GLYPHS_EN) {
            return &glyphs[codepoint];
        }
        if (codepoint >= 0x0E01 && codepoint <= 0x0E7F) {
            u32 idx = MAX_GLYPHS_EN + (codepoint - 0x0E01);
            if (idx < MAX_GLYPHS_EN + MAX_GLYPHS_TH) {
                return &glyphs[idx];
            }
        }
        return nullptr;
    }
};

struct Utf8Decoder {
    const u8 *input;
    u32       pos;
    u32       len;

    Utf8Decoder(const char *str, u32 length) noexcept : input(reinterpret_cast<const u8 *>(str)), pos(0), len(length) {}

    u32 next() noexcept {
        if (pos >= len) {
            return 0;
        }
        u8 b = input[pos++];
        if (b < 0x80) {
            return b;
        }
        if (b < 0xE0 && pos < len) {
            return (static_cast<u32>(b & 0x1F) << 6) | (input[pos++] & 0x3F);
        }
        if (b < 0xF0 && pos + 1 < len) {
            u32 cp =
                (static_cast<u32>(b & 0x0F) << 12) | (static_cast<u32>(input[pos] & 0x3F) << 6) | static_cast<u32>(input[pos + 1] & 0x3F);
            pos += 2;
            return cp;
        }
        if (pos + 2 < len) {
            u32 cp = (static_cast<u32>(b & 0x07) << 18) | (static_cast<u32>(input[pos] & 0x3F) << 12) |
                          (static_cast<u32>(input[pos + 1] & 0x3F) << 6) | static_cast<u32>(input[pos + 2] & 0x3F);
            pos += 3;
            return cp;
        }
        return 0xFFFD;
    }
};

// Helper functions for Thai character detection.
// These are the DEFINITIONS. Renderer::measure_text and Renderer::draw_text
// used to carry their own lambda copies, and the copies had already drifted:
// both included U+0E4D (mai han akat) in "vowel above" and this one did not, so
// a string's width depended on which function you asked.
inline bool is_thai_consonant(u32 cp) noexcept {
    return (cp >= 0x0E01 && cp <= 0x0E2F) || cp == 0x0E30 || cp == 0x0E32 || cp == 0x0E33;
}

inline bool is_thai_vowel_above(u32 cp) noexcept { return cp == 0x0E31 || cp == 0x0E4D || (cp >= 0x0E34 && cp <= 0x0E37); }

inline bool is_thai_vowel_below(u32 cp) noexcept { return (cp >= 0x0E38 && cp <= 0x0E39); }

inline bool is_thai_vowel_front(u32 cp) noexcept { return (cp >= 0x0E40 && cp <= 0x0E44); }

inline bool is_thai_tone_mark(u32 cp) noexcept { return (cp >= 0x0E48 && cp <= 0x0E4B); }

inline bool is_thai_combining(u32 cp) noexcept { return is_thai_vowel_above(cp) || is_thai_vowel_below(cp) || is_thai_tone_mark(cp); }

// ─── Shared text measurement ─────────────────────────────────────
//
// These belong next to BitmapFont because THREE places measure and draw text
// (Manager::measure, Renderer::measure_text, Renderer::draw_text) and each used
// to carry its own copy of the rules. They disagreed in ways nobody could see:
// only draw_text substituted '?' for a missing glyph, so a character with no
// baked bitmap advanced 0 when measured and a full '?' when drawn - an AUTO_W
// frame sized for text the renderer never drew. One definition, three callers.

// Is this glyph actually drawable? get_glyph() returns a NON-NULL pointer to a
// zeroed entry for every codepoint < 256, so "non-null" does not mean "baked".
inline bool glyph_baked(const BitmapFont &f, const GlyphInfo *g) noexcept {
    return g != nullptr && g->w > 0 && g->h > 0 && static_cast<u32>(g->u + g->w) <= f.atlas_w &&
           static_cast<u32>(g->v + g->h) <= f.atlas_h;
}

// The glyph a codepoint resolves to, with the one substitution the toolkit
// agrees on. nullptr means "draw nothing, advance nothing" and is only
// reachable when '?' itself is unbaked.
inline const GlyphInfo *resolve_glyph(const BitmapFont &font, u32 codepoint) noexcept {
    const GlyphInfo *g = font.get_glyph(codepoint);
    if (glyph_baked(font, g)) {
        return g;
    }
    const GlyphInfo *q = font.get_glyph('?');
    return glyph_baked(font, q) ? q : nullptr;
}

// Sarabun sets Thai smaller than Karla does at the same bake size, so Thai
// strings draw 1.2x. The old test was "any byte >= 0xE0", which is a UTF-8 lead
// byte, not a Thai codepoint: it matched U+2019/U+201C/U+20AC as well. This
// decodes, and shares one definition with the draw pass - a 20% disagreement
// between the two is exactly how an AUTO_W frame came out too narrow for Thai.
inline f32 text_scale_for(const char *text, f32 base) noexcept {
    if (text == nullptr) {
        return base;
    }
    Utf8Decoder dec(text, static_cast<u32>(std::strlen(text)));
    for (;;) {
        const u32 cp = dec.next();
        if (cp == 0) {
            break;
        }
        if (cp >= 0x0E01 && cp <= 0x0E5B) {
            return base * 1.2f;
        }
    }
    return base;
}

struct TextMetrics {
    f32    widest  = 0.0f; // advance of the LONGEST line, at the given scale
    f32    ascent  = 0.0f; // ink box above the baseline
    f32    descent = 0.0f; // ink box below the baseline
    u32 lines   = 1;
};

// The ONE definition of "how big is this string" at `scale`. `widest` is the
// widest LINE, not the whole string: multi-line text is a block, and a frame
// sized to the concatenation of every line is wider than anything drawn in it.
inline TextMetrics measure_string(const BitmapFont &font, const char *txt, f32 scale) noexcept {
    TextMetrics m;
    if (txt == nullptr || txt[0] == '\0') {
        return m;
    }
    Utf8Decoder dec(txt, static_cast<u32>(std::strlen(txt)));
    f32       line_w           = 0.0f;
    u32    prev_consonant   = 0;
    for (;;) {
        const u32 cp = dec.next();
        if (cp == 0) {
            break;
        }
        if (cp == '\n') {
            if (line_w > m.widest) {
                m.widest = line_w;
            }
            line_w         = 0.0f;
            ++m.lines;
            prev_consonant = 0;
            continue;
        }
        const GlyphInfo *g = resolve_glyph(font, cp);
        if (g == nullptr) {
            continue;
        }
        const f32 asc = static_cast<f32>(-g->bearing_y) * scale;
        if (asc > m.ascent) {
            m.ascent = asc;
        }
        const int dpx = static_cast<int>(g->bearing_y) + static_cast<int>(g->h);
        if (dpx > 0) {
            const f32 desc = static_cast<f32>(dpx) * scale;
            if (desc > m.descent) {
                m.descent = desc;
            }
        }
        // Thai combining marks ride on the consonant before them: draw_text
        // draws them without advancing, so measuring them as an advance
        // (Manager::measure did) overshoots every syllable.
        if (is_thai_combining(cp) && prev_consonant != 0) {
            continue;
        }
        line_w += static_cast<f32>(g->advance) * scale;
        prev_consonant = is_thai_consonant(cp) ? cp : 0;
    }
    if (line_w > m.widest) {
        m.widest = line_w;
    }
    return m;
}

struct LineScan {
    f32  advance = 0.0f; // total advance of the line at `scale`
    f32  ascent  = 0.0f;
    f32  descent = 0.0f;
    size_t keep    = 0; // leading BYTES that fit alongside a trailing "..."
};

// One codepoint-correct walk of ONE line (no '\n' inside it), producing the
// numbers draw_text_lines needs three times over: the ink ascent for the
// baseline, the advance for alignment, and where an ellipsis cut may land.
// `budget` < 0 skips the fit walk.
//
// This replaced four byte-walks. A 3-byte Thai character was looked up as three
// separate Latin-1 codepoints, so its ascent, its width and the ellipsis cut
// point were all computed from glyphs that were never drawn - and the cut could
// land mid-character. Walking codepoints makes `keep` a character boundary by
// construction, so no UTF-8 back-off is needed any more.
inline LineScan scan_line(const BitmapFont &font, const char *line, size_t len, f32 scale, f32 budget) noexcept {
    LineScan     out;
    if (line == nullptr || len == 0) {
        out.keep = 0;
        return out;
    }
    out.keep             = len;
    const bool     fit   = budget >= 0.0f;
    f32          acc   = 0.0f;
    size_t         keep  = 0;
    Utf8Decoder   dec(line, static_cast<u32>(len));
    u32       prev_consonant = 0;
    for (;;) {
        const u32 cp = dec.next();
        if (cp == 0) {
            break;
        }
        const size_t      b1 = dec.pos;
        const GlyphInfo  *g  = resolve_glyph(font, cp);
        if (g == nullptr) {
            keep = b1; // draws nothing, but must not glue the next glyph on
            continue;
        }
        const f32 asc = static_cast<f32>(-g->bearing_y) * scale;
        if (asc > out.ascent) {
            out.ascent = asc;
        }
        const int dpx = static_cast<int>(g->bearing_y) + static_cast<int>(g->h);
        if (dpx > 0) {
            const f32 desc = static_cast<f32>(dpx) * scale;
            if (desc > out.descent) {
                out.descent = desc;
            }
        }
        if (is_thai_combining(cp) && prev_consonant != 0) {
            // Rides its consonant: adds no width, so it fits whenever the
            // consonant did. Keep them together or the cut orphans the mark.
            keep = b1;
            continue;
        }
        const f32 adv = static_cast<f32>(g->advance) * scale;
        if (fit && acc + adv > budget) {
            break; // `keep` stays at the last character that fitted
        }
        acc += adv;
        keep = b1;
        prev_consonant = is_thai_consonant(cp) ? cp : 0;
    }
    out.advance = acc;
    if (fit) {
        out.keep = keep;
    }
    return out;
}

struct TextRenderer {
    BitmapFont *bitmap_font = nullptr;
    SdfFont    *sdf_font    = nullptr;

    void        set_bitmap_font(BitmapFont *font) noexcept { bitmap_font = font; }
    void        set_sdf_font(SdfFont *font) noexcept { sdf_font = font; }

    f32       render_bitmap(SpriteBatch *batch, const char *text, u32 len, f32 x, f32 y, u32 color, u8 layer) noexcept {
        if (!bitmap_font) {
            return x;
        }
        Utf8Decoder dec(text, len);
        f32       cursor_x = x;

        for (;;) {
            u32 cp = dec.next();
            if (cp == 0) {
                break;
            }

            if (cp == '\n') {
                cursor_x  = x;
                y        += bitmap_font->line_height;
                continue;
            }

            if (cp >= MAX_GLYPHS_EN) {
                const GlyphInfo *g = bitmap_font->get_glyph(cp);
                if (!g) {
                    continue;
                }
                f32 gx = cursor_x + g->bearing_x;
                f32 gy = y + g->bearing_y;
                batch->add(gx, gy, static_cast<f32>(g->w), static_cast<f32>(g->h), 0.0f, color, layer);
                cursor_x += g->advance;
                continue;
            }
            const GlyphInfo *g = bitmap_font->get_glyph(static_cast<u8>(cp));
            if (!g) {
                continue;
            }

            f32 gx = cursor_x + g->bearing_x;
            f32 gy = y + g->bearing_y;
            batch->add(gx, gy, static_cast<f32>(g->w), static_cast<f32>(g->h), 0.0f, color, layer);
            cursor_x += g->advance;
        }
        return cursor_x;
    }

    f32 render_sdf(SpriteBatch *batch, const char *text, u32 len, f32 x, f32 y, f32 scale, u32 color, u8 layer) noexcept {
        if (!sdf_font) {
            return x;
        }
        Utf8Decoder dec(text, len);
        f32       cursor_x         = x;
        u32    prev_consonant   = 0;
        f32       prev_consonant_x = 0;

        for (;;) {
            u32 cp = dec.next();
            if (cp == 0) {
                break;
            }

            if (cp == '\n') {
                cursor_x        = x;
                y              += static_cast<f32>(sdf_font->line_height) * scale;
                prev_consonant  = 0;
                continue;
            }

            const GlyphInfo *g = sdf_font->get_glyph(cp);
            if (!g) {
                continue;
            }

            // Front vowels (เ แ โ ใ ไ) - draw and advance
            if (is_thai_vowel_front(cp)) {
                f32 gx = cursor_x + static_cast<f32>(g->bearing_x);
                f32 gy = y + static_cast<f32>(g->bearing_y);
                batch->add(gx, gy, static_cast<f32>(g->w) * scale, static_cast<f32>(g->h) * scale, 0.0f, color, layer);
                cursor_x += static_cast<f32>(g->advance) * scale;
                continue;
            }

            // Combining characters (vowels above/below, tone marks)
            if (is_thai_combining(cp) && prev_consonant != 0) {
                f32 gx = prev_consonant_x + static_cast<f32>(g->bearing_x);
                f32 gy = y + static_cast<f32>(g->bearing_y);

                if (is_thai_vowel_above(cp)) {
                    gy -= static_cast<f32>(sdf_font->line_height) * 0.4f * scale;
                } else if (is_thai_vowel_below(cp)) {
                    gy += static_cast<f32>(sdf_font->line_height) * 0.3f * scale;
                } else if (is_thai_tone_mark(cp)) {
                    gy -= static_cast<f32>(sdf_font->line_height) * 0.35f * scale;
                }

                batch->add(gx, gy, static_cast<f32>(g->w) * scale, static_cast<f32>(g->h) * scale, 0.0f, color, layer);
                continue;
            }

            // Base character (consonant or other)
            f32 gx = cursor_x + static_cast<f32>(g->bearing_x);
            f32 gy = y + static_cast<f32>(g->bearing_y);
            batch->add(gx, gy, static_cast<f32>(g->w) * scale, static_cast<f32>(g->h) * scale, 0.0f, color, layer);

            // Store for potential combining marks
            if (is_thai_consonant(cp)) {
                prev_consonant   = cp;
                prev_consonant_x = cursor_x;
            } else {
                prev_consonant = 0;
            }

            cursor_x += static_cast<f32>(g->advance) * scale;
        }
        return cursor_x;
    }
};

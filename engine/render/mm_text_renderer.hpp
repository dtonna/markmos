#pragma once
#include "../core/mm_handle.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

static constexpr uint16_t MAX_GLYPHS_EN = 256;
static constexpr uint16_t MAX_GLYPHS_TH = 128;
static constexpr uint16_t MAX_GLYPH_BUF = 1024;
static constexpr uint8_t  SDF_RANGE     = 4;

struct GlyphInfo {
    uint16_t u, v;
    uint16_t w, h;
    int8_t   bearing_x;
    int8_t   bearing_y;
    uint8_t  advance;
};

struct BitmapFont {
    TextureHandle atlas;
    GlyphInfo     glyphs[MAX_GLYPHS_EN];
    GlyphInfo     glyphs_th[MAX_GLYPHS_TH];
    uint8_t       line_height;
    uint8_t       base_size;
    uint16_t      atlas_w, atlas_h;

    void          init(TextureHandle tex, uint8_t size) noexcept {
        atlas       = tex;
        base_size   = size;
        line_height = static_cast<uint8_t>(size * 1.2f);
        memset(glyphs, 0, sizeof(glyphs));
        memset(glyphs_th, 0, sizeof(glyphs_th));
    }

    const GlyphInfo *get_glyph(uint32_t codepoint) const noexcept {
        if (codepoint < MAX_GLYPHS_EN) {
            return &glyphs[codepoint];
        }
        if (codepoint >= 0x0E01 && codepoint < 0x0E01 + MAX_GLYPHS_TH) {
            return &glyphs_th[codepoint - 0x0E01];
        }
        return nullptr;
    }
};

struct SdfFont {
    TextureHandle atlas;
    GlyphInfo     glyphs[MAX_GLYPHS_EN + MAX_GLYPHS_TH];
    uint16_t      atlas_w, atlas_h;
    uint8_t       line_height;
    float         sdf_range;

    void          init(TextureHandle tex) noexcept {
        atlas       = tex;
        sdf_range   = SDF_RANGE;
        line_height = 24;
        memset(glyphs, 0, sizeof(glyphs));
    }

    const GlyphInfo *get_glyph(uint32_t codepoint) const noexcept {
        if (codepoint < MAX_GLYPHS_EN) {
            return &glyphs[codepoint];
        }
        if (codepoint >= 0x0E01 && codepoint <= 0x0E7F) {
            uint32_t idx = MAX_GLYPHS_EN + (codepoint - 0x0E01);
            if (idx < MAX_GLYPHS_EN + MAX_GLYPHS_TH) {
                return &glyphs[idx];
            }
        }
        return nullptr;
    }
};

struct Utf8Decoder {
    const uint8_t *input;
    uint32_t       pos;
    uint32_t       len;

    Utf8Decoder(const char *str, uint32_t length) noexcept : input(reinterpret_cast<const uint8_t *>(str)), pos(0), len(length) {}

    uint32_t next() noexcept {
        if (pos >= len) {
            return 0;
        }
        uint8_t b = input[pos++];
        if (b < 0x80) {
            return b;
        }
        if (b < 0xE0 && pos < len) {
            return (static_cast<uint32_t>(b & 0x1F) << 6) | (input[pos++] & 0x3F);
        }
        if (b < 0xF0 && pos + 1 < len) {
            uint32_t cp =
                (static_cast<uint32_t>(b & 0x0F) << 12) | (static_cast<uint32_t>(input[pos] & 0x3F) << 6) | static_cast<uint32_t>(input[pos + 1] & 0x3F);
            pos += 2;
            return cp;
        }
        if (pos + 2 < len) {
            uint32_t cp = (static_cast<uint32_t>(b & 0x07) << 18) | (static_cast<uint32_t>(input[pos] & 0x3F) << 12) |
                          (static_cast<uint32_t>(input[pos + 1] & 0x3F) << 6) | static_cast<uint32_t>(input[pos + 2] & 0x3F);
            pos += 3;
            return cp;
        }
        return 0xFFFD;
    }
};

// Helper functions for Thai character detection
inline bool is_thai_consonant(uint32_t cp) noexcept {
    return (cp >= 0x0E01 && cp <= 0x0E2E); // ก-ฮ
}

inline bool is_thai_vowel_above(uint32_t cp) noexcept { return (cp == 0x0E31) || (cp >= 0x0E34 && cp <= 0x0E37); }

inline bool is_thai_vowel_below(uint32_t cp) noexcept { return (cp >= 0x0E38 && cp <= 0x0E39); }

inline bool is_thai_vowel_front(uint32_t cp) noexcept { return (cp >= 0x0E40 && cp <= 0x0E44); }

inline bool is_thai_tone_mark(uint32_t cp) noexcept { return (cp >= 0x0E48 && cp <= 0x0E4B); }

inline bool is_thai_combining(uint32_t cp) noexcept { return is_thai_vowel_above(cp) || is_thai_vowel_below(cp) || is_thai_tone_mark(cp); }

struct TextRenderer {
    BitmapFont *bitmap_font = nullptr;
    SdfFont    *sdf_font    = nullptr;

    void        set_bitmap_font(BitmapFont *font) noexcept { bitmap_font = font; }
    void        set_sdf_font(SdfFont *font) noexcept { sdf_font = font; }

    float       render_bitmap(SpriteBatch *batch, const char *text, uint32_t len, float x, float y, uint32_t color, uint8_t layer) noexcept {
        if (!bitmap_font) {
            return x;
        }
        Utf8Decoder dec(text, len);
        float       cursor_x = x;

        for (;;) {
            uint32_t cp = dec.next();
            if (cp == 0) {
                break;
            }

            if (cp == '\n') {
                cursor_x  = x;
                y        += bitmap_font->line_height;
                continue;
            }

            if (cp >= MAX_GLYPHS_EN) {
                continue;
            }
            const GlyphInfo *g = bitmap_font->get_glyph(static_cast<uint8_t>(cp));
            if (!g) {
                continue;
            }

            float gx = cursor_x + g->bearing_x;
            float gy = y + g->bearing_y;
            batch->add(gx, gy, static_cast<float>(g->w), static_cast<float>(g->h), 0.0f, color, layer);
            cursor_x += g->advance;
        }
        return cursor_x;
    }

    float render_sdf(SpriteBatch *batch, const char *text, uint32_t len, float x, float y, float scale, uint32_t color, uint8_t layer) noexcept {
        if (!sdf_font) {
            return x;
        }
        Utf8Decoder dec(text, len);
        float       cursor_x         = x;
        uint32_t    prev_consonant   = 0;
        float       prev_consonant_x = 0;

        for (;;) {
            uint32_t cp = dec.next();
            if (cp == 0) {
                break;
            }

            if (cp == '\n') {
                cursor_x        = x;
                y              += static_cast<float>(sdf_font->line_height) * scale;
                prev_consonant  = 0;
                continue;
            }

            const GlyphInfo *g = sdf_font->get_glyph(cp);
            if (!g) {
                continue;
            }

            // Front vowels (เ แ โ ใ ไ) - draw and advance
            if (is_thai_vowel_front(cp)) {
                float gx = cursor_x + static_cast<float>(g->bearing_x);
                float gy = y + static_cast<float>(g->bearing_y);
                batch->add(gx, gy, static_cast<float>(g->w) * scale, static_cast<float>(g->h) * scale, 0.0f, color, layer);
                cursor_x += static_cast<float>(g->advance) * scale;
                continue;
            }

            // Combining characters (vowels above/below, tone marks)
            if (is_thai_combining(cp) && prev_consonant != 0) {
                float gx = prev_consonant_x + static_cast<float>(g->bearing_x);
                float gy = y + static_cast<float>(g->bearing_y);

                if (is_thai_vowel_above(cp)) {
                    gy -= static_cast<float>(sdf_font->line_height) * 0.4f * scale;
                } else if (is_thai_vowel_below(cp)) {
                    gy += static_cast<float>(sdf_font->line_height) * 0.3f * scale;
                } else if (is_thai_tone_mark(cp)) {
                    gy -= static_cast<float>(sdf_font->line_height) * 0.35f * scale;
                }

                batch->add(gx, gy, static_cast<float>(g->w) * scale, static_cast<float>(g->h) * scale, 0.0f, color, layer);
                continue;
            }

            // Base character (consonant or other)
            float gx = cursor_x + static_cast<float>(g->bearing_x);
            float gy = y + static_cast<float>(g->bearing_y);
            batch->add(gx, gy, static_cast<float>(g->w) * scale, static_cast<float>(g->h) * scale, 0.0f, color, layer);

            // Store for potential combining marks
            if (is_thai_consonant(cp)) {
                prev_consonant   = cp;
                prev_consonant_x = cursor_x;
            } else {
                prev_consonant = 0;
            }

            cursor_x += static_cast<float>(g->advance) * scale;
        }
        return cursor_x;
    }
};

#include "mm_font_atlas.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <cstring>

void FontAtlas::bake(const unsigned char* ttf_data, int /*ttf_size*/, float pixel_height) noexcept {
    memset(pixels, 0, sizeof(pixels));
    stbtt_bakedchar cd[FONT_NUM_CHARS];

    int result = stbtt_BakeFontBitmap(ttf_data, 0, pixel_height,
                                       pixels, FONT_ATLAS_W, FONT_ATLAS_H,
                                       FONT_FIRST_CHAR, FONT_NUM_CHARS, cd);
    (void)result;

    inv_atlas_w = 1.0f / static_cast<float>(FONT_ATLAS_W);
    inv_atlas_h = 1.0f / static_cast<float>(FONT_ATLAS_H);
    line_height = static_cast<uint8_t>(pixel_height * 1.2f);

    for (int i = 0; i < FONT_NUM_CHARS; ++i) {
        baked_chars[i * 4 + 0] = cd[i].x0;
        baked_chars[i * 4 + 1] = cd[i].y0;
        baked_chars[i * 4 + 2] = cd[i].x1;
        baked_chars[i * 4 + 3] = cd[i].y1;
        xoff[i]    = cd[i].xoff;
        yoff[i]    = cd[i].yoff;
        xadvance[i] = cd[i].xadvance;
        w[i] = static_cast<uint16_t>(cd[i].x1 - cd[i].x0);
        h[i] = static_cast<uint16_t>(cd[i].y1 - cd[i].y0);
    }
}

int FontAtlas::bake_range(const unsigned char* ttf_data, float pixel_height,
                          int first_char, int count, int start_y,
                          stbtt_bakedchar* chardata_out) const noexcept {
    unsigned char* sub_pixels = const_cast<unsigned char*>(pixels) + start_y * FONT_ATLAS_W;
    int sub_ph = FONT_ATLAS_H - start_y;
    int result = stbtt_BakeFontBitmap(ttf_data, 0, pixel_height,
                                       sub_pixels, FONT_ATLAS_W, sub_ph,
                                       first_char, count, chardata_out);
    for (int i = 0; i < count; ++i) {
        chardata_out[i].y0 = static_cast<unsigned short>(chardata_out[i].y0 + start_y);
        chardata_out[i].y1 = static_cast<unsigned short>(chardata_out[i].y1 + start_y);
    }
    return (result > 0) ? result + start_y : result;
}

void FontAtlas::get_quad(uint8_t c, float& x, float& y,
                         float& u0, float& v0, float& u1, float& v1,
                         float& gw, float& gh) const noexcept {
    if (c < FONT_FIRST_CHAR || c >= FONT_FIRST_CHAR + FONT_NUM_CHARS) {
        gw = 0; gh = 0;
        u0 = v0 = u1 = v1 = 0;
        return;
    }
    int idx = c - FONT_FIRST_CHAR;
    u0 = baked_chars[idx * 4 + 0] * inv_atlas_w;
    v0 = baked_chars[idx * 4 + 1] * inv_atlas_h;
    u1 = baked_chars[idx * 4 + 2] * inv_atlas_w;
    v1 = baked_chars[idx * 4 + 3] * inv_atlas_h;
    gw = w[idx];
    gh = h[idx];
    x = xoff[idx];
    y = yoff[idx];
}

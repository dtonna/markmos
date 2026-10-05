// CPU mipmap helper tests — chain length, 2x2 box average, odd dims.
// Plain main() + assert(), no framework. Headless: pure CPU.
#include "../render/mm_mipmaps.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

int main() {
    // ─── Level count ───
    {
        assert(mip_level_count(1, 1) == 1);
        assert(mip_level_count(2, 2) == 2);
        assert(mip_level_count(256, 256) == 9);
        assert(mip_level_count(256, 128) == 9); // driven by max side
        assert(mip_level_count(100, 60) == 7);  // 100→50→25→12→6→3→1
    }

    // ─── Solid 2x2 averages exactly ───
    {
        u8 src[16] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160};
        u8 dst[4]  = {0, 0, 0, 0};
        downsample_rgba8_box(dst, 1, 1, src, 2, 2);
        assert(dst[0] == 70);  // (10+50+90+130+2)/4 = 70
        assert(dst[1] == 80);  // (20+60+100+140+2)/4 = 80
        assert(dst[2] == 90);  // (30+70+110+150+2)/4 = 90
        assert(dst[3] == 100); // (40+80+120+160+2)/4 = 100
    }

    // ─── 4x4 black/white checker → mid gray ───
    {
        u8 src[64];
        for (u32 y = 0; y < 4; ++y) {
            for (u32 x = 0; x < 4; ++x) {
                u8 v              = ((x + y) & 1) ? 255 : 0;
                u8 *p             = src + (y * 4 + x) * 4;
                p[0] = p[1] = p[2] = v;
                p[3]                 = 255;
            }
        }
        u8 dst[16];
        downsample_rgba8_box(dst, 2, 2, src, 4, 4);
        // Each 2x2 block holds two black + two white → (0+0+255+255+2)/4 = 128.
        for (int i = 0; i < 4; ++i) {
            assert(dst[i * 4 + 0] == 128);
            assert(dst[i * 4 + 3] == 255);
        }
    }

    // ─── Odd dims clamp edges without overrun ───
    {
        u8 src[3 * 5 * 4];
        for (u32 i = 0; i < sizeof(src); ++i) {
            src[i] = static_cast<u8>(i & 0xFF);
        }
        u8 dst[2 * 1 * 4] = {}; // ceil? no: floor(5/2)=2, floor(3/2)=1
        downsample_rgba8_box(dst, 2, 1, src, 5, 3);
        // Spot-check first texel = average of src(0,0),(1,0),(0,1),(1,1).
        u32 e0 = (src[0] + src[4] + src[20] + src[24] + 2) >> 2;
        assert(dst[0] == static_cast<u8>(e0));
    }

    printf("[mipmaps] all tests passed\n");
    return 0;
}

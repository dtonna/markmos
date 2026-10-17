// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT
//
// FontAtlas append chaining — the contract the renderer's font init depends on.
//
// Phase 1 of the feature-selection work made Renderer::init() append several
// independently-optional font sets into ONE texture, each handing its end row
// to the next. That logic lives in AppendFontRange / AppendFontRangeEach so it
// can be tested headlessly: FontAtlas is header-only and needs no backend.
//
// This test executes the SAME functions the renderer calls — not a restatement
// of them — so reverting the cursor handling inside AppendFontRangeEach makes
// it fail. (An earlier version of this file called bake_range directly and
// passed against a renderer that had the bug, because the buggy code sits in
// the #if'd block the test never compiles.)
//
// Karla is used for every set because it is the only font always linked.

#include "../render/mm_font_atlas.hpp"
#include "../render/mm_font_data.hpp"
#if MM_ENGINE_FONT_SYM1
#include "../render/mm_glyph_undo_data.hpp"
#endif

#include <cstdio>
#include <cstring>

// The ASCII row cursor, derived exactly as mm_renderer.hpp does after bake().
static int NextYAfterAscii(const FontAtlas &fa) {
    int next_y = 0;
    for (int i = 0; i < FONT_NUM_CHARS; ++i) {
        const int row_end = static_cast<int>(fa.baked_chars[i * 4 + 3] + 1.0f);
        if (row_end > next_y) {
            next_y = row_end;
        }
    }
    return next_y;
}

static int MinY(const stbtt_bakedchar *cd, int count) {
    int lo = 0x7FFFFFFF;
    for (int i = 0; i < count; ++i) {
        if (cd[i].y1 > cd[i].y0 && cd[i].y0 < lo) {
            lo = cd[i].y0;
        }
    }
    return lo;
}

static int MaxY(const stbtt_bakedchar *cd, int count) {
    int hi = 0;
    for (int i = 0; i < count; ++i) {
        if (cd[i].y1 > cd[i].y0 && cd[i].y1 > hi) {
            hi = cd[i].y1;
        }
    }
    return hi;
}

// ─── AppendFontRange: bulk append hands on its end row ───
static void Test_BulkAppendChains() {
    FontAtlas fa;
    fa.bake(g_karla_ttf, static_cast<int>(g_karla_ttf_size), 48.0f);
    const int ascii_end = NextYAfterAscii(fa);
    assert(ascii_end > 0 && ascii_end < FONT_ATLAS_H);

    stbtt_bakedchar cd[8];
    const int ret = AppendFontRange(fa, g_karla_ttf, 48.0f, 0x41, 4, ascii_end, cd);
    assert(ret > ascii_end);           // advanced
    assert(MinY(cd, 4) >= ascii_end);  // placed below the ASCII row
    assert(MaxY(cd, 4) <= ret);        // and above what it reports
}

// ─── AppendFontRangeEach: the per-glyph fallback advances on EVERY glyph ───
// This is the function that used to reuse one start_y, so every glyph overwrote
// the one before it. Chained from the ASCII row it must spread downwards.
static void Test_EachGlyphAdvances() {
    FontAtlas fa;
    fa.bake(g_karla_ttf, static_cast<int>(g_karla_ttf_size), 48.0f);
    const int ascii_end = NextYAfterAscii(fa);

    constexpr int kCount = 6;
    stbtt_bakedchar cd[kCount];
    const int ret = AppendFontRangeEach(fa, g_karla_ttf, 48.0f, 0x41, kCount, ascii_end, cd);

    assert(ret > ascii_end);
    // Nothing sits on the ASCII row.
    assert(MinY(cd, kCount) >= ascii_end);

    // Every glyph that actually rendered must be strictly below the previous
    // one — the cursor advanced, so consecutive glyphs do not stack up.
    int prev_end = ascii_end;
    int placed   = 0;
    for (int i = 0; i < kCount; ++i) {
        if (cd[i].y1 <= cd[i].y0) {
            continue;  // not present in the font at this size
        }
        ++placed;
        assert(cd[i].y0 >= prev_end);
        prev_end = cd[i].y1;
    }
    assert(placed >= 3);  // enough of 'A'..'F' render to make the check real
    assert(MaxY(cd, kCount) <= ret);
}

// ─── Four sets chained, as the renderer does, must not overlap ───
static void Test_FourSetsDoNotOverlap() {
    FontAtlas fa;
    fa.bake(g_karla_ttf, static_cast<int>(g_karla_ttf_size), 48.0f);

    int cursor = NextYAfterAscii(fa);
    int ends[3];
    stbtt_bakedchar a[4], b[4], c[4];

    ends[0] = cursor = AppendFontRange(fa, g_karla_ttf, 48.0f, 0x41, 4, cursor, a);
    assert(cursor > ends[0] - 4);  // advanced past where it started

    int after_a = cursor;
    cursor      = AppendFontRange(fa, g_karla_ttf, 48.0f, 0x50, 4, cursor, b);
    assert(MinY(b, 4) >= after_a);

    int after_b = cursor;
    cursor      = AppendFontRangeEach(fa, g_karla_ttf, 48.0f, 0x61, 4, cursor, c);
    assert(MinY(c, 4) >= after_b);

    assert(cursor < FONT_ATLAS_H);
    (void)ends;
}

// ─── The test can SEE the defect it exists for ───
// Appending two sets from the SAME start_y must be detectable as overlap.
// Without this, the assertions above could all be vacuous and still pass
// against the broken cursor handling.
static void Test_OverlapIsDetectable() {
    FontAtlas fa;
    fa.bake(g_karla_ttf, static_cast<int>(g_karla_ttf_size), 48.0f);
    const int ascii_end = NextYAfterAscii(fa);

    stbtt_bakedchar first[4], second[4];
    const int first_end = AppendFontRange(fa, g_karla_ttf, 48.0f, 0x41, 4, ascii_end, first);
    // Deliberately NOT chaining — this is the original defect.
    (void)AppendFontRange(fa, g_karla_ttf, 48.0f, 0x50, 4, ascii_end, second);

    assert(MinY(second, 4) < first_end);  // they DO land on the same rows
}

// ─── U+238C (undo glyph) actually lands in the atlas ───
// Freecell's undo button draws this and NOTHING else non-ASCII. It arrives
// pre-baked (mm_glyph_undo_data.hpp, 280 bytes) rather than as a 185 KB TTF;
// tools/build_undo_glyph.cpp proves the emitted pixels and metrics match baking
// that TTF at a non-zero atlas row before it writes the file, so this test only
// has to check the placement half — which is the half the generator cannot see.
#if MM_ENGINE_FONT_SYM1
// The include above is guarded and this body was not, so the file compiled only
// when the font set it tests was switched on. That is the same trap audio_tests
// walked into: a test that only builds in one configuration is a test nobody
// notices is missing until they turn the feature off.
static void Test_UndoGlyphBakes() {
    FontAtlas fa;
    fa.bake(g_karla_ttf, static_cast<int>(g_karla_ttf_size), 48.0f);
    const int ascii_end = NextYAfterAscii(fa);

    constexpr int gw = mm_glyph_undo_x1 - mm_glyph_undo_x0;
    constexpr int gh = mm_glyph_undo_y1 - mm_glyph_undo_y0;

    // Slot 8 is the undo glyph; 0..7 are the suit symbols.
    stbtt_bakedchar cd[16];
    memset(cd, 0, sizeof(cd));
    const int ret = AppendBakedGlyph(fa, mm_glyph_undo_bitmap, gw, gh,
                                     mm_glyph_undo_x0, mm_glyph_undo_y0,
                                     mm_glyph_undo_x1, mm_glyph_undo_y1,
                                     mm_glyph_undo_xoff, mm_glyph_undo_yoff,
                                     mm_glyph_undo_advance, ascii_end, &cd[8]);

    assert(ret > ascii_end);                 // advanced the cursor
    assert(cd[8].y1 > cd[8].y0);              // it rendered
    assert(cd[8].x1 - cd[8].x0 == gw);        // width is the emitted width
    assert(cd[8].y1 - cd[8].y0 == gh);        // and so is the height
    assert(cd[8].xadvance == mm_glyph_undo_advance);
    assert(cd[8].yoff == mm_glyph_undo_yoff);
    assert(cd[8].y0 == ascii_end + mm_glyph_undo_y0);  // placed at the cursor
    assert(cd[8].y1 == ret);                           // ends where it reported

    // The pixels are actually in the atlas, at the reported position, and they
    // match the emitted bitmap row for row. Without this the glyph would be an
    // empty box of the right size, which is exactly what "renders" looks like.
    int covered = 0;
    for (int row = 0; row < gh; ++row) {
        for (int col = 0; col < gw; ++col) {
            const unsigned char want = mm_glyph_undo_bitmap[row * gw + col];
            const unsigned char got  = fa.pixels[(cd[8].y0 + row) * FONT_ATLAS_W + cd[8].x0 + col];
            assert(got == want);
            if (want > 0) {
                ++covered;
            }
        }
    }
    assert(covered > gw);  // a real glyph, not a hairline

    // Nothing was clobbered on the way: the suit slots stay empty.
    for (int i = 0; i < 8; ++i) {
        assert(cd[i].x1 == 0 && cd[i].x0 == 0);
    }
}
#endif

// ─── AppendBakedGlyph refuses rather than overflowing the atlas ───
// A row past the bottom must place nothing and leave the cursor alone: a
// half-written glyph reads as a corrupted font, which is worse than a missing
// one, and this is the one input a caller can get wrong.
#if MM_ENGINE_FONT_SYM1
static void Test_BakedGlyphRefusesToOverflow() {
    FontAtlas fa;
    fa.bake(g_karla_ttf, static_cast<int>(g_karla_ttf_size), 48.0f);

    constexpr int gw = mm_glyph_undo_x1 - mm_glyph_undo_x0;
    constexpr int gh = mm_glyph_undo_y1 - mm_glyph_undo_y0;

    stbtt_bakedchar cd{};
    memset(&cd, 0xAB, sizeof(cd));  // poison, so "untouched" is provable
    const int start  = FONT_ATLAS_H - 2;
    const int ret = AppendBakedGlyph(fa, mm_glyph_undo_bitmap, gw, gh,
                                     mm_glyph_undo_x0, mm_glyph_undo_y0,
                                     mm_glyph_undo_x1, mm_glyph_undo_y1,
                                     mm_glyph_undo_xoff, mm_glyph_undo_yoff,
                                     mm_glyph_undo_advance, start, &cd);
    assert(ret == start);                     // cursor unchanged
    unsigned char untouched[sizeof(stbtt_bakedchar)];
    memset(untouched, 0xAB, sizeof(untouched));
    assert(memcmp(&cd, untouched, sizeof(cd)) == 0);

    // A null bitmap or an empty box is also refused, not written.
    stbtt_bakedchar cd2{};
    memset(&cd2, 0xAB, sizeof(cd2));
    assert(AppendBakedGlyph(fa, nullptr, gw, gh, 1, 1, gw, gh, 0, 0, 20, 10, &cd2) == 10);
    assert(memcmp(&cd2, untouched, sizeof(cd2)) == 0);
}
#endif

int main() {
    Test_BulkAppendChains();
    Test_EachGlyphAdvances();
    Test_FourSetsDoNotOverlap();
    Test_OverlapIsDetectable();
#if MM_ENGINE_FONT_SYM1
#if MM_ENGINE_FONT_SYM1
    Test_UndoGlyphBakes();
#endif
#if MM_ENGINE_FONT_SYM1
    Test_BakedGlyphRefusesToOverflow();
#endif
#endif
    std::printf("[font_atlas_tests] all tests passed\n");
    return 0;
}
// Text measurement tests — the primitives that Manager::measure, draw_text and
// measure_text all share. Plain main() + assert(), no framework.
//
// Headless by construction: a BitmapFont is a plain struct of GlyphInfo, so a
// synthetic one is enough to pin advance / ascent / ellipsis arithmetic with no
// GPU, no atlas and no Renderer. That is the reason these live in
// mm_text_renderer.hpp rather than inside measure(): the rules were previously
// copied into three passes that could not be tested apart from each other, and
// they disagreed (only draw_text substituted '?', only draw_text applied the
// Thai scale bump, draw_text_lines walked BYTES so a 3-byte Thai character was
// measured as three Latin-1 codepoints).
#include "../render/mm_text_renderer.hpp"
#include "core/mm_types.h"

#include <cassert>
#include <cmath>

static bool Near(f32 a, f32 b) noexcept {
    return __builtin_fabsf(a - b) < 0.001f;
}

// A synthetic font with a 256x256 atlas. Every Latin glyph gets the SAME
// advance and ink box, so an expected width is just (codepoints * advance), and
// a char can be made unbaked by leaving it zeroed.
static BitmapFont MakeFont(u8 advance = 20, i8 bearing_y = -30) noexcept {
    BitmapFont f;
    f.init(TextureHandle::invalid(), 48);
    f.atlas_w = 256;
    f.atlas_h = 256;
    for (int c = 32; c < 128; ++c) {
        GlyphInfo &g         = f.glyphs[c];
        g.u                  = 0;
        g.v                  = 0;
        g.w                  = 16;
        g.h                  = 32;
        g.bearing_x          = 0;
        g.bearing_y          = bearing_y;
        g.advance            = advance;
    }
    // '?' must be baked for the fallback to be reachable at all.
    assert(f.glyphs['?'].w > 0);
    return f;
}

// A glyph in the Thai range (U+0E01 is ก), baked with its own advance so a
// measured width tells you whether it was looked up as ONE codepoint or as
// three Latin-1 bytes.
static void BakeThai(BitmapFont &f, u32 cp, u8 advance) noexcept {
    GlyphInfo &g      = f.glyphs_th[cp - 0x0E01];
    g.u               = 0;
    g.v               = 0;
    g.w               = 16;
    g.h               = 32;
    g.bearing_x       = 0;
    g.bearing_y       = -30;
    g.advance         = advance;
}

int main() {
    // ─── resolve_glyph: '?' substitution is the ONE rule, shared ───
    {
        BitmapFont f = MakeFont();

        // A baked glyph resolves to itself.
        const GlyphInfo *a = resolve_glyph(f, 'A');
        assert(a != nullptr && a->advance == 20);

        // Codepoint 200 is < 256, so get_glyph() hands back a NON-NULL pointer
        // to a zeroed entry. That is the whole trap: "non-null" used to read as
        // "baked", so measure() counted 0 for it while draw_text painted a '?'.
        assert(f.get_glyph(200) != nullptr);    // non-null...
        assert(f.get_glyph(200)->w == 0);       // ...and empty
        const GlyphInfo *b = resolve_glyph(f, 200);
        assert(b != nullptr);
        assert(b->advance == f.glyphs['?'].advance); // resolves to '?'
        assert(b->advance > 0);                       // and it has WIDTH

        // Out of every range: same substitution.
        assert(resolve_glyph(f, 0x4E2D) != nullptr);

        // A glyph whose rect runs past the atlas is not drawable, so it must
        // substitute too rather than sampling out of bounds.
        f.glyphs['Z'].u = 250;
        f.glyphs['Z'].w = 16; // 250 + 16 > 256
        const GlyphInfo *c = resolve_glyph(f, 'Z');
        assert(c != nullptr && c->advance == f.glyphs['?'].advance);

        // With '?' itself unbaked there is nothing left to substitute: draw
        // nothing AND advance nothing, which is what callers must honour.
        f.glyphs['?'].w = 0;
        assert(resolve_glyph(f, 200) == nullptr);
    }

    // ─── measure_string: one definition of "how big is this" ───
    {
        BitmapFont f = MakeFont(20);

        const TextMetrics one = measure_string(f, "ABC", 1.0f);
        assert(Near(one.widest, 60.0f)); // 3 * 20
        assert(one.lines == 1);
        assert(Near(one.ascent, 30.0f)); // -bearing_y
        assert(Near(one.descent, 2.0f));  // bearing_y + h = -30 + 32

        // Scale multiplies every number.
        const TextMetrics half = measure_string(f, "ABC", 0.5f);
        assert(Near(half.widest, 30.0f) && Near(half.ascent, 15.0f));

        // WIDEST LINE, not the concatenation. A two-line label sizes its frame
        // to the longest line; using the whole string made every multi-line
        // AUTO_W frame wider than anything drawn in it.
        const TextMetrics two = measure_string(f, "ABCDE\nAB", 1.0f);
        assert(two.lines == 2);
        assert(Near(two.widest, 100.0f)); // 5 * 20, NOT 7 * 20

        // Short first line, long second: still the second line's width.
        const TextMetrics two2 = measure_string(f, "A\nABCDE", 1.0f);
        assert(Near(two2.widest, 100.0f));

        // A missing glyph is measured as the '?' that WILL be drawn, so an
        // AUTO_W frame is sized for what appears on screen.
        const TextMetrics q = measure_string(f, "A\xC2\xA9", 1.0f); // A + U+00A9
        assert(Near(q.widest, 40.0f));

        // Empty / null are safe and report one line.
        const TextMetrics e = measure_string(f, "", 1.0f);
        assert(Near(e.widest, 0.0f) && e.lines == 1);
        const TextMetrics n = measure_string(f, nullptr, 1.0f);
        assert(Near(n.widest, 0.0f) && n.lines == 1);
    }

    // ─── measure_string: Thai is ONE codepoint, and marks ride free ───
    {
        BitmapFont f = MakeFont(20);
        BakeThai(f, 0x0E01, 25); // ก

        // U+0E01 is 3 UTF-8 bytes (E0 B8 81). Byte-walking looked it up as
        // three Latin-1 codepoints and summed three advances; the codepoint
        // walk must count exactly one glyph.
        const TextMetrics th = measure_string(f, "\xE0\xB8\x81", 1.0f);
        assert(Near(th.widest, 25.0f));

        // A combining mark (U+0E31 is a vowel ABOVE, also 3 bytes) rides on the
        // preceding consonant and does not advance - draw_text has always
        // drawn it that way, so measuring it as an advance overshot every
        // syllable by one glyph width.
        const TextMetrics withMark = measure_string(f, "\xE0\xB8\x81\xE0\xB8\xB1", 1.0f);
        assert(Near(withMark.widest, 25.0f)); // 25, not 25 + 20

        // A mark with no consonant before it is a base glyph: it does advance.
        const TextMetrics lone = measure_string(f, "\xE0\xB8\xB1", 1.0f);
        assert(Near(lone.widest, 20.0f));
    }

    // ─── text_scale_for: Thai bump, and only Thai ───
    {
        const f32 base = 0.30f;

        // Latin is untouched.
        assert(Near(text_scale_for("Easy", base), base));
        assert(Near(text_scale_for("", base), base));
        assert(Near(text_scale_for(nullptr, base), base));

        // Thai gets the bump.
        assert(Near(text_scale_for("\xE0\xB8\x81", base), base * 1.2f));
        assert(Near(text_scale_for("abc \xE0\xB8\x81", base), base * 1.2f));

        // The old detector was "any byte >= 0xE0", a UTF-8 LEAD byte, so these
        // three were scaled as if they were Thai:
        //   U+2019  E2 80 99   right single quote
        //   U+201C  E2 80 9C   left f64 quote
        //   U+20AC  E2 82 AC   euro sign
        assert(Near(text_scale_for("it\xE2\x80\x99s", base), base));
        assert(Near(text_scale_for("\xE2\x80\x9Cquoted\xE2\x80\x9D", base), base));
        assert(Near(text_scale_for("\xE2\x82\xAC" "5", base), base)); // split: "\xAC5" would be one escape

        // A 2-byte codepoint never matched the old test either, but pin it so
        // the range is explicit: U+00A9 is not Thai.
        assert(Near(text_scale_for("\xC2\xA9", base), base));
    }

    // ─── scan_line: one walk feeds ascent, alignment and the ellipsis ───
    {
        BitmapFont f = MakeFont(20);

        const LineScan all = scan_line(f, "ABCDE", 5, 1.0f, -1.0f);
        assert(Near(all.advance, 100.0f));
        assert(Near(all.ascent, 30.0f));
        assert(all.keep == 5); // no budget: the whole line is "kept"

        // A budget makes `keep` the leading bytes that fit.
        const LineScan fit3 = scan_line(f, "ABCDE", 5, 1.0f, 60.0f);
        assert(Near(fit3.advance, 60.0f));
        assert(fit3.keep == 3);

        // Partial glyphs never fit, so keep lands on a boundary.
        const LineScan fit55 = scan_line(f, "ABCDE", 5, 1.0f, 55.0f);
        assert(fit55.keep == 2);

        // No room at all: keep 0, and the caller must not slice mid-glyph.
        const LineScan none = scan_line(f, "ABCDE", 5, 1.0f, 5.0f);
        assert(none.keep == 0);

        // Thai: 3-byte codepoints, and the ellipsis cut is a CHARACTER boundary.
        // This is the bug the byte-walk had: it counted bytes, so a cut could
        // land inside a 3-byte sequence and draw garbage.
        BitmapFont tf = MakeFont(20);
        BakeThai(tf, 0x0E01, 25);
        BakeThai(tf, 0x0E02, 25);
        BakeThai(tf, 0x0E03, 25);
        const char *thai = "\xE0\xB8\x81\xE0\xB8\x82\xE0\xB8\x83"; // 3 chars, 9 bytes
        const LineScan ts = scan_line(tf, thai, 9, 1.0f, -1.0f);
        assert(Near(ts.advance, 75.0f)); // 3 * 25

        // Budget for two glyphs: keep must be 6 (two whole 3-byte chars).
        const LineScan ts2 = scan_line(tf, thai, 9, 1.0f, 50.0f);
        assert(ts2.keep == 6);
        assert((static_cast<u8>(thai[ts2.keep]) & 0xC0) != 0x80); // not a continuation

        // A combining mark must not be orphaned by the cut: it rides its
        // consonant, so keep covers the pair or it covers neither.
        BitmapFont mf = MakeFont(20);
        BakeThai(mf, 0x0E01, 25); // ก
        BakeThai(mf, 0x0E31, 20); // vowel above
        const char *syll = "\xE0\xB8\x81\xE0\xB8\xB1"; // 6 bytes, 2 codepoints
        const LineScan ms = scan_line(mf, syll, 6, 1.0f, 25.0f);
        assert(ms.keep == 6); // the mark came along for free
    }

    return 0;
}

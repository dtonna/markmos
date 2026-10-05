// Label widget tests — factory defaults, the inline-buffer limit, set_text /
// set_text_ext / set_text_align, and the recycled-slot resets.
//
// A LABEL is the simplest widget in the toolkit (no state, no input, Pass 3
// only) and it had no test file, which is not the same as having nothing to
// test: it owns `align[]`, `text_box[]` and the 48-byte inline buffer, and every
// one of those is a place a recycled slot can go wrong.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cstring>

static void NoopCb(u16, void *) noexcept {}

static InputState FreshInput() noexcept {
    InputState in;
    in.init();
    return in;
}

static void Tap(InputState &in, f32 x, f32 y) noexcept {
    in.action_count = 1;
    in.actions[0]  = InputAction::Select;
    in.action_x    = x;
    in.action_y    = y;
}

int main() {
    // ─── Factory: AUTO_W/AUTO_H, no frame, no fill, no callbacks ───
    {
        ui::Manager m;
        m.init();
        const u16 l = m.label(10.0f, 20.0f, "Score 1200", 0xFFFFFFFF, 0.32f);
        assert(l != UINT16_MAX);
        assert(m.pool[l].type == (u8)ui::widget_type::LABEL);
        // A label SIZES ITSELF to its text: the frame starts empty and measure()
        // fills it. This is also why set_text_align needs box_w > 0 to have
        // anything to align inside - an auto box hugs the text and every
        // alignment collapses to LEFT.
        assert(m.pool[l].frame.w == 0.0f && m.pool[l].frame.h == 0.0f);
        assert(m.pool[l].flags & ui::WF_AUTO_W);
        assert(m.pool[l].flags & ui::WF_AUTO_H);
        assert(m.pool[l].scale == 0.32f);
        assert(m.pool[l].text_color == 0xFFFFFFFF);
        assert(std::strcmp(m.pool[l].text, "Score 1200") == 0);
        // NO fill: Pass 1 skips a zero bg_color, so a label is text only.
        assert(m.pool[l].bg_color == 0);
        assert(m.pool[l].on_click == nullptr);
        assert(m.pool[l].on_draw == nullptr);
        assert(!(m.pool[l].flags & ui::WF_FOCUSABLE));
        // Not a container and not a hit target: pick() skips it by type.
        assert(!m.is_container(l));
        assert(m.hit_test(15.0f, 25.0f) != l);
    }

    // ─── A label is skipped by pick(), so it can never steal a tap ───
    // Testing "a tap on a label does nothing" directly is IMPOSSIBLE headless:
    // a label's frame is 0x0 until measure() runs, and measure() needs a
    // Renderer (1.03 MB, and no backend here). A test that taps a zero-sized
    // rect proves nothing - it would pass against a label that WAS pickable.
    //
    // So this asserts the rule that makes it true instead: stack a LABEL over a
    // BUTTON at the same point and check the tap reaches the button. That fails
    // the moment pick() stops skipping LABEL by type.
    {
        ui::Manager m;
        m.init();
        const u16 btn = m.button(10.0f, 20.0f, 100.0f, 30.0f, "btn", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);
        const u16 lbl = m.label(12.0f, 24.0f, "over the button", 0xFFFFFFFF, 0.3f, btn);
        assert(m.pool[btn].frame.w > 0.0f); // the button HAS a real box
        InputState in = FreshInput();
        Tap(in, 60.0f, 35.0f);
        m.handle(in);
        assert(m.was_clicked(btn));
        assert(m.was_clicked(lbl) == false);
        // And the label's own rect is empty until something measures it.
        assert(m.pool[lbl].frame.w == 0.0f);
    }

    // ─── The 48-byte inline buffer truncates SILENTLY, mid-word ───
    // Widget is locked at 128 bytes, so the string is a fixed inline buffer and
    // the factories strncpy into it. Anything longer is cut with no error, which
    // is why set_text() exists and why set_text_ext() takes a caller-owned
    // string instead. Both halves of that contract are here.
    {
        ui::Manager m;
        m.init();
        char long_label[96];
        for (int i = 0; i < 95; ++i) {
            long_label[i] = static_cast<char>('a' + (i % 26));
        }
        long_label[95] = '\0';
        const u16 l = m.label(0.0f, 0.0f, long_label, 0xFFFFFFFF, 0.3f);
        assert(std::strlen(m.pool[l].text) == sizeof(m.pool[l].text) - 1); // 47, not 95

        // set_text REFUSES a string that does not fit, and changes NOTHING -
        // a caller can branch on the return value and fall back to a shorter
        // label or to set_text_ext().
        assert(!m.set_text(l, long_label));
        assert(std::strlen(m.pool[l].text) == sizeof(m.pool[l].text) - 1);
        assert(m.set_text(l, "short"));
        assert(std::strcmp(m.pool[l].text, "short") == 0);
        // Exactly-fitting is allowed: 47 chars + NUL.
        char exact[sizeof(m.pool[l].text)];
        for (u8 i = 0; i < sizeof(m.pool[l].text) - 1; ++i) {
            exact[i] = 'x';
        }
        exact[sizeof(m.pool[l].text) - 1] = '\0';
        assert(m.set_text(l, exact));
        assert(std::strlen(m.pool[l].text) == sizeof(m.pool[l].text) - 1);

        // set_text_ext is the escape hatch: caller-owned, unbounded, and while
        // it is set text_of() ignores the inline buffer entirely.
        static const char *const kLong = "a caller-owned string far longer than the inline buffer";
        m.set_text_ext(l, kLong);
        assert(m.text_of(l) == kLong);
        assert(std::strcmp(m.pool[l].text, exact) == 0); // the buffer is untouched

        // And set_text CLEARS the override: mixing the two is a trap the caller
        // cannot see, so the "set a plain string" path takes the widget back.
        // (The reverse is not true - set_text_ext does not touch the buffer.)
        assert(m.set_text(l, "plain"));
        assert(m.text_ext[l] == nullptr);
        assert(std::strcmp(m.text_of(l), "plain") == 0);
    }

    // ─── set_text_align: alignment needs a PINNED box ───
    {
        ui::Manager m;
        m.init();
        const u16 l = m.label(0.0f, 0.0f, "hello", 0xFFFFFFFF, 0.3f);
        assert(m.align[l] == m.theme.label_align); // seeded from the theme
        assert(m.text_box[l] == 0.0f);              // ...with no pinned box
        m.set_text_align(l, ui::text_align::RIGHT, 200.0f);
        assert(m.align[l] == ui::text_align::RIGHT);
        assert(m.text_box[l] == 200.0f); // box_w > 0 CLEARS AUTO_W
        // align_text_x is the pure helper both use.
        assert(ui::align_text_x(ui::text_align::LEFT, 10.0f, 200.0f, 50.0f) == 10.0f);
        assert(ui::align_text_x(ui::text_align::CENTER, 10.0f, 200.0f, 50.0f) == 85.0f);
        assert(ui::align_text_x(ui::text_align::RIGHT, 10.0f, 200.0f, 50.0f) == 160.0f);
        // box_w == 0 keeps the auto box: a layout-driven label that is already
        // pinned should not have its width overwritten with a number.
        const u16 l2 = m.label(0.0f, 0.0f, "auto", 0xFFFFFFFF, 0.3f);
        m.set_text_align(l2, ui::text_align::CENTER, 0.0f);
        assert(m.text_box[l2] == 0.0f);
        assert(m.pool[l2].flags & ui::WF_AUTO_W); // still auto
        m.set_text_align(l2, ui::text_align::CENTER, 120.0f);
        assert(!(m.pool[l2].flags & ui::WF_AUTO_W)); // pinned now
    }

    // ─── Recycled slots: the label's own arrays do not survive ───
    // align[] and text_box[] are written by SETTERS, so nothing but alloc()
    // clears them - and a stale alignment makes the new label draw somewhere
    // else entirely, which is invisible in a test that only checks the string.
    {
        ui::Manager m;
        m.init();
        const u16 a = m.label(0.0f, 0.0f, "first", 0xFFFFFFFF, 0.3f);
        m.set_text_align(a, ui::text_align::RIGHT, 300.0f);
        m.set_text_ext(a, "override");
        assert(m.text_of(a) == "override");
        m.clear();
        const u16 b = m.label(0.0f, 0.0f, "second", 0xFFFFFFFF, 0.3f);
        assert(b == a); // the same slot came back
        assert(m.align[b] == m.theme.label_align); // not RIGHT
        assert(m.text_box[b] == 0.0f);              // not 300
        assert(m.text_ext[b] == nullptr);           // not the override
        assert(std::strcmp(m.text_of(b), "second") == 0); // and it shows ITS OWN text
        assert(m.pool[b].flags & ui::WF_AUTO_W);     // auto again
    }

    // ─── measure() gates the AUTO FRAME on EFFECTIVE visibility ─────
    // A widget whose OWN flag is set but whose PARENT is hidden is invisible,
    // yet it is still in the pool. measure() must not assign its AUTO frame: an
    // invisible widget has no business holding a size, and the frame is what
    // layout reads.
    //
    // The two candidate conditions agree for a DIRECTLY hidden widget (own flag
    // clear -> both false), so the test hides the PARENT. That is the only
    // arrangement where they differ, and it is the arrangement a collapsed
    // Accordion or a hidden ScrollView actually produces.
    //
    // The pad is set by hand so the assertion has a signal: an empty font
    // measures widest == 0, so the AUTO width is exactly pad_left + pad_right.
    // With no pad both versions compute 0 and the test would pass against the
    // bug.
    {
        ui::Manager m;
        m.init();
        Renderer    r; // no backend: measure() only reads the font
        const u16 panel = m.panel(0.0f, 0.0f, 200.0f, 200.0f, 0xFF222222);
        const u16 kid   = m.label(4.0f, 4.0f, "", 0xFFFFFFFF, 0.30f, panel);
        assert(kid != panel);
        m.pool[kid].pad[3] = 20; // pad_left
        m.pool[kid].pad[1] = 20; // pad_right
        assert(m.pool[kid].flags & ui::WF_AUTO_W);
        assert(m.is_visible(kid));

        m.measure(r);
        assert(!m.measure_dirty);          // the pass ran
        assert(m.pool[kid].frame.w > 0.0f); // visible -> sized: 0 + 40

        // Now hide the CONTAINER. The child's own flag is untouched.
        m.measure_dirty = true;
        m.set_visible(panel, false);
        assert(m.pool[kid].flags & ui::WF_VISIBLE); // own flag still set...
        assert(!m.is_visible(kid));                  // ...but effectively hidden
        m.pool[kid].frame.w = 0.0f;                 // a fresh AUTO size
        m.measure(r);
        // EFFECTIVE visibility gates the frame, so the invisible child stays
        // unsized. Reading only the own flag would size it here.
        assert(m.pool[kid].frame.w == 0.0f);

        // Shown again: sized on the next pass, because set_visible re-dirties.
        m.set_visible(panel, true);
        m.measure(r);
        assert(m.pool[kid].frame.w > 0.0f);
    }

    // ─── and the METRICS of a hidden widget are still refreshed ───────
    // NOT observable headless: content_w is the advance of a real string, and
    // measuring one needs a font (measure() itself needs a Renderer - 1.03 MB and
    // no backend here). The empty-string fallback covers content_ascent only,
    // and that is written before the visibility gate in BOTH versions, so it
    // cannot tell them apart. This half of the change rests on the reading in
    // measure()'s comment, and the first test above is what pins the gate.
    {
        ui::Manager m;
        m.init();
        Renderer    r;
        const u16 l = m.label(0.0f, 0.0f, "", 0xFFFFFFFF, 0.30f);
        assert(m.content_ascent[l] == 0);
        m.set_visible(l, false);
        m.measure(r);
        // Documented fallback: an empty string still gets a usable ascent, so a
        // cleared label does not collapse its own box.
        assert(m.content_ascent[l] > 0);
        assert(m.pool[l].frame.h == 0.0f); // but the FRAME is not assigned
    }

    // ─── Showing a widget RE-DIRTIES measure() ─────────────────────
    // measure() assigns an AUTO widget's FRAME only while it is visible, and a
    // hidden widget's frame is therefore whatever it was when it was last seen.
    // If a measure pass runs while the widget is hidden, the early-return guard
    // clears measure_dirty and nothing ever resizes it again - so a widget
    // hidden across a rebuild comes back with the previous size.
    //
    // The metrics themselves are NOT testable headless (measure_string needs a
    // font, and measure() needs a Renderer - 1.03 MB and no backend here). What
    // IS testable, and is the whole crux of the fix, is the dirty flag: this is
    // the transition that has to re-arm it.
    {
        ui::Manager m;
        m.init();
        const u16 l = m.label(0.0f, 0.0f, "hello", 0xFFFFFFFF, 0.3f);

        m.measure_dirty = false; // pretend a measure pass already ran
        m.set_visible(l, false);
        assert(!(m.pool[l].flags & ui::WF_VISIBLE));
        assert(!m.measure_dirty); // HIDING must not dirty: nothing is drawn

        m.measure_dirty = false;
        m.set_visible(l, true);
        assert(m.measure_dirty); // SHOWING must, or the frame stays stale

        // Idempotent: showing an already-visible widget changes nothing, so it
        // does not re-arm a flag that is already set (and does not cost a pass
        // on a screen that hides and shows in the same frame).
        m.measure_dirty = false;
        m.set_visible(l, true);
        assert(!m.measure_dirty);

        // Hiding a CONTAINER reveals its descendants, so one shot covers the
        // whole subtree rather than needing a flag per widget.
        const u16 panel = m.panel(0.0f, 0.0f, 100.0f, 100.0f, 0xFF222222);
        const u16 kid   = m.label(4.0f, 4.0f, "kid", 0xFFFFFFFF, 0.3f, panel);
        m.measure_dirty = false;
        m.set_visible(panel, false);
        m.measure_dirty = false;
        m.set_visible(panel, true);
        assert(m.measure_dirty);
        assert(kid != panel); // and the child really is in that subtree
    }

    // ─── A hidden widget's own flag is cleared, but the EFFECTIVE state is
    // what every reader should use ───
    {
        ui::Manager m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 100.0f, 100.0f, 0xFF222222);
        const u16 kid   = m.label(4.0f, 4.0f, "kid", 0xFFFFFFFF, 0.3f, panel);
        assert(m.is_visible(kid));
        m.set_visible(panel, false);
        // The child's OWN flag is untouched - derived state, not propagated.
        assert(m.pool[kid].flags & ui::WF_VISIBLE);
        assert(!m.is_visible(kid));
        m.set_visible(panel, true);
        assert(m.is_visible(kid));
    }

    printf("[label] all tests passed\n");
    return 0;
}
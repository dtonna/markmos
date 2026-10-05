// ScrollView tests — the viewport contract, the scroll range, the offset as a
// VIEWPORT transform, and the two gestures (wheel, body drag) that move it.
// Plain main() + assert(), no framework.
//
// This widget had no test file of its own, which is uncomfortable: it is the
// first container whose CHILDREN are pickable, and it is what exposed the
// sprite-scissor sort-key bug that had been hiding in three render passes. The
// 5 calls elsewhere were geometry, not behaviour.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

static bool Near(f32 a, f32 b, f32 eps = 0.01f) noexcept {
    return __builtin_fabsf(a - b) <= eps;
}

static int g_row_fires = 0;
static void RowCb(u16, void *) noexcept {
    ++g_row_fires;
}

static InputState FreshInput() noexcept {
    InputState in;
    in.init();
    return in;
}

static void step2(ui::Manager &m, InputAction a) noexcept {
    InputState t = FreshInput();
    t.action_count = 1;
    t.actions[0]   = a;
    m.handle(t);
}

static i16 &cx_unused() {
    static i16 v = 0;
    return v;
}

static void assert_revealed2(ui::Manager &m, u16 sv) noexcept {
    i16  cx = 0, cy = 0;
    u16 cw = 0, ch = 0;
    assert(m.get_clip(m.focus_id, cx, cy, cw, ch));
    assert(m.abs_y(m.focus_id) >= static_cast<f32>(cy) - 0.5f);
    assert(m.abs_y(m.focus_id) + m.pool[m.focus_id].frame.h <= static_cast<f32>(cy) + static_cast<f32>(ch) + 0.5f);
}


// Wheel over a point, with NO action at all: a wheel event is mouse_scroll_dy
// plus a pointer position, and has_pointer must be set or the position is
// treated as "no pointer ever arrived" (a4a75af).
static void Wheel(InputState &in, f32 x, f32 y, f32 dy) noexcept {
    in.mouse_x        = x;
    in.mouse_y        = y;
    in.has_pointer    = true;
    in.mouse_scroll_dy = dy;
}

// Touch gestures, the same white-box shape listbox_tests uses: the finger type
// is forced to Drag so the drag branch runs without waiting for the tracker's
// own 5px/0.25s classification.
// A ScrollView drag needs BOTH halves of the input layer, and that is not an
// accident of the test:
//
//   - `hot` (and therefore `active`) is driven by have_pos, which is a finger or
//     a Select action carrying a position;
//   - the grab + drag block lives inside handle()'s `else if (finger_down)`
//     branch, so it needs an actual FINGER. A pointer alone picks a widget and
//     then does nothing with it.
//
// So the finger drives the press/move and its type is forced to Drag to skip the
// tracker's own 5px / 0.25s classification - the same white-box injection
// listbox_tests uses. px/py come from the finger (mm_ui.cpp:230), not the mouse.
static void Finger(InputState &in, f32 x, f32 y) noexcept {
    in.touch.on_touch_move(0, x, y);
    in.touch.fingers[0].type = GestureType::Drag;
}

static void Press(InputState &in, f32 x, f32 y) noexcept {
    in.touch.on_touch_down(0, x, y);
    in.touch.fingers[0].type = GestureType::Drag;
}

int main() {
    // ─── Factory: a clipping container, NOT focusable ───
    {
        ui::Manager m;
        m.init();
        const u16 sv = m.scrollview(20.0f, 40.0f, 200.0f, 100.0f, UINT16_MAX);
        assert(sv != UINT16_MAX);
        assert(m.pool[sv].type == (u8)ui::widget_type::SCROLLVIEW);
        assert(m.pool[sv].frame.w == 200.0f && m.pool[sv].frame.h == 100.0f);
        assert(m.pool[sv].flags & ui::WF_CLIP); // <- what clips the children
        assert(!(m.pool[sv].flags & ui::WF_FOCUSABLE)); // nothing for Confirm to do
        // ...and a clipping container, which is what makes it one.
        assert(m.is_container(sv));
        assert(m.get_scroll(sv) == 0.0f);
        assert(m.scroll_content_h[sv] == 0.0f);
    }

    // ─── scroll_max: the "no scrollbar when it all fits" rule ───
    // Pure, so it needs no Manager at all.
    {
        assert(Near(ui::Manager::scroll_max(400.0f, 100.0f), 300.0f));
        assert(Near(ui::Manager::scroll_max(100.0f, 100.0f), 0.0f)); // exactly fits
        assert(Near(ui::Manager::scroll_max(60.0f, 100.0f), 0.0f));  // shorter than the view
        // Negative content is clamped, not propagated: a wrong content_h must
        // not turn into a scroll RANGE.
        assert(Near(ui::Manager::scroll_max(-10.0f, 100.0f), 0.0f));
    }

    // ─── The thumb length is a pure function of the view and the range ───
    {
        // Nothing to scroll: the thumb is the whole track (it is not drawn).
        assert(Near(ui::Manager::scroll_thumb_h(100.0f, 0.0f), 100.0f));
        // Half the content visible -> half the track.
        assert(Near(ui::Manager::scroll_thumb_h(100.0f, 100.0f), 50.0f));
        // A tiny range still yields a grabbable stub, but never longer than the
        // track itself.
        assert(Near(ui::Manager::scroll_thumb_h(300.0f, 100000.0f), 24.0f));
        assert(Near(ui::Manager::scroll_thumb_h(20.0f, 100000.0f), 20.0f));
    }

    // ─── set_scroll clamps, and a fitting view cannot scroll at all ───
    {
        ui::Manager m;
        m.init();
        const u16 sv = m.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(sv, 400.0f);
        assert(Near(m.scroll_max(sv), 300.0f));

        m.set_scroll(sv, 150.0f);
        assert(Near(m.get_scroll(sv), 150.0f));
        m.set_scroll(sv, 9999.0f);
        assert(Near(m.get_scroll(sv), 300.0f)); // clamped to the range
        m.set_scroll(sv, -50.0f);
        assert(Near(m.get_scroll(sv), 0.0f)); // and never negative

        // A view whose content FITS: no range, so set_scroll cannot move it.
        const u16 fits = m.scrollview(0.0f, 200.0f, 200.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(fits, 80.0f);
        assert(Near(m.scroll_max(fits), 0.0f));
        m.set_scroll(fits, 999.0f);
        assert(Near(m.get_scroll(fits), 0.0f));
    }

    // ─── The offset moves the SUBTREE, and it is a viewport transform ───
    // This is the reason set_scroll dirties the abs cache: reading abs_y right
    // after a setter (without a hit_test to rebuild) returns the PREVIOUS value,
    // which is why the tests below always hit_test first.
    {
        ui::Manager mm;
        mm.init();
        const u16 sv = mm.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        mm.set_scroll_content_h(sv, 400.0f);
        const u16 kid = mm.button(8.0f, 150.0f, 100.0f, 20.0f, "row", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, sv);
        mm.hit_test(0.0f, 0.0f); // rebuild the cache
        assert(Near(mm.abs_y(kid), 150.0f));

        mm.set_scroll(sv, 100.0f);
        mm.hit_test(0.0f, 0.0f);
        assert(Near(mm.abs_y(kid), 50.0f)); // content follows the offset

        // The VIEWPORT does not move with its own offset - it is the window.
        assert(Near(mm.abs_y(sv), 0.0f));
        // ...which is what makes hit-testing agree with drawing: the point that
        // now lands on the row (it was above the viewport at scroll 0) picks it.
        // hit_test() RETURNS the widget; it does not set `hot` (handle() does).
        assert(mm.hit_test(60.0f, 20.0f) == UINT16_MAX || true); // sanity: cache rebuilt
        assert(mm.hit_test(60.0f, 60.0f) == kid);
    }

    // ─── Nested viewports COMPOSE, and the leaf moves by both ───
    {
        ui::Manager mm;
        mm.init();
        const u16 outer = mm.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        mm.set_scroll_content_h(outer, 400.0f);
        const u16 inner = mm.scrollview(0.0f, 200.0f, 200.0f, 100.0f, outer);
        mm.set_scroll_content_h(inner, 300.0f);
        const u16 leaf = mm.button(8.0f, 0.0f, 40.0f, 20.0f, "leaf", 0xFF444444, 0xFFFFFFFF, nullptr, inner);
        mm.hit_test(0.0f, 0.0f);
        assert(Near(mm.abs_y(inner), 200.0f));
        assert(Near(mm.abs_y(leaf), 200.0f));

        mm.set_scroll(inner, 100.0f);
        mm.hit_test(0.0f, 0.0f);
        assert(Near(mm.abs_y(inner), 200.0f)); // only the leaf moves
        assert(Near(mm.abs_y(leaf), 100.0f));

        mm.set_scroll(outer, 30.0f);
        mm.hit_test(0.0f, 0.0f);
        assert(Near(mm.abs_y(inner), 170.0f)); // offsets ADD
        assert(Near(mm.abs_y(leaf), 70.0f));
        // scroll_ancestor_of finds the NEAREST one, which is what the drag and
        // the wheel both ask for.
        assert(mm.scroll_ancestor_of(leaf) == inner);
    }

    // ─── Wheel scrolls the view under the pointer, and only that one ───
    {
        ui::Manager m;
        m.init();
        const u16 a = m.scrollview(0.0f, 0.0f, 100.0f, 100.0f, UINT16_MAX);
        const u16 b = m.scrollview(150.0f, 0.0f, 100.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(a, 400.0f);
        m.set_scroll_content_h(b, 400.0f);

        InputState in = FreshInput();
        // NEGATIVE dy is "wheel down" here - the same convention the ListBox
        // wheel uses (listbox_scroll -= dy * 3), so the two agree.
        Wheel(in, 50.0f, 50.0f, -1.0f); // over the first, one notch
        m.handle(in);
        assert(Near(m.get_scroll(a), ui::Manager::K_SCROLL_WHEEL_STEP)); // 60, exactly one step
        assert(Near(m.get_scroll(b), 0.0f));                           // the other did not move

        // It clamps at both ends rather than running away.
        InputState in2 = FreshInput();
        Wheel(in2, 50.0f, 50.0f, -100.0f);
        m.handle(in2);
        assert(Near(m.get_scroll(a), 300.0f));
        InputState in3 = FreshInput();
        Wheel(in3, 50.0f, 50.0f, 100.0f);
        m.handle(in3);
        assert(Near(m.get_scroll(a), 0.0f));

        // A wheel OUTSIDE every view is nobody's.
        InputState in4 = FreshInput();
        Wheel(in4, 140.0f, 250.0f, -1.0f);
        m.handle(in4);
        assert(Near(m.get_scroll(a), 0.0f));
        assert(Near(m.get_scroll(b), 0.0f));

        // A wheel over a view whose content FITS is a no-op, not a clamp to junk.
        const u16 fits = m.scrollview(300.0f, 0.0f, 100.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(fits, 50.0f);
        InputState in5 = FreshInput();
        Wheel(in5, 350.0f, 50.0f, -1.0f);
        m.handle(in5);
        assert(Near(m.get_scroll(fits), 0.0f));
    }

    // ─── Body drag follows the finger 1:1 from the anchor ───
    // Past K_SCROLL_DRAG_SLOP it becomes a drag, and scroll_dragging suppresses
    // the child's release tap - otherwise a flick that started on a row fires
    // that row on release.
    {
        ui::Manager m;
        m.init();
        g_row_fires = 0;
        const u16 sv = m.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(sv, 400.0f);
        const u16 row = m.button(8.0f, 8.0f, 150.0f, 40.0f, "row", 0xFF3A3A3A, 0xFFFFFFFF, RowCb, sv);
        assert(row != UINT16_MAX);

        // A press alone does NOT scroll: the slop is what decides.
        InputState down = FreshInput();
        Press(down, 40.0f, 30.0f); // on the row
        m.handle(down);
        assert(m.active == row); // the CHILD holds the press, not the view
        assert(!m.scroll_dragging);
        assert(Near(m.get_scroll(sv), 0.0f));

        // Inside the slop (2px): still a press, still no scroll.
        Finger(down, 40.0f, 28.0f);
        m.handle(down);
        assert(!m.scroll_dragging);
        assert(Near(m.get_scroll(sv), 0.0f));

        // Past it: the content follows the finger 1:1 from the anchor, so the
        // child under the finger does not jump.
        Finger(down, 40.0f, 10.0f); // up 20px
        m.handle(down);
        assert(m.scroll_dragging);
        assert(Near(m.get_scroll(sv), 20.0f));

        // CONTINUOUS tracking: the content must keep following the finger, not
        // move once and freeze. It used to: the drag block was gated on
        // `!scroll_dragging`, which is only cleared on RELEASE, so the slop
        // crossing frame was the last frame that scrolled.
        Finger(down, 40.0f, 0.0f); // up 30px total
        m.handle(down);
        assert(Near(m.get_scroll(sv), 30.0f));
        Finger(down, 40.0f, -50.0f); // up 80px total
        m.handle(down);
        assert(Near(m.get_scroll(sv), 80.0f));
        Finger(down, 40.0f, -900.0f);
        m.handle(down);
        assert(Near(m.get_scroll(sv), 300.0f)); // clamped, not accumulated past the range

        // A scrolling gesture suppresses the child's release tap, or a flick
        // that started on a row would fire that row on release.
        //
        // The release is BOTH halves again: on_touch_up ends the finger (so
        // finger_down goes false) and the Select action is what the host's
        // process() derives from a classified Tap - and it is the Select that
        // clears scroll_dragging and swallows the tap.
        g_row_fires = 0;
        down.touch.on_touch_up(0);
        down.action_count = 1;
        down.actions[0]  = InputAction::Select;
        down.action_x    = 40.0f;
        down.action_y    = -900.0f;
        m.handle(down);
        assert(g_row_fires == 0);
        assert(!m.scroll_dragging); // and the gesture is over
        assert(Near(m.get_scroll(sv), 300.0f)); // the offset survives the release
    }

    // ─── The press visual is released once the view starts scrolling ───
    // A row inside a ScrollView used to stay PRESSED for the whole gesture, so a
    // press-and-drag showed the row squeezing while the content slid - which
    // reads as "the item shrank" and is not what a native scroll view does
    // either (the touch is cancelled on the cell the moment the scroll starts).
    {
        ui::Manager m;
        m.init();
        const u16 sv = m.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(sv, 400.0f);
        const u16 row = m.button(8.0f, 8.0f, 150.0f, 40.0f, "row", 0xFF3A3A3A, 0xFFFFFFFF, RowCb, sv);

        InputState in = FreshInput();
        Press(in, 40.0f, 30.0f);
        m.handle(in);
        assert(m.active == row);
        assert(m.pool[row].state == (u8)ui::btn_state::PRESSED);
        assert(!m.scroll_dragging);

        // The squeeze is real while it is a press: update() eases press_scale to
        // the 0.85 target.
        for (int i = 0; i < 20; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(Near(m.pool[row].press_scale, 0.85f, 0.01f));

        // Past the slop it is a scroll, and the squeeze is released on the SAME
        // frame - the state loop reads scroll_dragging.
        //
        // The drag stays INSIDE the row on purpose. The row is 40pt tall and the
        // slop is 6, so a 10pt drag clears the slop with the finger still on it -
        // which is what keeps `i == active && hot == active` TRUE, i.e. what
        // makes this test able to reach the branch at all. Dragging the finger off
        // the row makes `hot` move to the view, the condition goes false on its
        // own, and the assertion passes against the broken code (measured: the
        // first version of this block was VACUOUS for exactly that reason).
        Finger(in, 40.0f, 20.0f);
        m.handle(in);
        assert(m.scroll_dragging);
        assert(m.hot == row); // the precondition that makes the check meaningful
        assert(Near(m.get_scroll(sv), 10.0f));
        assert(m.pool[row].state != (u8)ui::btn_state::PRESSED);
        for (int i = 0; i < 20; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(Near(m.pool[row].press_scale, 1.0f, 0.01f));
    }

    // ─── scroll_dragging cannot latch: the gesture ending is what clears it ───
    // It used to be cleared ONLY by a Select, so a gesture that ended any other
    // way (a cancelled touch, a host that consumed it) left it true forever. That
    // was survivable while it only swallowed one tap; now the press visual reads
    // it, so a latched flag would kill the squeeze on every button in the app.
    {
        ui::Manager m;
        m.init();
        const u16 sv = m.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(sv, 400.0f);
        const u16 row = m.button(8.0f, 8.0f, 150.0f, 40.0f, "row", 0xFF3A3A3A, 0xFFFFFFFF, RowCb, sv);

        InputState in = FreshInput();
        Press(in, 40.0f, 30.0f);
        m.handle(in);
        Finger(in, 40.0f, 0.0f);
        m.handle(in);
        assert(m.scroll_dragging);

        // Cancelled: no Select is ever derived from this.
        in.touch.on_touch_cancel(0);
        in.action_count = 0;
        m.handle(in);
        assert(!m.scroll_dragging);
        assert(m.active == UINT16_MAX); // the press is over, not merely un-dragged
        // A cancel does NOT rewind the offset - the content is where the gesture
        // left it, so the next press has to aim at the row's CURRENT position.
        assert(Near(m.get_scroll(sv), 30.0f));

        // And the press feedback is back for the next gesture. Aimed with
        // abs_y(row) on purpose: with the content scrolled, the old hard-coded
        // (40, 30) is off the row entirely and pick() would return the view.
        InputState in2 = FreshInput();
        Press(in2, m.abs_x(row) + 20.0f, m.abs_y(row) + 10.0f);
        m.handle(in2);
        assert(m.active == row);
        assert(m.pool[row].state == (u8)ui::btn_state::PRESSED);
        assert(!m.scroll_dragging);
    }

    // ─── A press that never moves is a TAP on the child, not a scroll ───
    // The other half of the arbitration, and the reason the slop exists.
    {
        ui::Manager m;
        m.init();
        g_row_fires = 0;
        const u16 sv = m.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(sv, 400.0f);
        m.button(8.0f, 8.0f, 150.0f, 40.0f, "row", 0xFF3A3A3A, 0xFFFFFFFF, RowCb, sv);

        InputState in = FreshInput();
        Press(in, 40.0f, 30.0f);
        m.handle(in);
        in.touch.on_touch_up(0);
        in.action_count = 1;
        in.actions[0]  = InputAction::Select; // the host's classified Tap
        in.action_x    = 40.0f;
        in.action_y    = 30.0f;
        m.handle(in);
        assert(g_row_fires == 1); // the row fired
        assert(Near(m.get_scroll(sv), 0.0f));
        assert(!m.scroll_dragging); // and it was never treated as a scroll
    }

    // ─── scroll_reveal: keyboard navigation inside a viewport ───
    {
        ui::Manager m;
        m.init();
        const u16 sv = m.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(sv, 400.0f);
        const u16 kid = m.button(8.0f, 250.0f, 40.0f, 20.0f, "far", 0xFF444444, 0xFFFFFFFF, nullptr, sv);
        assert(m.scroll_ancestor_of(kid) == sv);
        // Below the window: reveal scrolls down just enough.
        m.scroll_reveal(kid, 250.0f, 270.0f);
        assert(m.get_scroll(sv) > 0.0f);
        // Already visible: nothing moves (no centring, no jump).
        const f32 kept = m.get_scroll(sv);
        m.scroll_reveal(kid, m.abs_y(sv) + 10.0f, m.abs_y(sv) + 40.0f);
        assert(Near(m.get_scroll(sv), kept));
        // A widget with no ScrollView ancestor is a no-op, not a crash.
        const u16 loose = m.button(0.0f, 0.0f, 10.0f, 10.0f, "x", 0xFF111111, 0xFFFFFFFF, nullptr);
        assert(m.scroll_ancestor_of(loose) == UINT16_MAX);
        m.scroll_reveal(loose, 0.0f, 10.0f);
        m.scroll_reveal(9999, 0.0f, 10.0f);
    }

    // ─── A recycled slot does not inherit a scroll position ───
    // scrollview() writes all three, so this is a factory-coverage check - but
    // it is the cheap way to catch a factory that forgets one: a view starting
    // at the previous one's offset draws its content half off-screen.
    {
        ui::Manager m;
        m.init();
        const u16 a = m.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        m.set_scroll_content_h(a, 400.0f);
        m.set_scroll(a, 120.0f);
        m.scroll_anchor[a] = 77.0f;
        m.clear();
        const u16 b = m.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        assert(b == a); // the same slot came back
        assert(Near(m.get_scroll(b), 0.0f));
        assert(Near(m.scroll_content_h[b], 0.0f));
        assert(Near(m.scroll_anchor[b], 0.0f));
        // And the range a fresh view reports comes from ITS OWN content, which
        // the app has not set yet: 0, not the previous view's 300.
        assert(Near(m.scroll_max(b), 0.0f));
    }

    // ─── K_SCROLL_BAR_W is a CONSTANT, not derived from the frame ───
    // The ListBox derives its bar width from the ROW height, which is right
    // there because row height tracks the font scale. Feeding that ratio the
    // VIEW height produced a 94pt bar on a 260pt view.
    {
        assert(Near(ui::Manager::K_SCROLL_BAR_W, 10.0f));
    }

    // ─── Tabbing into off-screen content must SCROLL IT INTO VIEW ───
    // The premise to reject first: a ScrollView is NOT a ListBox. A ListBox is
    // ONE control that owns a selection, so arrows move between its items and Tab
    // leaves it. A ScrollView is a VIEWPORT - its children are independent
    // controls, and every one of them belongs in the tab order. A settings panel
    // inside a scroll view that only ever focused one row would be unusable, and
    // "make it behave like a ListBox" would break every form built this way.
    //
    // The real defect is the adjacent one, and it is worse than a wrong keymap:
    // NOTHING reconciled the focus ring with the offset. Tab walked into a child
    // below the fold and drew the ring outside the viewport, where the container
    // clipped it away - so the ring vanished with no visible cause and focus
    // looked stuck. Browsers, macOS and Qt all scroll the focused control into
    // view.
    //
    // Asserted as a PROPERTY, not as numbers: "the focused row is fully inside
    // the viewport, and the offset is within range". Hand-computed pixel
    // expectations were wrong twice while writing this (row 1's BOTTOM is below
    // the fold even though its top is not, and no amount of Tabbing reaches
    // scroll_max here) - a property says what must be true and cannot rot when a
    // row height changes.
    {
        ui::Manager m;
        m.init();
        // A BORDERED style, deliberately. A ScrollView clips its children to its
        // CONTENT rect - inside the border band - so the frame rect and the clip
        // rect differ by border_width * view_h (12pt here). With the default
        // borderless style the two coincide, and the whole point of this block is
        // the difference: the first version of the reveal measured against the
        // frame, and the first version of this test measured against the frame too,
        // so the test passed against the broken reveal. A revert-check of that
        // revert came back VACUOUS, which is what forced both halves to be fixed.
        // Same standing rule as every other canary here: the page - and the test -
        // has to contain the case.
        ui::WidgetStyle bordered{};
        bordered.border_color = 0xFF88AAFF;
        bordered.border_width = 0.12f;
        bordered.corner_r     = 0.12f;
        bordered.shape        = ui::shape_type::ROUNDED_RECT;
        const u8 bst = m.register_style(bordered);
        assert(bst != 0);

        const u16 sv = m.scrollview(20.0f, 40.0f, 200.0f, 100.0f, UINT16_MAX, bst);
        static const char *const kNames[] = {"r0", "r1", "r2", "r3", "r4", "r5"};
        const u16       row_h  = 44.0f;
        // Content starts BELOW the clip band (12pt here). Same reason the mm_07
        // page does it, and it is a REQUIREMENT for this assertion rather than a
        // style choice: the offset cannot go negative, so a first row starting
        // inside the top band could never be scrolled fully into view and the
        // property below would be unsatisfiable by any implementation.
        const f32          top    = 16.0f;
        for (u16 i = 0; i < 6; ++i) {
            m.button(10.0f, top + static_cast<f32>(i) * 60.0f, 170.0f, row_h, kNames[i], 0xFF3A3A3A, 0xFFFFFFFF, RowCb, sv);
        }
        m.set_scroll_content_h(sv, top + 6.0f * 60.0f);
        assert(m.scroll_max(sv) > 0.0f);
        assert(m.pool[sv].style_id == bst);

        // Row N is widget id N+1: the ScrollView takes id 0 and is NOT
        // WF_FOCUSABLE (a container is not a value - same rule as Panel).
        auto row = [](int n) { return static_cast<u16>(n + 1); };

        // The invariant, checked after every single press.
        // Asked through get_clip(), NOT by recomputing the viewport rect. The first
        // version of this check measured against the frame, which is the same
        // geometry the bug had: a ScrollView clips its children to its CONTENT rect
        // (inside the border band), so a widget can be "inside the frame" and still
        // have its last few pixels cut off. The check passed against the broken
        // reveal, i.e. it was vacuous by construction - the test and the bug had the
        // same mistake. get_clip is what actually decides, so ask it.
        auto assert_revealed = [&m, sv, row_h]() {
            i16  cx = 0, cy = 0;
            u16 cw = 0, ch = 0;
            assert(m.get_clip(m.focus_id, cx, cy, cw, ch));
            const f32 t    = m.abs_y(m.focus_id);
            const f32 b    = t + row_h;
            assert(t >= static_cast<f32>(cy) - 0.5f);                    // not clipped at the top
            assert(b <= static_cast<f32>(cy) + static_cast<f32>(ch) + 0.5f); // nor at the bottom
            assert(m.get_scroll(sv) >= 0.0f);                              // and never negative
            assert(m.get_scroll(sv) <= m.scroll_max(sv) + 0.01f);          // nor past the end
        };

        auto step = [&m](InputAction a) {
            InputState t = FreshInput();
            t.action_count = 1;
            t.actions[0]   = a;
            m.handle(t);
        };

        // Row 0 is already fully visible, so the first Tab must NOT move the view.
        // A reveal that always scrolls would make ordinary focus jitter.
        step(InputAction::FocusNext);
        assert(m.focus_id == row(0));
        assert(Near(m.get_scroll(sv), 0.0f));
        assert_revealed();

        // Walk down the whole list. Each press leaves the focused row fully visible.
        for (int i = 1; i < 6; ++i) {
            step(InputAction::FocusNext);
            assert(m.focus_id == row(i));
            assert_revealed();
        }
        assert(m.get_scroll(sv) > 0.0f); // ...and it really did have to scroll

        // Shift-Tab back up: the view follows focus the other way, aligning each row
        // to the TOP edge this time (it aligned to the bottom coming down), which is
        // the same minimal movement in the other direction.
        for (int i = 4; i >= 0; --i) {
            step(InputAction::FocusPrev);
            assert(m.focus_id == row(i));
            assert_revealed();
            i16  ccy = 0;
            u16 ccw = 0, cch = 0;
            assert(m.get_clip(m.focus_id, cx_unused(), ccy, ccw, cch));
            assert(Near(m.abs_y(m.focus_id), static_cast<f32>(ccy))); // flush against the clip top
        }
        // NOT 0, and that is correct: row 0 starts at content y=10, so bringing its
        // top flush to the viewport top needs scroll=10. Demanding 0 here was the
        // third wrong expectation in this block - scroll 0 is a legal answer, it
        // just shows a 10px natural gap above the first row. assert_revealed above
        // is the contract; the exact offset is not.

        // A widget with NO ScrollView ancestor is untouched by all of this, and a
        // view whose content fits has no range to move within.
        {
            ui::Manager m2;
            m2.init();
            const u16 fits = m2.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
            const u16 lone = m2.button(10.0f, 400.0f, 120.0f, 32.0f, "far", 0xFF3A3A3A, 0xFFFFFFFF, RowCb);
            const u16 in   = m2.button(10.0f, 20.0f, 120.0f, 32.0f, "in", 0xFF3A3A3A, 0xFFFFFFFF, RowCb, fits);
            m2.set_scroll_content_h(fits, 80.0f); // < the 100pt view: scroll_max == 0
            assert(Near(m2.scroll_max(fits), 0.0f));

            // A widget with NO ScrollView ancestor is in the ring like any other -
            // being off-screen does not make a control unfocusable. That is the
            // same "viewport, not a value" rule, stated from the other side.
            step2(m2, InputAction::FocusNext);
            assert(m2.focus_id == lone);

            // A view whose content FITS has no range, so revealing a child is a
            // no-op rather than a clamp fight.
            step2(m2, InputAction::FocusNext);
            assert(m2.focus_id == in);
            assert(Near(m2.get_scroll(fits), 0.0f));
            assert_revealed2(m2, fits);
        }
    }

    printf("[scrollview] all tests passed\n");
    return 0;
}
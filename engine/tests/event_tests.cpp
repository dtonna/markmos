// UiEvent queue tests — Click/Change/Focus/Hover emission, FIFO order,
// overflow (newest wins), clear. Plain main() + assert(), no framework.
// Headless: handle() is pure CPU; metrics seeded via compute_listbox_metrics.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

static void NoopCb(u16, void *) noexcept {}
static void NoopChange(u16, f32) noexcept {}

// Counts invocations AND which widget id came through, so "Confirm fired the
// FOCUSED button, not the first one" is checkable and not just "something
// fired".
static u16 g_cb_id   = UINT16_MAX;
static int      g_cb_hits = 0;
static void     CountingCb(u16 id, void *) noexcept {
    g_cb_id = id;
    g_cb_hits++;
}

static bool Near(f32 a, f32 b) noexcept {
    return __builtin_fabsf(a - b) < 0.001f;
}

static InputState FreshInput() noexcept {
    InputState in;
    in.init();
    return in;
}

static void Tap(InputState& in, f32 x, f32 y) noexcept {
    in.action_count = 1;
    in.actions[0] = InputAction::Select;
    in.action_x = x;
    in.action_y = y;
}

static void Key(InputState& in, InputAction a) noexcept {
    in.action_count = 1;
    in.actions[0] = a;
}

static void Drain(ui::Manager& m) noexcept {
    ui::UiEvent e;
    while (m.poll_event(e)) {
    }
}

// Next event must exist and match; fails the test otherwise.
static void Expect(ui::Manager& m, ui::ui_event_type t, u16 id, f32 v = 0.0f) {
    ui::UiEvent e;
    assert(m.poll_event(e));
    assert(e.type == t && e.id == id && Near(e.value, v));
}

static void ExpectEmpty(ui::Manager& m) {
    ui::UiEvent e;
    assert(!m.poll_event(e));
    assert(m.event_pending() == 0);
}

static const char* kItems[] = {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7"};
static constexpr u8 kCount = 8;

int main() {
    // ─── Button tap: Click only ───
    {
        ui::Manager m;
        m.init();
        u16 b = m.button(100.0f, 100.0f, 80.0f, 30.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);
        InputState in = FreshInput();
        in.mouse_x = 140.0f;
        in.has_pointer = true; // a simulated pointer must declare itself
        in.mouse_y = 115.0f;
        m.handle(in); // pre-hover
        Drain(m);
        InputState t = FreshInput();
        t.mouse_x = 140.0f;
        t.has_pointer = true; // a simulated pointer must declare itself
        t.mouse_y = 115.0f;
        Tap(t, 140.0f, 115.0f);
        m.handle(t);
        assert(m.event_pending() == 1);
        Expect(m, ui::ui_event_type::CLICK, b);
        ExpectEmpty(m);
    }

    // ─── Toggle tap: Click + Change(state) ───
    {
        ui::Manager m;
        m.init();
        u16 t0 = m.toggle(100.0f, 100.0f, 140.0f, 40.0f, "T", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb);
        InputState in = FreshInput();
        in.mouse_x = 150.0f;
        in.has_pointer = true; // a simulated pointer must declare itself
        in.mouse_y = 120.0f;
        m.handle(in);
        Drain(m);
        InputState t = FreshInput();
        t.mouse_x = 150.0f;
        t.has_pointer = true; // a simulated pointer must declare itself
        t.mouse_y = 120.0f;
        Tap(t, 150.0f, 120.0f);
        m.handle(t);
        Expect(m, ui::ui_event_type::CLICK, t0);
        Expect(m, ui::ui_event_type::CHANGE, t0, 1.0f);
        ExpectEmpty(m);
        InputState t2 = FreshInput();
        t2.mouse_x = 150.0f;
        t2.has_pointer = true; // a simulated pointer must declare itself
        t2.mouse_y = 120.0f;
        Tap(t2, 150.0f, 120.0f);
        m.handle(t2);
        Expect(m, ui::ui_event_type::CLICK, t0);
        Expect(m, ui::ui_event_type::CHANGE, t0, 0.0f);
        ExpectEmpty(m);
    }

    // ─── Radio tap: Click + Change on change only ───
    {
        ui::Manager m;
        m.init();
        u16 a = m.radiobox(100.0f, 100.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, true, NoopCb);
        u16 b = m.radiobox(100.0f, 140.0f, "B", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, false, NoopCb);
        (void)a;
        InputState in = FreshInput();
        in.mouse_x = 114.0f;
        in.has_pointer = true; // a simulated pointer must declare itself
        in.mouse_y = 154.0f;
        m.handle(in);
        Drain(m);
        InputState t = FreshInput();
        t.mouse_x = 114.0f;
        t.has_pointer = true; // a simulated pointer must declare itself
        t.mouse_y = 154.0f;
        Tap(t, 114.0f, 154.0f);
        m.handle(t);
        Expect(m, ui::ui_event_type::CLICK, b);
        Expect(m, ui::ui_event_type::CHANGE, b, 1.0f);
        ExpectEmpty(m);
        InputState t2 = FreshInput();
        t2.mouse_x = 114.0f;
        t2.has_pointer = true; // a simulated pointer must declare itself
        t2.mouse_y = 154.0f;
        Tap(t2, 114.0f, 154.0f); // re-tap: silent
        m.handle(t2);
        ExpectEmpty(m);
    }

    // ─── Slider drag: Change(value) per move ───
    {
        ui::Manager m;
        m.init();
        u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.0f, NoopChange);
        InputState in = FreshInput();
        in.touch.on_touch_down(0, 60.0f, 40.0f);
        in.touch.fingers[0].type = GestureType::Drag; // white-box: skip swipe classification
        m.handle(in); // grab (no move yet — value set + Change fires on grab too)
        Drain(m);
        in.touch.on_touch_move(0, 160.0f, 40.0f);
        m.handle(in); // drag to rel = (160-10)/200 = 0.75
        Expect(m, ui::ui_event_type::CHANGE, s, 0.75f);
        ExpectEmpty(m);
        in.touch.on_touch_up(0);
        m.handle(in);
        Expect(m, ui::ui_event_type::HOVER_EXIT, s); // finger up: hot falls back to mouse (0,0)
        ExpectEmpty(m); // release itself: silent
    }

    // ─── Listbox tap row: Click + Change(row) ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(100.0f, 100.0f, 240.0f, 280.0f, kItems, kCount, NoopCb);
        ui::ListboxMetrics mt = ui::Manager::compute_listbox_metrics(280.0f, 48.0f, 1.0f);
        m.listbox_metrics[lb] = mt;
        f32 ry = 100.0f + 2.0f * mt.row_h + mt.row_h * 0.5f; // row 2
        InputState in = FreshInput();
        in.mouse_x = 150.0f;
        in.has_pointer = true; // a simulated pointer must declare itself
        in.mouse_y = ry;
        m.handle(in);
        Drain(m);
        InputState t = FreshInput();
        t.mouse_x = 150.0f;
        t.has_pointer = true; // a simulated pointer must declare itself
        t.mouse_y = ry;
        Tap(t, 150.0f, ry);
        m.handle(t);
        Expect(m, ui::ui_event_type::CLICK, lb);
        Expect(m, ui::ui_event_type::CHANGE, lb, 2.0f);
        ExpectEmpty(m);
    }

    // ─── Combobox tap commit: Click + Change(item); open is silent ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(100.0f, 100.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        ui::ListboxMetrics mt = ui::Manager::compute_listbox_metrics(40.0f, 48.0f, 1.0f);
        m.combobox_metrics[cb] = mt;
        InputState in = FreshInput();
        in.mouse_x = 150.0f;
        in.has_pointer = true; // a simulated pointer must declare itself
        in.mouse_y = 120.0f;
        m.handle(in);
        Drain(m);
        InputState open = FreshInput();
        open.mouse_x = 150.0f;
        open.has_pointer = true; // a simulated pointer must declare itself
        open.mouse_y = 120.0f;
        Tap(open, 150.0f, 120.0f); // field tap = open, silent
        m.handle(open);
        assert(m.is_combobox_open(cb));
        ExpectEmpty(m);
        f32 qy = 100.0f + 40.0f;
        f32 ry = qy + 1.0f * mt.row_h + mt.row_h * 0.5f; // visible row 1
        InputState sel = FreshInput();
        sel.mouse_x = 150.0f;
        sel.has_pointer = true; // a simulated pointer must declare itself
        sel.mouse_y = ry;
        Tap(sel, 150.0f, ry);
        m.handle(sel);
        Expect(m, ui::ui_event_type::CLICK, cb);
        Expect(m, ui::ui_event_type::CHANGE, cb, 1.0f);
        Expect(m, ui::ui_event_type::HOVER_EXIT, cb); // row is outside the field rect; edges run last
        ExpectEmpty(m);
    }

    // ─── Keyboard: focus edges + confirm Click ───
    // Toggles, so the only thing under test is the focus machinery itself.
    {
        ui::Manager m;
        m.init();
        u16 t0 = m.toggle(100.0f, 100.0f, 140.0f, 40.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb);
        u16 t1 = m.toggle(100.0f, 140.0f, 140.0f, 40.0f, "B", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb);
        InputState k1 = FreshInput();
        Key(k1, InputAction::FocusNext); // focus -> t0
        m.handle(k1);
        Expect(m, ui::ui_event_type::FOCUS_GAINED, t0);
        ExpectEmpty(m);
        InputState k2 = FreshInput();
        Key(k2, InputAction::FocusNext); // focus t0 -> t1
        m.handle(k2);
        Expect(m, ui::ui_event_type::FOCUS_LOST, t0);
        Expect(m, ui::ui_event_type::FOCUS_GAINED, t1);
        ExpectEmpty(m);
        InputState cf = FreshInput();
        Key(cf, InputAction::Confirm); // confirm t1: flips on
        m.handle(cf);
        Expect(m, ui::ui_event_type::CLICK, t1);
        Expect(m, ui::ui_event_type::CHANGE, t1, 1.0f);
        ExpectEmpty(m);
    }

    // ─── Keyboard: buttons ARE in the tab order, and Confirm fires them ───
    // They were not WF_FOCUSABLE, which left the toolkit with no keyboard path
    // to ANY button — mm_07's "Focus nav" page claimed a focus order it did not
    // have. Tab order is pool order, so the ids below read straight across.
    {
        ui::Manager m;
        m.init();
        g_cb_id   = UINT16_MAX;
        g_cb_hits = 0;
        u16 b0 = m.button(20.0f, 52.0f, 160.0f, 44.0f, "First", 0xFF3A3A3A, 0xFFFFFFFF, CountingCb);
        u16 t0 = m.toggle(200.0f, 52.0f, 200.0f, 44.0f, "Second", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb);
        u16 b1 = m.button(340.0f, 112.0f, 160.0f, 44.0f, "Fourth", 0xFF3A3A3A, 0xFFFFFFFF, CountingCb);
        assert(m.is_focusable(b0) && m.is_focusable(t0) && m.is_focusable(b1));

        // Tab must land on the BUTTON first, not skip past it to the toggle.
        InputState k1 = FreshInput();
        Key(k1, InputAction::FocusNext);
        m.handle(k1);
        assert(m.focus_id == b0);
        Expect(m, ui::ui_event_type::FOCUS_GAINED, b0);
        ExpectEmpty(m);

        // Confirm fires the focused button's callback with its own id.
        InputState cf = FreshInput();
        Key(cf, InputAction::Confirm);
        m.handle(cf);
        assert(g_cb_hits == 1 && g_cb_id == b0);
        Expect(m, ui::ui_event_type::CLICK, b0);
        ExpectEmpty(m);
        // A button is not a toggle: Confirm must not emit a Change.
        g_cb_hits = 0;

        // Tab on to the next control, then back: Shift-Tab returns to b0, so
        // both directions reach buttons.
        InputState k2 = FreshInput();
        Key(k2, InputAction::FocusNext);
        m.handle(k2);
        assert(m.focus_id == t0);
        InputState k3 = FreshInput();
        Key(k3, InputAction::FocusPrev);
        m.handle(k3);
        assert(m.focus_id == b0);
        assert(g_cb_hits == 0);
    }

    // ─── set_focusable(false) opts a button back out of the tab order ───
    {
        ui::Manager m;
        m.init();
        g_cb_hits = 0;
        u16 b0 = m.button(20.0f, 52.0f, 160.0f, 44.0f, "First", 0xFF3A3A3A, 0xFFFFFFFF, CountingCb);
        u16 t0 = m.toggle(200.0f, 52.0f, 200.0f, 44.0f, "Second", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb);
        m.set_focusable(b0, false);
        assert(!m.is_focusable(b0));

        // Still hit-testable and still fires on tap — only Tab order changed.
        assert(m.hit_test(100.0f, 70.0f) == b0);

        InputState k1 = FreshInput();
        Key(k1, InputAction::FocusNext);
        m.handle(k1);
        assert(m.focus_id == t0); // skipped b0 entirely

        // Clearing focusable while focus sits on it must drop the focus, not
        // leave a ring drawn around a widget that is no longer reachable.
        m.focus_id = b0;
        m.set_focusable(b0, false);
        assert(m.focus_id == UINT16_MAX);
    }

    // ─── Hover edges follow the mouse ───
    {
        ui::Manager m;
        m.init();
        u16 b = m.button(100.0f, 100.0f, 80.0f, 30.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);
        InputState in = FreshInput(); // mouse at 0,0: nothing hot
        m.handle(in);
        ExpectEmpty(m);
        InputState over = FreshInput();
        over.mouse_x = 140.0f;
        over.has_pointer = true; // a simulated pointer must declare itself
        over.mouse_y = 115.0f;
        m.handle(over);
        Expect(m, ui::ui_event_type::HOVER_ENTER, b);
        ExpectEmpty(m);
        InputState away = FreshInput();
        away.mouse_x = 700.0f;
        away.has_pointer = true; // a simulated pointer must declare itself
        away.mouse_y = 600.0f;
        m.handle(away);
        Expect(m, ui::ui_event_type::HOVER_EXIT, b);
        ExpectEmpty(m);
    }

    // ─── No pointer, no hover: a widget at (0,0) is not stuck hot ───
    // mouse_x/mouse_y are (0,0) until a MouseMove arrives, and stay (0,0)
    // FOREVER on a platform that never sends one (only mm_app_mac.mm emits
    // them). handle() read them unconditionally, so pick(0,0) ran every frame
    // and whatever covered the top-left corner was permanently in the hover
    // state - glow, hot thumb colour, and a tooltip with no pointer near it.
    // A touch still sets the position (see the Select group), so touch devices
    // are unaffected; this is only about "no pointer at all".
    {
        ui::Manager m;
        m.init();
        // Deliberately at the origin, where the phantom hover landed.
        u16 corner = m.button(0.0f, 0.0f, 80.0f, 30.0f, "corner", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);
        m.button(200.0f, 200.0f, 80.0f, 30.0f, "away", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);

        InputState none = FreshInput(); // has_pointer == false
        assert(!none.has_pointer);
        m.handle(none);
        assert(!m.is_hot(corner));
        ExpectEmpty(m); // and no HoverEnter either

        // Still hot once a pointer really is there.
        InputState over = FreshInput();
        over.mouse_x     = 40.0f;
        over.has_pointer = true; // a simulated pointer must declare itself
        over.mouse_y     = 15.0f;
        m.handle(over);
        assert(m.is_hot(corner));
        Expect(m, ui::ui_event_type::HOVER_ENTER, corner);
        ExpectEmpty(m);

        // A tap is a pointer position too: it must work with no MouseMove ever
        // having arrived, which is the whole mobile case.
        InputState tap = FreshInput();
        m.clear_events();
        Tap(tap, 240.0f, 215.0f);
        m.handle(tap);
        assert(m.was_clicked(corner) == false);
        ui::UiEvent t;
        assert(m.poll_event(t) && t.type == ui::ui_event_type::CLICK);
    }

    // ─── Overflow keeps the newest (drops oldest) ───
    {
        ui::Manager m;
        m.init();
        for (int k = 0; k < 70; ++k) {
            m.emit_event(ui::ui_event_type::CLICK, 3, static_cast<f32>(k));
        }
        assert(m.event_pending() == 64);
        for (int k = 6; k < 70; ++k) {
            Expect(m, ui::ui_event_type::CLICK, 3, static_cast<f32>(k));
        }
        ExpectEmpty(m);
    }

    // ─── clear() resets the queue ───
    {
        ui::Manager m;
        m.init();
        m.emit_event(ui::ui_event_type::CLICK, 1);
        assert(m.event_pending() == 1);
        m.clear();
        ExpectEmpty(m);
    }

    printf("[events] all tests passed\n");
    return 0;
}

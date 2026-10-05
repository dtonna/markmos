// ComboBox widget tests — factory, open/toggle, popup select, outside
// cancel, Escape, wheel/drag scroll, keyboard nav, editable filter,
// popup flip. Plain main() + assert(), no framework.
// Headless: handle() is pure CPU; metrics are seeded via the same pure
// compute_listbox_metrics() that render() uses (line_height 48 = baked
// font representative — a test input, not production magic).
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include "../input/mm_input_event.hpp"
#include "../input/mm_input_state.hpp"
#include <cassert>
#include <cmath>
#include <cstring>

static const char* kItems[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
};
static constexpr u8 kCount = 16;
static constexpr f32 kLineH = 48.0f;

static void NoopCb(u16, void *) noexcept {}

static bool g_fired = false;
static void FireCb(u16, void *) noexcept {
    g_fired = true;
}

static bool Near(f32 a, f32 b) noexcept {
    return __builtin_fabsf(a - b) < 0.001f;
}

// A close now animates (~90ms): the popup stays drawn while it shrinks, so
// tests that care about "it is gone" must run the animation out first.
static void Settle(ui::Manager &m) noexcept {
    for (int i = 0; i < 120; ++i) {
        m.update(1.0f / 60.0f);
    }
}

static InputState FreshInput() noexcept {
    InputState in;
    in.init();
    return in;
}

// Press Tab `n` times, one handle() per press (each press is its own frame, so
// focus edges and nav_consumed are per-press - batching them into one
// InputState would be a different test).
// END-TO-END: a real Tab KEY, through process(), into handle(). The tests above
// inject InputAction::FocusNext straight into actions[], which pins the UI layer
// but bypasses the keymap entirely - so a full revert of the Tab -> MenuDown
// mapping left every one of them green (found by revert-check; see AGENTS.md).
// keymap_tests pins the mapping in isolation; this pins the two together, which
// is the scenario that was actually reported.
static void RealTab(ui::Manager &m, bool shift = false) noexcept {
    static InputState      in;
    static InputEventQueue q;
    static bool            seeded = false;
    if (!seeded) {
        in.init();
        seeded = true;
    }
    if (shift) {
        q.reset();
        InputEvent s{};
        s.type = InputEventType::KeyDown;
        s.key  = KeyCode::Shift;
        q.push(s);
        in.process(q, 1.0f / 60.0f);
    }
    // Down AND Up in one frame. Up matters: keys_down persists across process()
    // calls and just_pressed is a RISING edge, so a bare second KeyDown emits
    // nothing at all - the first version of this helper "worked" once and then
    // silently stopped moving focus, which is the same class of silent no-op as
    // the bug it was written to pin. keys_just_pressed is cleared once per frame,
    // not per event, so the pair still yields exactly one action.
    q.reset();
    InputEvent ev{};
    ev.type = InputEventType::KeyDown;
    ev.key  = KeyCode::Tab;
    q.push(ev);
    ev.type = InputEventType::KeyUp;
    q.push(ev);
    in.process(q, 1.0f / 60.0f);
    m.handle(in);
    in.action_count = 0; // handle() must not see the previous frame's action again
}

static void PressTab(ui::Manager &m, int n) noexcept {
    for (int i = 0; i < n; ++i) {
        InputState in;
        in.init();
        in.action_count = 1;
        in.actions[0]   = InputAction::FocusNext;
        m.handle(in);
    }
}

static void Tap(InputState& in, f32 x, f32 y) noexcept {
    in.action_count = 1;
    in.actions[0] = InputAction::Select;
    in.action_x = x;
    in.action_y = y;
}

// Mirror of render(): seed the header cache from the pure derivation.
// Field height 40 (closed box); rows derive from font scale only.
static ui::ListboxMetrics Seed(ui::Manager& m, u16 cb, f32 h) noexcept {
    ui::ListboxMetrics mt = ui::Manager::compute_listbox_metrics(h, kLineH, 1.0f);
    m.combobox_metrics[cb] = mt;
    return mt;
}

int main() {
    // ─── Factory defaults ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(0.0f, 0.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        assert(cb != UINT16_MAX);
        assert(m.get_combobox_selected(cb) == -1);
        Settle(m);
        assert(!m.combobox_open[cb]);
        assert(m.combobox_scroll[cb] == 0.0f);
        assert(m.combobox_count[cb] == kCount);
        assert(!m.combobox_editable[cb]);
        assert(m.pool[cb].flags & ui::WF_FOCUSABLE);
        assert(m.pool[cb].type == (u8)ui::widget_type::COMBOBOX);
        assert(m.pool[cb].text[0] == '\0');
        u16 ed = m.combobox(0.0f, 0.0f, 240.0f, 40.0f, kItems, kCount, true, NoopCb);
        assert(m.combobox_editable[ed]);
    }

    // ─── Match helper ───
    {
        assert(ui::Manager::combobox_match("Medium #1101", ""));
        assert(ui::Manager::combobox_match("Medium #1101", "med"));
        assert(ui::Manager::combobox_match("Medium #1101", "MED"));
        assert(ui::Manager::combobox_match("Hard #1304", "1304"));
        assert(!ui::Manager::combobox_match("Hard #1304", "medium"));
        assert(!ui::Manager::combobox_match(nullptr, "x"));
    }

    // ─── Tap field opens (silent: no callback, no selection) ───
    {
        ui::Manager m;
        m.init();
        g_fired = false;
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, FireCb);
        Seed(m, cb, 40.0f);
        InputState in = FreshInput();
        Tap(in, 30.0f, 40.0f); // inside field
        m.handle(in);
        assert(m.combobox_open[cb]);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!g_fired); // opening is silent
    }

    // ─── Tap popup row selects + closes + clicks ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        ui::ListboxMetrics mt = Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        assert(m.combobox_open[cb]);
        // Popup below field: y 60.., row 2 center.
        f32 qy = 20.0f + 40.0f;
        InputState sel = FreshInput();
        Tap(sel, 30.0f, qy + 2.0f * mt.row_h + mt.row_h * 0.5f);
        m.handle(sel);
        Settle(m);
        assert(!m.combobox_open[cb]);
        assert(m.get_combobox_selected(cb) == 2);
        assert(m.was_clicked(cb));
        assert(std::strcmp(m.pool[cb].text, "r2") == 0);
    }

    // ─── Tap field while open closes silently (toggle shut) ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        InputState shut = FreshInput();
        Tap(shut, 30.0f, 40.0f); // own field
        m.handle(shut);
        Settle(m);
        assert(!m.combobox_open[cb]);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!m.was_clicked(cb));
    }

    // ─── Outside tap cancels (no change, no click on combo) ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        InputState out = FreshInput();
        Tap(out, 700.0f, 600.0f); // far outside
        m.handle(out);
        Settle(m);
        assert(!m.combobox_open[cb]);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!m.was_clicked(cb));
    }

    // ─── Escape closes ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        assert(m.combobox_open[cb]);
        InputState esc = FreshInput();
        esc.keys_just_pressed[static_cast<size_t>(KeyCode::Escape)] = true;
        m.handle(esc);
        Settle(m);
        assert(!m.combobox_open[cb]);
    }

    // ─── Opening scrolls selection into view ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        Seed(m, cb, 40.0f);
        m.combobox_selected[cb] = 12;
        std::strncpy(m.pool[cb].text, "r12", sizeof(m.pool[cb].text) - 1);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        // 12 - 6 + 1 = 7
        assert(Near(m.combobox_scroll[cb], 7.0f));
    }

    // ─── Wheel scrolls open popup + clamps ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        ui::ListboxMetrics mt = Seed(m, cb, 40.0f);
        (void)mt;
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        f32 qy = 20.0f + 40.0f;
        InputState wh = FreshInput();
        wh.mouse_x = 30.0f;
        wh.has_pointer = true; // a simulated pointer must declare itself
        wh.mouse_y = qy + 10.0f;
        wh.mouse_scroll_dy = -1.0f;
        m.handle(wh);
        // The wheel sets the TARGET; the drawn value eases toward it, so it is
        // strictly between 0 and 3 on this frame (an instant jump used to read
        // as stutter).
        assert(Near(m.combobox_scroll_target[cb], 3.0f));
        assert(Near(m.combobox_scroll[cb], 0.0f)); // not drawn yet: update() owns motion
        m.update(1.0f / 60.0f);
        assert(m.combobox_scroll[cb] > 0.0f && m.combobox_scroll[cb] < 3.0f);
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(Near(m.combobox_scroll[cb], 3.0f));
        InputState wh2 = FreshInput();
        wh2.mouse_x = 30.0f;
        wh2.has_pointer = true; // a simulated pointer must declare itself
        wh2.mouse_y = qy + 10.0f;
        wh2.mouse_scroll_dy = -10.0f;
        m.handle(wh2);
        assert(Near(m.combobox_scroll_target[cb], 10.0f)); // 16 - 6
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(Near(m.combobox_scroll[cb], 10.0f));
    }

    // ─── dt == 0 is a no-op (Plan B) ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        (void)Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        assert(Near(m.combobox_anim[cb], 0.0f)); // opens at 0, animates in
        m.update(0.0f);
        assert(Near(m.combobox_anim[cb], 0.0f));
        m.update(1.0f / 60.0f);
        assert(m.combobox_anim[cb] > 0.0f && m.combobox_anim[cb] < 1.0f);
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(Near(m.combobox_anim[cb], 1.0f));
        assert(m.combobox_open[cb]);
    }

    // ─── Close animates out, then actually flips open=false ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        (void)Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(Near(m.combobox_anim[cb], 1.0f));

        // Outside tap: fully open, so it animates shut (not a snap).
        InputState out = FreshInput();
        Tap(out, 700.0f, 700.0f);
        m.handle(out);
        assert(m.combobox_open[cb] && m.combobox_closing[cb]);
        assert(m.combobox_anim[cb] > 0.9f);
        m.update(1.0f / 60.0f);
        assert(m.combobox_anim[cb] < 1.0f); // shrinking
        assert(m.combobox_open[cb]);         // still drawn while it shrinks
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(Near(m.combobox_anim[cb], 0.0f));
        assert(!m.combobox_open[cb] && !m.combobox_closing[cb]);
    }

    // ─── A tap during the OPEN animation snaps shut (no shrink bounce) ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        (void)Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        m.update(1.0f / 60.0f); // mid-open
        assert(m.combobox_anim[cb] < 1.0f);
        InputState out = FreshInput();
        Tap(out, 700.0f, 700.0f);
        m.handle(out);
        assert(!m.combobox_open[cb]);
        assert(Near(m.combobox_anim[cb], 0.0f));
    }

    // ─── popup_anim_mode: every mode grows out of the field edge ───
    // popup_anim_apply is a pure rect transform (no Renderer), so the modes
    // are checked by GEOMETRY, not by pixels.
    {
        const ui::popup_anim_mode modes[] = {
            ui::popup_anim_mode::SCALE_FADE, ui::popup_anim_mode::GARAGE, ui::popup_anim_mode::SLIDE, ui::popup_anim_mode::FADE,
        };
        // Opens `cb` and returns its settled popup rect.
        auto settled_rect = [&](ui::Manager &m, u16 cb, f32 &rx, f32 &ry, f32 &rw, f32 &rh) {
            InputState open = FreshInput();
            Tap(open, 30.0f, 40.0f);
            m.handle(open);
            for (int i = 0; i < 120; ++i) {
                m.update(1.0f / 60.0f);
            }
            m.combobox_popup_rect(cb, rx, ry, rw, rh);
        };
        for (ui::popup_anim_mode mode : modes) {
            // 1) Settled = identity for every mode.
            {
                ui::Manager m;
                m.init();
                u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
                (void)Seed(m, cb, 40.0f);
                m.set_popup_anim(cb, mode);
                assert(m.get_popup_anim(cb) == mode);
                f32 rx, ry, rw, rh;
                settled_rect(m, cb, rx, ry, rw, rh);
                f32 qx = rx, qy = ry, qw = rw, qh = rh;
                ui::combobox::popup_anim_apply(m, cb, qx, qy, qw, qh);
                assert(Near(qx, rx) && Near(qy, ry) && Near(qw, rw) && Near(qh, rh));
            }
            // 2) Mid-animation: the mode does its own thing, anchored on the
            //    field side (top edge, because this popup is below the field).
            {
                ui::Manager m;
                m.init();
                u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
                (void)Seed(m, cb, 40.0f);
                m.set_popup_anim(cb, mode);
                InputState open = FreshInput();
                Tap(open, 30.0f, 40.0f);
                m.handle(open);
                m.update(1.0f / 60.0f); // one frame in
                f32 rx, ry, rw, rh;
                m.combobox_popup_rect(cb, rx, ry, rw, rh);
                f32 qx = rx, qy = ry, qw = rw, qh = rh;
                ui::combobox::popup_anim_apply(m, cb, qx, qy, qw, qh);
                switch (mode) {
                case ui::popup_anim_mode::SLIDE:
                    assert(qy > ry + 0.5f);     // pushed down out of the field
                    assert(Near(qh, rh));        // size unchanged
                    break;
                case ui::popup_anim_mode::GARAGE:
                    assert(qh < rh - 0.5f);     // door still rolled out
                    assert(qh > 0.0f);
                    assert(Near(qy, ry));        // anchored at the top edge
                    assert(Near(qw, rw));        // width never changes
                    break;
                case ui::popup_anim_mode::FADE:
                    assert(Near(qx, rx) && Near(qy, ry) && Near(qw, rw) && Near(qh, rh));
                    break;
                default:                        // SCALE_FADE
                    assert(qh < rh - 0.5f && qw < rw - 0.5f);
                    assert(Near(qy, ry));
                    break;
                }
                // 3) Runs to completion and lands exactly back on the rect.
                for (int i = 0; i < 120; ++i) {
                    m.update(1.0f / 60.0f);
                }
                f32 sx, sy, sw, sh;
                m.combobox_popup_rect(cb, sx, sy, sw, sh);
                f32 ex = sx, ey = sy, ew = sw, eh = sh;
                ui::combobox::popup_anim_apply(m, cb, ex, ey, ew, eh);
                assert(Near(ex, sx) && Near(ey, sy) && Near(ew, sw) && Near(eh, sh));
            }
        }
    }

    // ─── Unknown widget id is safe (setter/getter bounds) ───
    {
        ui::Manager m;
        m.init();
        m.set_popup_anim(9999, ui::popup_anim_mode::GARAGE);
        assert(m.get_popup_anim(9999) == ui::popup_anim_mode::SCALE_FADE);
    }

    // ─── A recycled slot does not inherit a popup animation mode ──────
    // Deliberately per WIDGET and not per style - it is behaviour, not skin,
    // and a style-level slot would force one mode on every combo sharing it.
    // Which means combobox() does not write it, so only alloc() can: a combo
    // landing on the slot of a GARAGE combo silently opened with SCALE_FADE.
    {
        ui::Manager m;
        m.init();
        const u16 a = m.combobox(0.0f, 0.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        assert(m.get_popup_anim(a) == ui::popup_anim_mode::SCALE_FADE); // the factory default
        m.set_popup_anim(a, ui::popup_anim_mode::GARAGE);
        assert(m.get_popup_anim(a) == ui::popup_anim_mode::GARAGE);
        m.clear();
        const u16 b = m.combobox(0.0f, 0.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        assert(b == a); // the same slot came back
        assert(m.get_popup_anim(b) == ui::popup_anim_mode::SCALE_FADE);
    }

    // ─── Flick inertia: target keeps moving after the drag ends ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        (void)Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        // Hand-built velocity: no drag needed, the target is moving.
        m.combobox_scroll_target[cb] = 1.0f;
        m.combobox_scroll[cb]        = 0.0f;
        m.update(1.0f / 60.0f); // one frame of "dragging" builds velocity
        m.combobox_scroll_vel[cb] = 60.0f;
        f32 before = m.combobox_scroll_target[cb];
        m.update(1.0f / 60.0f);
        assert(m.combobox_scroll_target[cb] > before); // kept going
        assert(m.combobox_scroll_vel[cb] < 60.0f);      // and decayed
        // Eventually stops, and clamps at maxscroll instead of running off.
        for (int i = 0; i < 600; ++i) {
            m.update(1.0f / 60.0f);
        }
        f32 maxsc = ui::Manager::combobox_maxscroll(static_cast<u8>(kCount));
        assert(Near(m.combobox_scroll[cb], maxsc) || __builtin_fabsf(m.combobox_scroll[cb] - maxsc) < 0.01f);
        assert(m.combobox_scroll_vel[cb] == 0.0f);
    }

    // ─── Keyboard: arrows highlight only (no select, no callback) ───
    {
        ui::Manager m;
        m.init();
        g_fired = false;
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, FireCb);
        Seed(m, cb, 40.0f);
        m.focus_id = cb;
        InputState dn = FreshInput();
        dn.action_count = 1;
        dn.actions[0] = InputAction::MenuDown;
        m.handle(dn);
        assert(m.combobox_open[cb]); // arrows auto-open...
        assert(m.combobox_hl[cb] == 0); // ...and highlight row 0...
        assert(m.get_combobox_selected(cb) == -1); // ...without selecting...
        assert(m.pool[cb].text[0] == '\0'); // ...or touching the label...
        assert(!g_fired); // ...or firing the callback
        assert(m.focus_id == cb);
        for (int k = 0; k < 8; ++k) {
            InputState ik = FreshInput();
            ik.action_count = 1;
            ik.actions[0] = InputAction::MenuDown;
            m.handle(ik);
        }
        assert(m.combobox_hl[cb] == 8);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!g_fired);
        assert(Near(m.combobox_scroll_target[cb], 3.0f)); // 8 - 6 + 1, autoscrolled
        for (int k = 0; k < 120; ++k) {
            m.update(1.0f / 60.0f);
        }
        assert(Near(m.combobox_scroll[cb], 3.0f));
        // Enter commits the highlight and shuts.
        InputState cf = FreshInput();
        cf.action_count = 1;
        cf.actions[0] = InputAction::Confirm;
        m.handle(cf);
        Settle(m);
        assert(!m.combobox_open[cb]);
        assert(m.get_combobox_selected(cb) == 8);
        assert(std::strcmp(m.pool[cb].text, "r8") == 0);
        assert(g_fired);
        assert(m.was_clicked(cb));
    }

    // ─── Enter on closed popup just opens (silent) ───
    {
        ui::Manager m;
        m.init();
        g_fired = false;
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, FireCb);
        Seed(m, cb, 40.0f);
        m.focus_id = cb;
        InputState cf = FreshInput();
        cf.action_count = 1;
        cf.actions[0] = InputAction::Confirm;
        m.handle(cf);
        assert(m.combobox_open[cb]);
        assert(!g_fired);
        assert(!m.was_clicked(cb));
    }

    // ─── Editable filter narrows + select restores label ───
    {
        ui::Manager m;
        m.init();
        static const char* words[] = {"apple", "apricot", "banana", "blueberry"};
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, words, 4, true, NoopCb);
        Seed(m, cb, 40.0f);
        m.focus_id = cb;
        InputState t = FreshInput();
        t.text_input[0] = 'a';
        t.text_input[1] = 'p';
        t.text_count = 2;
        m.handle(t);
        assert(m.combobox_open[cb]); // typing auto-opens
        assert(m.combobox_filtering[cb]);
        assert(m.combobox_visible_count(cb) == 2); // apple + apricot
        assert(m.combobox_visible_to_item(cb, 0) == 0);
        assert(m.combobox_visible_to_item(cb, 1) == 1);
        assert(m.combobox_item_to_visible(cb, 2) == -1); // banana filtered out
        assert(m.combobox_hl[cb] == 0); // first match ready for Enter
        // Select first visible row.
        f32 qy = 20.0f + 40.0f;
        ui::ListboxMetrics mt = m.combobox_metrics[cb];
        InputState sel = FreshInput();
        Tap(sel, 30.0f, qy + mt.row_h * 0.5f);
        m.handle(sel);
        assert(m.get_combobox_selected(cb) == 0);
        assert(std::strcmp(m.pool[cb].text, "apple") == 0);
        assert(!m.combobox_filtering[cb]); // commit drops the filter
        // Reopen: full list again, not the one-row "apple" filter.
        InputState reopen = FreshInput();
        Tap(reopen, 30.0f, 40.0f);
        m.handle(reopen);
        assert(m.combobox_open[cb]);
        assert(m.combobox_visible_count(cb) == 4);
        assert(std::strcmp(m.pool[cb].text, "apple") == 0); // label kept
        assert(m.combobox_hl[cb] == 0); // selection highlighted
        // Typing a non-match clears selection; popup shows "No match".
        InputState t2 = FreshInput();
        t2.text_input[0] = 'z';
        t2.text_count = 1;
        m.handle(t2);
        assert(m.combobox_visible_count(cb) == 0);
        assert(m.get_combobox_selected(cb) == -1);
        assert(m.combobox_hl[cb] == -1);
        // Tapping the empty popup shuts it without selecting.
        InputState empty = FreshInput();
        Tap(empty, 30.0f, qy + mt.row_h * 0.5f);
        m.handle(empty);
        Settle(m);
        assert(!m.combobox_open[cb]);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!m.was_clicked(cb));
    }

    // ─── Cancel restores the label (typed filter is dropped) ───
    {
        ui::Manager m;
        m.init();
        static const char* words[] = {"apple", "apricot", "banana"};
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, words, 3, true, NoopCb);
        Seed(m, cb, 40.0f);
        m.combobox_selected[cb] = 2;
        std::strncpy(m.pool[cb].text, "banana", sizeof(m.pool[cb].text) - 1);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        InputState t = FreshInput();
        t.text_input[0] = 'a';
        t.text_count = 1;
        m.handle(t);
        assert(m.combobox_filtering[cb]);
        InputState esc = FreshInput();
        esc.keys_just_pressed[static_cast<size_t>(KeyCode::Escape)] = true;
        m.handle(esc);
        Settle(m);
        assert(!m.combobox_open[cb]);
        assert(!m.combobox_filtering[cb]);
        assert(std::strcmp(m.pool[cb].text, "banana") == 0); // label restored
        assert(m.get_combobox_selected(cb) == 2);
    }

    // ─── Overlay steals finger_down from widgets underneath ───
    {
        ui::Manager m;
        m.init();
        u16 top = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        u16 low = m.combobox(10.0f, 120.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        Seed(m, top, 40.0f);
        Seed(m, low, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f); // open the top popup (covers the low field)
        m.handle(open);
        assert(m.is_combobox_open(top));
        InputState grab = FreshInput();
        grab.touch.on_touch_down(0, 30.0f, 140.0f); // inside popup, over low field
        grab.touch.fingers[0].type = GestureType::Drag; // white-box
        m.handle(grab);
        assert(m.active == top); // popup wins, not the low field underneath
    }

    // ─── Popup flips above when below overflows ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 500.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        ui::ListboxMetrics mt = Seed(m, cb, 40.0f);
        m.view_w = 900.0f;
        m.view_h = 600.0f;
        InputState noop = FreshInput();
        m.handle(noop); // rebuilds the abs cache (no-op input)
        f32 px, py, pw, ph;
        assert(m.combobox_popup_rect(cb, px, py, pw, ph));
        assert(Near(ph, 6.0f * mt.row_h));
        assert(Near(py, 500.0f - ph)); // flipped above
        // Roomy view: popup below.
        m.view_h = 2000.0f;
        assert(m.combobox_popup_rect(cb, px, py, pw, ph));
        assert(Near(py, 540.0f));
    }

    // ─── Body drag scrolls popup ───
    {
        ui::Manager m;
        m.init();
        u16 cb = m.combobox(10.0f, 20.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb);
        ui::ListboxMetrics mt = Seed(m, cb, 40.0f);
        InputState open = FreshInput();
        Tap(open, 30.0f, 40.0f);
        m.handle(open);
        f32 qy = 20.0f + 40.0f;
        InputState in = FreshInput();
        in.touch.on_touch_down(0, 30.0f, qy + 10.0f);
        in.touch.fingers[0].type = GestureType::Drag; // white-box: skip swipe classification
        m.handle(in);
        f32 dy = -2.0f * mt.row_h;
        in.touch.on_touch_move(0, 30.0f, qy + 10.0f + dy);
        m.handle(in);
        // Target tracks the finger exactly; the drawn value eases toward it.
        assert(Near(m.combobox_scroll_target[cb], 2.0f));
        m.update(1.0f / 60.0f);
        assert(m.combobox_scroll[cb] > 0.0f && m.combobox_scroll[cb] < 2.0f);
        Settle(m);
        assert(Near(m.combobox_scroll[cb], 2.0f));
    }

    // ─── is_text_capture: hotkeys must yield to live text input ───
    {
        ui::Manager m;
        m.init();
        assert(!m.is_text_capture());
        static const char* words[] = {"apple", "banana"};
        u16 ed = m.combobox(0.0f, 0.0f, 240.0f, 40.0f, words, 2, true, NoopCb);
        u16 ro = m.combobox(0.0f, 60.0f, 240.0f, 40.0f, words, 2, false, NoopCb);
        assert(!m.is_text_capture()); // closed + unfocused: hotkeys live
        m.focus_id = ed;
        assert(m.is_text_capture()); // focused-closed: typing would auto-open
        m.focus_id = UINT16_MAX;
        m.combobox_open[ro] = true;
        assert(!m.is_text_capture()); // select-only popup eats no typing
        m.combobox_open[ro] = false;
        m.combobox_open[ed] = true;
        assert(m.is_text_capture()); // open filter eats typing
        m.combobox_open[ed] = false;
        u16 tf = m.textfield(0.0f, 120.0f, 200.0f, 40.0f, "", 0xFF000000, 0xFFFFFFFF);
        m.editing_id = tf;
        assert(m.is_text_capture());
        m.editing_id = UINT16_MAX;
        assert(!m.is_text_capture());
    }

    // ─── Typing in the filter does not walk the focus ring away ───
    // W and S are MenuUp/MenuDown as well as letters, so one keystroke arrives
    // as both, and the arrow half looks like it should steal focus. It does
    // not: a focused ComboBox takes the highlight branch above and `continue`s
    // before the generic focus move. This test is the pin for that, so the
    // invariant cannot be broken by a reordering.
    {
        ui::Manager m;
        m.init();
        static const char *const kEd[3] = {"Easy", "Medium", "Hard"};
        u16 tf  = m.textfield(0.0f, 0.0f, 200.0f, 40.0f, "", 0xFF000000, 0xFFFFFFFF);
        u16 cb  = m.combobox(0.0f, 60.0f, 200.0f, 40.0f, kEd, 3, true, NoopCb);
        u16 tf2 = m.textfield(0.0f, 120.0f, 200.0f, 40.0f, "", 0xFF000000, 0xFFFFFFFF);
        (void)tf;
        (void)tf2;
        m.focus_id = cb;
        assert(m.is_text_capture()); // focused editable combo

        // 's' is both a letter and MenuDown.
        InputState in;
        in.init();
        in.text_count = 1;
        in.text_input[0] = 's';
        in.action_count = 1;
        in.actions[0] = InputAction::MenuDown;
        m.handle(in);
        assert(m.is_text_capture()); // still capturing
        assert(m.focus_id == cb);    // and the focus never left
        assert(std::strcmp(m.text_of(cb), "s") == 0); // the letter went in

        // Same for the arrow half of a plain arrow key.
        InputState up;
        up.init();
        up.action_count = 1;
        up.actions[0] = InputAction::MenuUp;
        m.handle(up);
        assert(m.focus_id == cb);
    }

    // ─── A HIDDEN combobox must not eat input, and must not claim the keyboard ──
    // 7 handle() sites read the widget's OWN WF_VISIBLE / WF_ENABLED, so a combo
    // inside a hidden panel still swallowed keystrokes and wheel ticks. The worst
    // one is this pair: a hidden EDITABLE combo reports text capture, so the host
    // concludes it is mid-edit and skips its own hotkeys - silently, with nothing
    // on screen to explain it.
    //
    // The own flag is left set on purpose (derived state, not propagated), so the
    // only way to distinguish "hidden" from "visible" is the EFFECTIVE state.
    {
        ui::Manager m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 300.0f, 200.0f, 0xFF222222);
        const u16 cb    = m.combobox(10.0f, 10.0f, 240.0f, 40.0f, kItems, kCount, true /*editable*/, NoopCb, panel);
        assert(!m.is_text_capture()); // visible + closed: not capturing

        // Focus it FIRST, then hide the panel. The order matters and it is the
        // order that happens in an app: the user tabs to a combo, the screen
        // hides the row it lives in, and focus is never cleared - so a FOCUSED
        // hidden combo is the reachable state. (Hiding first could not reach
        // the branch at all, because focus nav already uses effective state.)
        InputState nav = FreshInput();
        nav.action_count = 1;
        nav.actions[0]  = InputAction::FocusNext;
        m.handle(nav);
        assert(m.focus_id == cb);

        // Open the popup too, so BOTH editable-filter paths are covered: the
        // "topmost open popup wins" loop and the focused-combobox fallback.
        InputState open = FreshInput();
        Tap(open, 130.0f, 30.0f);
        m.handle(open);
        assert(m.combobox_open[cb]);

        m.set_visible(panel, false);
        assert(m.pool[cb].flags & ui::WF_VISIBLE); // own flag untouched
        assert(m.pool[cb].flags & ui::WF_ENABLED);
        assert(!m.is_visible(cb));
        assert(m.focus_id == cb); // ...and focus is still on it

        // THE assertion: a hidden editable combo must not report text capture,
        // or the host skips every hotkey for a widget nobody can see.
        assert(!m.is_text_capture());

        // ...and the keystroke must reach the host instead of the hidden filter.
        InputState in;
        in.init();
        in.text_count = 1;
        in.text_input[0] = 'q';
        m.handle(in);
        assert(!m.combobox_filtering[cb]);         // not captured
        assert(std::strcmp(m.text_of(cb), "q") != 0); // and not typed into it
    }

    // ─── ...and a hidden combobox must not answer a tap or a wheel tick ──
    {
        ui::Manager m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 300.0f, 200.0f, 0xFF222222);
        const u16 cb    = m.combobox(10.0f, 10.0f, 240.0f, 40.0f, kItems, kCount, true, NoopCb, panel);
        // Open it first: the wheel path and the Select pre-pass both skip a closed
        // combo, so a closed one would prove nothing.
        InputState open = FreshInput();
        Tap(open, 130.0f, 30.0f);
        m.handle(open);
        assert(m.combobox_open[cb]);
        Seed(m, cb, 40.0f); // non-trivial row metrics + scroll range
        const f32 scrolled = m.combobox_scroll[cb];

        m.set_visible(panel, false);

        // Wheel over it: a hidden popup must not swallow the tick, because a
        // listbox UNDERNEATH it is a legitimate target for the same wheel.
        // The point has to be INSIDE the popup rect, which opens BELOW the
        // field - a wheel at the field's own y misses the popup entirely, so the
        // test would pass against the bug.
        f32 qx = 0.0f, qy = 0.0f, qw = 0.0f, qh = 0.0f;
        assert(m.combobox_popup_rect(cb, qx, qy, qw, qh));
        assert(qh > 1.0f);
        InputState w = FreshInput();
        w.mouse_x = qx + qw * 0.5f;
        w.mouse_y = qy + qh * 0.5f;
        w.has_pointer = true;
        w.mouse_scroll_dy = -1.0f;
        m.handle(w);
        assert(Near(m.combobox_scroll[cb], scrolled));        // drawn value
        assert(Near(m.combobox_scroll_target[cb], scrolled)); // and the target it eases from

        // A tap on it must not commit a row or close it.
        InputState t = FreshInput();
        Tap(t, 130.0f, 30.0f);
        m.handle(t);
        assert(m.combobox_open[cb]); // still open: the tap did not reach it
    }

    // ─── Focus NAVIGATION is NOT the same question as "is it usable" ───
    // A hidden widget cannot be FOCUSED, because focus nav already uses the
    // effective state. So the assertion above cannot be reached by hiding the
    // combo alone - it needs the ancestor. That is also why making
    // is_text_capture() effective is a separate fix from the focus nav: the two
    // disagree today, and the focus ring is right.
    {
        ui::Manager m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 300.0f, 200.0f, 0xFF222222);
        m.button(10.0f, 10.0f, 80.0f, 30.0f, "before", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, panel);
        m.combobox(10.0f, 60.0f, 240.0f, 40.0f, kItems, kCount, true, NoopCb, panel);
        m.set_visible(panel, false);
        InputState in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::FocusNext;
        m.handle(in);
        assert(m.focus_id == UINT16_MAX); // nothing focusable inside a hidden panel
    }

    // ─── Hiding a panel leaves an OPEN popup's state alone ───────────
    // The change above makes a hidden popup inert, and that is the whole point.
    // But "inert" must not mean "forgotten": the popup is still marked open, and
    // an app that re-shows the panel expects it back. If a fix had closed it,
    // the widget would come back silently dismissed.
    {
        ui::Manager m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 300.0f, 300.0f, 0xFF222222);
        const u16 cb    = m.combobox(10.0f, 10.0f, 240.0f, 40.0f, kItems, kCount, false, NoopCb, panel);
        InputState     open = FreshInput();
        Tap(open, 130.0f, 30.0f);
        m.handle(open);
        assert(m.combobox_open[cb]);

        m.set_visible(panel, false);
        for (int i = 0; i < 30; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.combobox_open[cb]); // still open, just not reachable
        assert(!m.is_visible(cb));

        m.set_visible(panel, true);
        // Reachable again: a tap inside the popup now lands on it, which it would
        // not if the open flag had been cleared behind our back.
        InputState t = FreshInput();
        Tap(t, 130.0f, 60.0f);
        m.handle(t);
        assert(!m.combobox_open[cb]); // the tap closed it, so it WAS reachable
    }

    // ─── Tab walks focus PAST a combo, it does not walk its ITEMS ───
    // This is the reported bug, in the shape it was reported: a TextField and a
    // 3-item ComboBox, "how many Tabs to get back to the TextField?". The answer
    // used to be "never". Tab arrived as MenuDown, the ComboBox branch matched
    // MenuDown, it moved the popup highlight and CLAMPED at the last item, and
    // `continue`d - so the focus ring had no way out and neither did the popup.
    {
        static const char *three[] = {"one", "two", "three"};
        ui::Manager       m;
        m.init();
        const u16 tf = m.textfield(10.0f, 10.0f, 200.0f, 32.0f, "name", 0xFFFFFFFF, 0xFF888888);
        const u16 cb = m.combobox(10.0f, 60.0f, 240.0f, 40.0f, three, 3, false, NoopCb);
        Seed(m, cb, 40.0f);

        // The focus ring here is exactly [tf, cb], and focus starts empty, so
        // Tab #1 lands on the TextField and Tab #2 on the ComboBox. ONE Tab from
        // the combo returns to the field - that is the answer to the question
        // this test exists for. Before the split it was: never, because Tab
        // arrived as MenuDown, the ComboBox moved its highlight instead, clamped
        // at item 3 and `continue`d.
        InputState k1 = FreshInput();
        k1.action_count = 1;
        k1.actions[0]   = InputAction::FocusNext;
        m.handle(k1);
        assert(m.focus_id == tf);

        InputState k2 = FreshInput();
        k2.action_count = 1;
        k2.actions[0]   = InputAction::FocusNext;
        m.handle(k2);
        assert(m.focus_id == cb);
        assert(!m.combobox_open[cb]); // Tab must NOT open the dropdown
        assert(m.combobox_hl[cb] == -1);

        // Tab #3: straight back out. Still not open, still no highlight moved,
        // and focus is home - the whole point of the split. Two more presses for
        // the highlight would have been the old behaviour, clamped forever after.
        InputState k3 = FreshInput();
        k3.action_count = 1;
        k3.actions[0]   = InputAction::FocusNext;
        m.handle(k3);
        assert(m.focus_id == tf);
        assert(!m.combobox_open[cb]);
        assert(m.combobox_hl[cb] == -1);
        assert(!m.nav_consumed_this_frame()); // the key is the focus ring's, not the widget's

        // Shift-Tab walks the same ring backwards, so neither direction traps.
        InputState k4 = FreshInput();
        k4.action_count = 1;
        k4.actions[0]   = InputAction::FocusPrev;
        m.handle(k4);
        assert(m.focus_id == cb);
        InputState k5 = FreshInput();
        k5.action_count = 1;
        k5.actions[0]   = InputAction::FocusPrev;
        m.handle(k5);
        assert(m.focus_id == tf);

        // ...and the same ring, driven by the ACTUAL KEY rather than by an
        // action injected by hand. If the keymap ever folds Tab back into
        // MenuDown, this is the assertion that notices - every block above would
        // still be green.
        RealTab(m);
        assert(m.focus_id == cb);
        RealTab(m); // ONE press, back to the field. Not three, not never.
        assert(m.focus_id == tf);
        assert(!m.combobox_open[cb]);
        RealTab(m, /*shift=*/true); // Shift-Tab from the field wraps to the combo
        assert(m.focus_id == cb);
    }

    // ─── The arrow contract is MenuUp/MenuDown ONLY - Swap* is not an arrow ───
    // SwapUp/SwapDown used to be what Tab emitted, which is the whole reason this
    // file's bug existed. They are now producerless, so leaving them in a widget
    // condition is invisible - a revert that re-adds them cannot be caught by any
    // behavioural test, because nothing emits them (confirmed by revert-check:
    // VACUOUS). So pin the exclusion directly instead: a ComboBox must ignore
    // them outright rather than "happen not to" be sent them.
    {
        static const char *three[] = {"one", "two", "three"};
        ui::Manager       m;
        m.init();
        const u16 cb = m.combobox(10.0f, 60.0f, 240.0f, 40.0f, three, 3, false, NoopCb);
        Seed(m, cb, 200.0f);
        m.focus_id = cb;
        for (InputAction a : {InputAction::SwapUp, InputAction::SwapDown}) {
            InputState in = FreshInput();
            in.action_count = 1;
            in.actions[0]   = a;
            m.handle(in);
            assert(!m.combobox_open[cb]);        // not an "open the list" key
            assert(m.combobox_hl[cb] == -1);     // not a "move the highlight" key
            assert(!m.nav_consumed_this_frame()); // and not the widget's to swallow
            assert(m.focus_id == cb);             // focus did not walk either
        }
    }

    // ─── Tab on an OPEN popup cancels it and leaves - it must not commit ───
    {
        static const char *three[] = {"one", "two", "three"};
        ui::Manager       m;
        m.init();
        const u16 tf = m.textfield(10.0f, 10.0f, 200.0f, 32.0f, "name", 0xFFFFFFFF, 0xFF888888);
        const u16 cb = m.combobox(10.0f, 60.0f, 240.0f, 40.0f, three, 3, false, FireCb);
        // Tall enough that all 3 rows are visible: hl must be able to reach 2, or
        // "a commit would have been observable" is not actually established.
        Seed(m, cb, 200.0f);

        PressTab(m, 2); // empty -> tf -> cb
        assert(m.focus_id == cb);

        // Space/Enter opens it and moves the highlight down twice, so "the popup
        // had a highlighted item" is true and a commit would be observable.
        InputState open = FreshInput();
        open.action_count = 1;
        open.actions[0]   = InputAction::Confirm;
        m.handle(open);
        assert(m.combobox_open[cb]);
        for (int i = 0; i < 2; ++i) {
            InputState dn = FreshInput();
            dn.action_count = 1;
            dn.actions[0]   = InputAction::MenuDown;
            m.handle(dn);
        }
        // hl lands on 1, not 2: opening sets hl to the SELECTED row (-1 when
        // nothing is selected), so the first arrow press moves it to 0.
        assert(m.combobox_hl[cb] == 1);
        assert(m.get_combobox_selected(cb) == -1);

        g_fired = false;
        InputState tab = FreshInput();
        tab.action_count = 1;
        tab.actions[0]   = InputAction::FocusNext;
        m.handle(tab);

        // CANCEL, not commit: the player opened the list, moved the highlight and
        // then left. Committing would change the value on the way out the door,
        // and the only key that means "yes, this one" is Enter.
        assert(m.focus_id == tf);                    // focus walked on
        assert(!m.combobox_open[cb]);                // popup shut
        assert(m.get_combobox_selected(cb) == -1);   // nothing was chosen
        assert(!g_fired);                            // and no callback fired

        Settle(m);
        assert(!m.combobox_open[cb]);                // shut all the way, not mid-animation
    }

    // ─── A DISABLED combo is inert: no focus, no popup, no reaction ───
    // is_enabled is the EFFECTIVE state, so disabling the PANEL is the case that
    // matters and the one an app actually hits. Confirms a disabled Button does
    // not fire on Enter either - that path had no enabled check at all.
    {
        static const char *three[] = {"one", "two", "three"};
        ui::Manager       m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 300.0f, 200.0f, 0xFF222222);
        const u16 tf    = m.textfield(10.0f, 10.0f, 200.0f, 32.0f, "name", 0xFFFFFFFF, 0xFF888888);
        const u16 cb    = m.combobox(10.0f, 60.0f, 240.0f, 40.0f, three, 3, false, FireCb, panel);
        Seed(m, cb, 40.0f);

        PressTab(m, 1);
        assert(m.focus_id == tf);
        PressTab(m, 1);
        assert(m.focus_id == cb);

        // Open it while it still works, so "closed by the disable" is observable
        // rather than indistinguishable from "never opened".
        InputState open = FreshInput();
        open.action_count = 1;
        open.actions[0]   = InputAction::Confirm;
        m.handle(open);
        assert(m.combobox_open[cb]);

        m.set_enabled(panel, false);
        assert(m.pool[cb].flags & ui::WF_ENABLED); // own flag untouched
        assert(!m.is_enabled(cb));                 // ...but effectively dead

        InputState probe = FreshInput();
        probe.action_count = 1;
        probe.actions[0]   = InputAction::FocusNext;
        m.handle(probe);

        assert(m.focus_id == tf);                  // focus dropped, not parked
        assert(!m.combobox_open[cb]);              // popup shut
        g_fired = false;

        // Arrows and Enter on the dead widget do nothing at all. focus_id is the
        // TextField now, so this proves the ring never came back.
        for (int i = 0; i < 4; ++i) {
            InputState dn = FreshInput();
            dn.action_count = 1;
            dn.actions[0]   = InputAction::MenuDown;
            m.handle(dn);
            InputState cf = FreshInput();
            cf.action_count = 1;
            cf.actions[0]   = InputAction::Confirm;
            m.handle(cf);
        }
        assert(m.focus_id != cb);
        assert(!m.combobox_open[cb]);
        assert(!g_fired);

        // Re-enabling does NOT hand focus or the popup back: both were dropped on
        // purpose, and a control that quietly reclaims the keyboard greys out
        // and comes back is a worse surprise than one that needs a fresh Tab.
        m.set_enabled(panel, true);
        m.handle(FreshInput());
        assert(m.focus_id == tf);
        assert(!m.combobox_open[cb]);
    }

    // ─── Home / End jump to the first / last row, and OPEN the popup ───
    // Opening on Home/End is deliberate and matches the arrows: on a combo the
    // first and last item are not reachable without being seen, so "jump to the
    // end" has to show the end. Neither key SELECTS - that stays on Enter - so a
    // jump that happens to land on a row the player wanted still needs one more
    // key, which is the same rule the arrows follow.
    {
        // kItems has 16, more than K_MAX_POPUP_ROWS (6), so the popup scroll has to
        // follow the jump - a 3-item list would fit entirely and the assertion
        // would pass against a reveal that never scrolled.
        ui::Manager m;
        m.init();
        const u16 cb = m.combobox(10.0f, 60.0f, 240.0f, 40.0f, kItems, kCount, false, FireCb);
        Seed(m, cb, 400.0f);
        m.focus_id = cb;
        g_fired    = false;

        auto key = [&m](InputAction a) {
            InputState in = FreshInput();
            in.action_count = 1;
            in.actions[0]   = a;
            m.handle(in);
        };

        key(InputAction::MenuLast);
        assert(m.combobox_open[cb]);                            // opened...
        assert(m.combobox_hl[cb] == static_cast<int>(kCount) - 1); // ...on the LAST row
        assert(m.get_combobox_selected(cb) == -1);              // ...without selecting it
        assert(m.pool[cb].text[0] == '\0');                    // ...or touching the label
        assert(!g_fired);                                       // ...or firing the callback
        assert(m.focus_id == cb);

        // ...and the popup scrolled so that last row is actually ON SCREEN. Without
        // this the jump is a lie: hl moves to a row the player cannot see.
        assert(m.combobox_scroll_target[cb] > 0.0f);
        const int rows  = ui::Manager::K_MAX_POPUP_ROWS;
        const int first = static_cast<int>(m.combobox_scroll_target[cb]);
        assert(static_cast<int>(kCount) - rows <= first);

        key(InputAction::MenuFirst);
        assert(m.combobox_hl[cb] == 0);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!g_fired);
        assert(Near(m.combobox_scroll_target[cb], 0.0f)); // scrolled back

        // Enter still commits what Home/End highlighted, which is what makes the
        // jump useful rather than decorative.
        key(InputAction::MenuLast);
        InputState enter = FreshInput();
        enter.action_count = 1;
        enter.actions[0]   = InputAction::Confirm;
        m.handle(enter);
        assert(!m.combobox_open[cb]);
        assert(m.get_combobox_selected(cb) == static_cast<int>(kCount) - 1);
        assert(g_fired);
    }

    // ─── F4 / Alt+Down toggle the popup, and closing CANCELS ───
    // The Windows / browser contract, and the reason it needed its own action:
    // Alt+Down arrives as the same physical key as Down, so emitting MenuDown for
    // it too would open the popup and shut it again in one keystroke.
    // Closing CANCELS rather than commits - the same rule as Esc and as Tab, and
    // deliberately different from Enter ("yes, this one"). Asserted here because
    // "toggle" and "commit" feel interchangeable until a player loses a selection.
    {
        ui::Manager m;
        m.init();
        const u16 cb = m.combobox(10.0f, 60.0f, 240.0f, 40.0f, kItems, kCount, false, FireCb);
        Seed(m, cb, 400.0f);
        m.focus_id = cb;
        g_fired    = false;

        auto key = [&m](InputAction a) {
            InputState in = FreshInput();
            in.action_count = 1;
            in.actions[0]   = a;
            m.handle(in);
        };

        // Open on a silent toggle: no selection, no callback, no label change.
        // hl is -1, not 0: open_now highlights the current SELECTION, and there
        // isn't one. The first arrow then lands on row 0 (asserted below), which
        // is the same thing Enter-on-closed does - the toggle does not
        // special-case itself into looking pre-selected.
        key(InputAction::MenuToggle);
        assert(m.combobox_open[cb]);
        assert(m.combobox_hl[cb] == -1);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!g_fired);
        assert(m.pool[cb].text[0] == '\0');

        // Move the highlight somewhere else, then toggle shut: the highlight is
        // discarded and the selection is still -1.
        key(InputAction::MenuDown);
        key(InputAction::MenuDown);
        assert(m.combobox_hl[cb] == 1);
        key(InputAction::MenuToggle);
        Settle(m);
        assert(!m.combobox_open[cb]);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!g_fired);
        assert(m.pool[cb].text[0] == '\0');

        // And it opens again from shut - the toggle is symmetric.
        key(InputAction::MenuToggle);
        assert(m.combobox_open[cb]);

        // The key is FLAGGED, or a host with its own F4 binding double-fires.
        InputState probe = FreshInput();
        probe.action_count = 1;
        probe.actions[0]   = InputAction::MenuToggle;
        m.handle(probe);
        assert(m.nav_consumed_this_frame());
    }

    // ─── PageUp / PageDown move a VIEWPORT, not one row ───
    // A page is what the popup can show (K_MAX_POPUP_ROWS), minus one so the row
    // landed on is still visible without the autoscroll having to rescue it.
    // 16 items, 6 rows -> page 5.
    {
        ui::Manager m;
        m.init();
        const u16 cb = m.combobox(10.0f, 60.0f, 240.0f, 40.0f, kItems, kCount, false, FireCb);
        Seed(m, cb, 400.0f);
        m.focus_id = cb;
        g_fired    = false;

        auto key = [&m](InputAction a) {
            InputState in = FreshInput();
            in.action_count = 1;
            in.actions[0]   = a;
            m.handle(in);
        };

        const int page = static_cast<int>(ui::Manager::K_MAX_POPUP_ROWS) - 1;

        key(InputAction::MenuPageDown); // opens, like the arrows and Home/End
        assert(m.combobox_open[cb]);
        assert(m.combobox_hl[cb] == page);
        assert(m.get_combobox_selected(cb) == -1);
        assert(!g_fired);

        key(InputAction::MenuPageDown);
        assert(m.combobox_hl[cb] == page * 2);
        // The jump has to be ON SCREEN or it is a lie - same requirement as the
        // Home/End test above.
        assert(m.combobox_scroll_target[cb] > 0.0f);

        key(InputAction::MenuPageUp);
        assert(m.combobox_hl[cb] == page);

        // Clamped at both ends, like every other move in this file. Not wrapping:
        // a list that wraps hides its ends.
        key(InputAction::MenuPageUp);
        key(InputAction::MenuPageUp);
        assert(m.combobox_hl[cb] == 0);
        for (int i = 0; i < 8; ++i) {
            key(InputAction::MenuPageDown);
        }
        assert(m.combobox_hl[cb] == static_cast<int>(kCount) - 1);
        assert(Near(m.combobox_scroll_target[cb], m.combobox_maxscroll(static_cast<u8>(kCount))));

        // Still highlight-only: paging never selects.
        assert(m.get_combobox_selected(cb) == -1);
        assert(!g_fired);
    }

    printf("[combobox] all tests passed\n");
    return 0;
}

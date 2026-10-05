// Toggle widget tests — factory defaults, tap flips the state, the thumb slides
// toward it via update(), effective-disabled, thumb texture, events.
// Plain main() + assert(), no framework (matches checkbox_tests).
// Headless: handle() and update() are pure CPU; the thumb itself is drawn in
// Pass 2, which the mm_07 screenshot covers.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

static bool Near(f32 a, f32 b, f32 eps = 0.005f) noexcept {
    return __builtin_fabsf(a - b) < eps;
}

static int g_fire_count = 0;
static void FireCb(u16, void *) noexcept {
    ++g_fire_count;
}

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

// Step until the thumb has (nearly) landed. Never assert an exact frame count on
// an accumulated lerp - 0.1f ten times lands just under 1.0f - so this bounds
// the loop and checks the destination.
static void SettleThumb(ui::Manager &m, u16 id) noexcept {
    for (int i = 0; i < 240; ++i) {
        m.update(1.0f / 60.0f);
    }
    assert(Near(m.pool[id].thumb_pos, m.pool[id].state ? 1.0f : 0.0f, 0.01f));
}

int main() {
    // ─── Factory defaults ───
    // TOGGLE had no test file at all: the thumb slide was only covered
    // indirectly by ui_update_tests and the tap by event_tests.
    {
        ui::Manager m;
        m.init();
        const u16 t = m.toggle(10.0f, 10.0f, 56.0f, 28.0f, "sound", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb);
        assert(t != UINT16_MAX);
        assert(m.pool[t].type == (u8)ui::widget_type::TOGGLE);
        assert(m.pool[t].frame.w == 56.0f && m.pool[t].frame.h == 28.0f);
        assert(m.pool[t].state == 0);
        assert(m.pool[t].thumb_pos == 0.0f); // the factory seeds the slide, not the target
        // The TRACK colour comes from the theme, not from bg_on/bg_off: those two
        // drive the THUMB tint in Pass 1/2. Pin it so that split stays deliberate.
        assert(m.pool[t].bg_color == m.theme.toggle_track_off);
        assert(m.on_color[t] == 0xFF5CB85C);
        assert(m.off_color[t] == 0xFF444444);
        assert(m.pool[t].flags & ui::WF_FOCUSABLE); // toggles ARE in the Tab order
        assert(m.is_enabled(t));

        const u16 on = m.toggle(10.0f, 50.0f, 56.0f, 28.0f, "on", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, true, FireCb);
        assert(m.pool[on].state == 1);
    }

    // ─── Tap flips the state, and the thumb slides to it ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        const u16 t = m.toggle(10.0f, 10.0f, 56.0f, 28.0f, "sound", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb);

        InputState in = FreshInput();
        Tap(in, 38.0f, 24.0f); // centre of the frame
        m.handle(in);
        assert(m.pool[t].state == 1);
        assert(g_fire_count == 1);
        // The slide is driven, not instant: one frame must not land it.
        m.update(1.0f / 60.0f);
        assert(m.pool[t].thumb_pos > 0.0f && m.pool[t].thumb_pos < 1.0f);
        SettleThumb(m, t);

        Tap(in, 38.0f, 24.0f);
        m.handle(in);
        assert(m.pool[t].state == 0);
        SettleThumb(m, t);
        // Fires EVERY time, unlike a radiobox (which is silent on a re-tap).
        assert(g_fire_count == 2);
    }

    // ─── A tap outside does nothing ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        const u16 t = m.toggle(10.0f, 10.0f, 56.0f, 28.0f, "s", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb);
        InputState     in = FreshInput();
        Tap(in, 500.0f, 500.0f);
        m.handle(in);
        assert(m.pool[t].state == 0);
        assert(g_fire_count == 0);
    }

    // ─── Keyboard confirm flips it ───
    {
        ui::Manager m;
        m.init();
        const u16 t = m.toggle(10.0f, 10.0f, 56.0f, 28.0f, "s", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, nullptr);
        InputState     in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::FocusNext; // focus first
        m.handle(in);
        assert(m.focus_id == t);
        in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::Confirm;
        m.handle(in);
        assert(m.pool[t].state == 1);
    }

    // ─── A DISABLED toggle does not respond, and reads disabled all the way down ───
    {
        ui::Manager m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 200.0f, 200.0f, 0xFF222222);
        const u16 t     = m.toggle(10.0f, 10.0f, 56.0f, 28.0f, "s", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb, panel);
        g_fire_count         = 0;

        InputState in = FreshInput();
        Tap(in, 38.0f, 24.0f);
        m.handle(in);
        assert(m.pool[t].state == 1); // the toggle's OWN flag is still set...

        // ...so the point of the test: disabling the PARENT disables the child
        // without touching it, which is exactly what a stored copy cannot express.
        m.set_enabled(panel, false);
        // The child's OWN flag is still set - that is the whole point of deriving
        // the effective state instead of propagating flags downward.
        assert(m.pool[t].flags & ui::WF_ENABLED);
        assert(!m.is_enabled(t)); // ...while the effective state says otherwise
        // A disabled widget must not keep chasing a press target either.
        m.pool[t].press_scale = 0.5f;
        m.update(1.0f);
        assert(Near(m.pool[t].press_scale, 1.0f, 0.01f));

        InputState in2 = FreshInput();
        Tap(in2, 38.0f, 24.0f);
        m.handle(in2);
        assert(m.pool[t].state == 1); // no change: the child is not clickable

        // Re-enabling the parent brings the child back - the case that proves the
        // flag is DERIVED and not propagated.
        m.set_enabled(panel, true);
        assert(m.is_enabled(t));
    }

    // ─── update(0) is a no-op (Plan B guard) ───
    {
        ui::Manager m;
        m.init();
        const u16 t = m.toggle(10.0f, 10.0f, 56.0f, 28.0f, "s", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, true, nullptr);
        m.pool[t].thumb_pos = 0.37f;
        m.update(0.0f);
        assert(m.pool[t].thumb_pos == 0.37f);
    }

    // ─── Click + Change events ───
    {
        ui::Manager m;
        m.init();
        const u16 t = m.toggle(10.0f, 10.0f, 56.0f, 28.0f, "s", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, nullptr);
        InputState     in = FreshInput();
        Tap(in, 38.0f, 24.0f);
        m.handle(in);
        bool click = false, change = false;
        f32 v    = -1.0f;
        for (int i = 0; i < 8; ++i) {
            ui::UiEvent e;
            if (!m.poll_event(e)) {
                break;
            }
            if (e.type == ui::ui_event_type::CLICK && e.id == t) {
                click = true;
            }
            if (e.type == ui::ui_event_type::CHANGE && e.id == t) {
                change = true;
                v      = e.value;
            }
        }
        assert(click);
        assert(change);
        assert(v == 1.0f);
    }

    // ─── A recycled slot does not inherit a toggle's colours ───
    {
        ui::Manager m;
        m.init();
        const u16 a = m.toggle(0.0f, 0.0f, 56.0f, 28.0f, "a", 0xFF111111, 0xFF222222, 0xFFFFFFFF, false, nullptr);
        m.on_color[a] = 0xFFAABBCC;
        m.clear();
        const u16 b = m.toggle(0.0f, 0.0f, 56.0f, 28.0f, "b", 0xFF333333, 0xFF444444, 0xFFFFFFFF, false, nullptr);
        assert(b == a);
        // The factory writes both, so this is a factory-coverage check, not an
        // alloc() one - but it is the cheap way to catch a future factory that
        // forgets on_color (the thumb would then tint with the previous toggle's).
        assert(m.on_color[b] == 0xFF333333);
        assert(m.off_color[b] == 0xFF444444);
    }

    // ─── thumb_tex: the skinning field a textured thumb rides ───
    {
        ui::Manager m;
        m.init();
        ui::WidgetStyle st{};
        const u8  sid = m.register_style(st);
        const u16 t   = m.toggle(0.0f, 0.0f, 56.0f, 28.0f, "t", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, nullptr, UINT16_MAX, sid);
        assert(m.pool[t].style_id == sid);
        // A zero-init style means "no texture", and "no texture" MUST be
        // is_valid() and not handle.id != 0: invalid() is 0xFFFFFFFF, so an `!= 0`
        // test would put the toggle on the textured path with a bogus handle.
        assert(!m.styles[sid].thumb_tex.is_valid());
        assert(m.styles[sid].thumb_tex.handle.id != 0);
    }

    printf("[toggle] all tests passed\n");
    return 0;
}
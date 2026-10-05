// Checkbox widget tests — factory, tap toggle (fires every tap),
// keyboard confirm toggle, outside-tap noop, Click+Change events, removal.
// Plain main() + assert(), no framework.
// Headless: handle() is pure CPU (no font/render needed for selection).
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>

static void NoopCb(u16, void *) noexcept {}

static int g_fired_id = -1;
static int g_fire_count = 0;
static void FireCb(u16 id, void *) noexcept {
    g_fired_id = static_cast<int>(id);
    ++g_fire_count;
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

static void Confirm(InputState& in) noexcept {
    in.action_count = 1;
    in.actions[0] = InputAction::Confirm;
}

int main() {
    // ─── Factory defaults ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        u16 c = m.checkbox(10.0f, 20.0f, "Sfx", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb);
        assert(c != UINT16_MAX);
        assert(m.pool[c].state == 0);
        assert(m.pool[c].frame.w == 28.0f && m.pool[c].frame.h == 28.0f);
        assert(m.on_color[c] == 0xFF5CB85C && m.off_color[c] == 0xFF444444);
        assert(g_fire_count == 0); // factory never fires
        u16 on = m.checkbox(10.0f, 60.0f, "On", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, true, NoopCb);
        assert(m.pool[on].state == 1);
    }

    // ─── Tap toggles + fires every time (unlike radio re-tap) ───
    {
        ui::Manager m;
        m.init();
        g_fired_id = -1;
        g_fire_count = 0;
        u16 c = m.checkbox(10.0f, 20.0f, "Sfx", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb);
        InputState t = FreshInput();
        Tap(t, 10.0f + 14.0f, 20.0f + 14.0f); // center of the 28px box
        m.handle(t);
        assert(m.pool[c].state == 1);
        assert(g_fire_count == 1 && g_fired_id == c);
        assert(m.was_clicked(c));
        InputState t2 = FreshInput();
        Tap(t2, 10.0f + 14.0f, 20.0f + 14.0f); // second tap: back off, fires again
        m.handle(t2);
        assert(m.pool[c].state == 0);
        assert(g_fire_count == 2);
    }

    // ─── Tap emits Click + Change(state) ───
    {
        ui::Manager m;
        m.init();
        u16 c = m.checkbox(10.0f, 20.0f, "Sfx", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb);
        InputState pre = FreshInput();
        pre.mouse_x = 10.0f + 14.0f;
        pre.has_pointer = true; // a simulated pointer must declare itself
        pre.mouse_y = 20.0f + 14.0f;
        m.handle(pre);
        ui::UiEvent drain;
        while (m.poll_event(drain)) {
        }
        InputState t = FreshInput();
        t.mouse_x = 10.0f + 14.0f;
        t.has_pointer = true; // a simulated pointer must declare itself
        t.mouse_y = 20.0f + 14.0f;
        Tap(t, 10.0f + 14.0f, 20.0f + 14.0f);
        m.handle(t);
        assert(m.event_pending() == 2);
        ui::UiEvent e;
        assert(m.poll_event(e) && e.type == ui::ui_event_type::CLICK && e.id == c);
        assert(m.poll_event(e) && e.type == ui::ui_event_type::CHANGE && e.id == c && e.value == 1.0f);
        assert(!m.poll_event(e) && m.event_pending() == 0);
    }

    // ─── Tap outside changes nothing ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        u16 c = m.checkbox(10.0f, 20.0f, "Sfx", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, true, FireCb);
        InputState t = FreshInput();
        Tap(t, 700.0f, 600.0f); // far outside
        m.handle(t);
        assert(m.pool[c].state == 1);
        assert(g_fire_count == 0);
    }

    // ─── Keyboard confirm toggles + fires every time ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        u16 c = m.checkbox(10.0f, 20.0f, "Sfx", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb);
        m.focus_id = c;
        InputState cf = FreshInput();
        Confirm(cf);
        m.handle(cf);
        assert(m.pool[c].state == 1);
        assert(g_fire_count == 1);
        InputState cf2 = FreshInput();
        Confirm(cf2);
        m.handle(cf2);
        assert(m.pool[c].state == 0);
        assert(g_fire_count == 2);
    }

    // ─── Removed widget ignores taps ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        u16 c = m.checkbox(10.0f, 20.0f, "Sfx", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb);
        m.remove(c);
        InputState t = FreshInput();
        Tap(t, 10.0f + 14.0f, 20.0f + 14.0f);
        m.handle(t);
        assert(g_fire_count == 0);
    }

    // ─── State fade (anim): checkmark fades in/out instead of popping ───
    {
        ui::Manager m;
        m.init();
        u16 c = m.checkbox(10.0f, 20.0f, "C", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, FireCb);

        assert(m.state_fade[c] == 0.0f); // unchecked starts fully off
        InputState in = FreshInput();
        Tap(in, 24.0f, 34.0f); // inside the 28px box
        m.handle(in);
        assert(m.pool[c].state == 1);
        m.update(1.0f / 60.0f);
        assert(m.state_fade[c] > 0.0f && m.state_fade[c] < 1.0f);
        f32 held = m.state_fade[c];
        m.update(0.0f);
        assert(m.state_fade[c] == held); // dt == 0 no-op
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.state_fade[c] > 0.999f);

        // Un-tapping fades it back out.
        InputState in2 = FreshInput();
        Tap(in2, 24.0f, 34.0f);
        m.handle(in2);
        assert(m.pool[c].state == 0);
        m.update(1.0f / 60.0f);
        assert(m.state_fade[c] < 1.0f && m.state_fade[c] > 0.0f);
    }

    // ─── Initial state snaps (no first-frame fade-in) ───
    {
        ui::Manager m;
        m.init();
        u16 c = m.checkbox(10.0f, 20.0f, "C", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, true, FireCb);
        assert(m.pool[c].state == 1 && m.state_fade[c] == 1.0f);
    }

    printf("[checkbox] all tests passed\n");
    return 0;
}

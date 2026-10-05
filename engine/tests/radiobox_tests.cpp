// Radiobox widget tests — factory, group exclusivity, last-initial-wins,
// tap select + silent re-tap, keyboard confirm, group getter, removal.
// Plain main() + assert(), no framework.
// Headless: handle() is pure CPU (no font/render needed for selection).
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

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
        u16 r = m.radiobox(10.0f, 20.0f, "Easy", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, false, FireCb);
        assert(r != UINT16_MAX);
        assert(!m.is_radiobox_selected(r));
        assert(m.get_radio_group_selected(1) == -1);
        assert(m.pool[r].frame.w == 28.0f && m.pool[r].frame.h == 28.0f);
        assert(m.radio_group[r] == 1);
        assert(g_fire_count == 0);
        u16 b = m.button(0.0f, 0.0f, 80.0f, 30.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);
        assert(!m.is_radiobox_selected(b)); // type guard: not a radio
    }

    // ─── initial selects; last-initial-wins; groups independent ───
    {
        ui::Manager m;
        m.init();
        u16 a = m.radiobox(10.0f, 20.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, true, NoopCb);
        u16 b = m.radiobox(10.0f, 60.0f, "B", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, true, NoopCb);
        u16 c = m.radiobox(10.0f, 100.0f, "C", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 2, true, NoopCb);
        assert(!m.is_radiobox_selected(a)); // cleared by b
        assert(m.is_radiobox_selected(b));
        assert(m.is_radiobox_selected(c)); // other group untouched
        assert(m.get_radio_group_selected(1) == b);
        assert(m.get_radio_group_selected(2) == c);
        assert(m.get_radio_group_selected(3) == -1); // empty group
    }

    // ─── Tap selects + fires; re-tap is silent ───
    {
        ui::Manager m;
        m.init();
        g_fired_id = -1;
        g_fire_count = 0;
        u16 a = m.radiobox(10.0f, 20.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, true, FireCb);
        u16 b = m.radiobox(10.0f, 60.0f, "B", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, false, FireCb);
        InputState t = FreshInput();
        Tap(t, 10.0f + 14.0f, 60.0f + 14.0f); // center of b
        m.handle(t);
        assert(!m.is_radiobox_selected(a));
        assert(m.is_radiobox_selected(b));
        assert(g_fire_count == 1 && g_fired_id == b);
        assert(m.was_clicked(b));
        InputState t2 = FreshInput();
        Tap(t2, 10.0f + 14.0f, 60.0f + 14.0f); // re-tap b
        m.handle(t2);
        assert(m.is_radiobox_selected(b));
        assert(g_fire_count == 1); // silent: no second callback
    }

    // ─── Tap outside changes nothing ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        u16 a = m.radiobox(10.0f, 20.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, true, FireCb);
        InputState t = FreshInput();
        Tap(t, 700.0f, 600.0f); // far outside
        m.handle(t);
        assert(m.is_radiobox_selected(a));
        assert(g_fire_count == 0);
    }

    // ─── Keyboard confirm selects + fires; re-confirm is silent ───
    {
        ui::Manager m;
        m.init();
        g_fired_id = -1;
        g_fire_count = 0;
        u16 a = m.radiobox(10.0f, 20.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, true, FireCb);
        u16 b = m.radiobox(10.0f, 60.0f, "B", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, false, FireCb);
        m.focus_id = b;
        InputState cf = FreshInput();
        Confirm(cf);
        m.handle(cf);
        assert(!m.is_radiobox_selected(a));
        assert(m.is_radiobox_selected(b));
        assert(g_fire_count == 1 && g_fired_id == b);
        InputState cf2 = FreshInput();
        Confirm(cf2);
        m.handle(cf2);
        assert(g_fire_count == 1); // silent: no second callback
    }

    // ─── Removed widget is invisible to the group getter ───
    {
        ui::Manager m;
        m.init();
        u16 a = m.radiobox(10.0f, 20.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, true, NoopCb);
        assert(m.get_radio_group_selected(1) == a);
        m.remove(a);
        assert(m.get_radio_group_selected(1) == -1);
    }

    // ─── State fade (anim): dot fades in/out instead of popping ───
    {
        ui::Manager m;
        m.init();
        u16 a = m.radiobox(10.0f, 20.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, false, NoopCb);
        u16 b = m.radiobox(10.0f, 60.0f, "B", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, false, NoopCb);

        // Unselected widgets start fully faded out (no first-frame fade-in).
        assert(m.state_fade[a] == 0.0f && m.state_fade[b] == 0.0f);

        // Select A: fade rises but is strictly between the two states, so a
        // single frame can never render it fully on or fully off.
        m.radiobox_select(a);
        assert(m.pool[a].state == 1 && m.pool[b].state == 0);
        m.update(1.0f / 60.0f);
        assert(m.state_fade[a] > 0.0f && m.state_fade[a] < 1.0f);
        assert(m.state_fade[b] == 0.0f);

        // dt == 0 is a no-op (Plan B: update() owns time-driven state).
        f32 held = m.state_fade[a];
        m.update(0.0f);
        assert(m.state_fade[a] == held);

        // Converges to 1, and stays there.
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.state_fade[a] > 0.999f && m.state_fade[a] <= 1.0f);

        // Switch to B: A fades back out, B fades in.
        m.radiobox_select(b);
        m.update(1.0f / 60.0f);
        assert(m.state_fade[a] < 1.0f && m.state_fade[a] > 0.0f);
        assert(m.state_fade[b] > 0.0f && m.state_fade[b] < 1.0f);
        for (int i = 0; i < 120; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.state_fade[a] < 0.001f && m.state_fade[b] > 0.999f);
    }

    // ─── Initial selection snaps (does not fade in on frame 1) ───
    {
        ui::Manager m;
        m.init();
        u16 a = m.radiobox(10.0f, 20.0f, "A", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, true, NoopCb);
        u16 b = m.radiobox(10.0f, 60.0f, "B", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, 1, false, NoopCb);
        assert(m.pool[a].state == 1);
        assert(m.state_fade[a] == 1.0f && m.state_fade[b] == 0.0f);
    }

    printf("[radiobox] all tests passed\n");
    return 0;
}

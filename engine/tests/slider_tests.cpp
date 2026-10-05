// Slider widget tests — factory initial, drag value mapping, clamp,
// bordered-style content-box inset mapping, on_change args.
// Plain main() + assert(), no framework.
// Headless: handle() is pure CPU; drag injected white-box via
// GestureType::Drag (same pattern as event_tests).
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

static bool Near(f32 a, f32 b) noexcept {
    return __builtin_fabsf(a - b) < 0.005f;
}

static void NoopChange(u16, f32) noexcept {}

static f32 g_last_value = -1.0f;
static void CaptureChange(u16, f32 v) noexcept {
    g_last_value = v;
}

static InputState FreshInput() noexcept {
    InputState in;
    in.init();
    return in;
}

// Press-and-hold finger flagged as Drag (skips swipe classification).
static void DragDown(InputState& in, f32 x, f32 y) noexcept {
    in.touch.on_touch_down(0, x, y);
    in.touch.fingers[0].type = GestureType::Drag;
}

int main() {
    // ─── Factory stores initial ───
    {
        ui::Manager m;
        m.init();
        u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.33f, NoopChange);
        assert(s != UINT16_MAX);
        assert(Near(m.get_slider_value(s), 0.33f));
        assert(m.get_slider_value(UINT16_MAX) == 0.0f); // OOB guard
    }

    // ─── Drag maps linearly across a borderless bar ───
    {
        ui::Manager m;
        m.init();
        u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.0f, NoopChange);
        InputState in = FreshInput();
        DragDown(in, 60.0f, 40.0f);
        m.handle(in); // grab at (60-10)/200 = 0.25
        assert(Near(m.get_slider_value(s), 0.25f));
        in.touch.on_touch_move(0, 160.0f, 40.0f);
        m.handle(in); // (160-10)/200 = 0.75
        assert(Near(m.get_slider_value(s), 0.75f));
        in.touch.on_touch_up(0);
        m.handle(in);
    }

    // ─── Drag clamps at both ends ───
    {
        ui::Manager m;
        m.init();
        u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.5f, NoopChange);
        InputState in = FreshInput();
        DragDown(in, 110.0f, 40.0f);
        m.handle(in);
        in.touch.on_touch_move(0, -500.0f, 40.0f);
        m.handle(in);
        assert(m.get_slider_value(s) == 0.0f);
        in.touch.on_touch_move(0, 9999.0f, 40.0f);
        m.handle(in);
        assert(m.get_slider_value(s) == 1.0f);
        in.touch.on_touch_up(0);
        m.handle(in);
    }

    // ─── Bordered style: drag maps inside the border caps ───
    {
        ui::Manager m;
        m.init();
        ui::WidgetStyle st{};
        st.border_color = 0xFFFF0000;
        st.border_width = 0.1f; // uniform: x0 = ax + 0.1w, x1 = ax + 0.9w
        u8 sid = m.register_style(st);
        // Slider at x=10 w=200 → content 30..190, span 160.
        u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.0f, NoopChange, UINT16_MAX, sid);
        InputState in = FreshInput();
        DragDown(in, 30.0f, 40.0f);
        m.handle(in);
        assert(Near(m.get_slider_value(s), 0.0f));
        in.touch.on_touch_move(0, 110.0f, 40.0f); // (110-30)/160 = 0.5
        m.handle(in);
        assert(Near(m.get_slider_value(s), 0.5f));
        in.touch.on_touch_move(0, 190.0f, 40.0f);
        m.handle(in);
        assert(Near(m.get_slider_value(s), 1.0f));
        in.touch.on_touch_up(0);
        m.handle(in);
    }

    // ─── on_change receives the mapped value ───
    {
        ui::Manager m;
        m.init();
        g_last_value = -1.0f;
        u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.0f, CaptureChange);
        (void)s;
        InputState in = FreshInput();
        DragDown(in, 60.0f, 40.0f);
        m.handle(in);
        assert(Near(g_last_value, 0.25f));
        in.touch.on_touch_move(0, 160.0f, 40.0f);
        m.handle(in);
        assert(Near(g_last_value, 0.75f));
        in.touch.on_touch_up(0);
        m.handle(in);
    }

    // ─── Keyboard: arrows step, Home/End jump, and the app is told ───
    // The Slider was the one focusable widget with NO key handling at all: Tab
    // walked straight through it as if it were a button, and nothing could change
    // its value without a pointer. Every other focusable widget answers the arrow
    // keys, so this was an inconsistency rather than a decision.
    {
        ui::Manager  m;
        m.init();
        g_last_value = -1.0f;
        const u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.5f, CaptureChange);
        m.focus_id       = s;
        const f32 step = ui::Manager::K_SLIDER_KEY_STEP;
        assert(Near(step, 0.01f));

        auto key = [&m](InputAction a) {
            InputState in = FreshInput();
            in.action_count = 1;
            in.actions[0]   = a;
            m.handle(in);
        };

        key(InputAction::MenuDown);
        assert(Near(m.get_slider_value(s), 0.5f + step));
        assert(Near(g_last_value, 0.5f + step)); // on_change fired, like the drag

        key(InputAction::MenuUp);
        key(InputAction::MenuUp);
        assert(Near(m.get_slider_value(s), 0.5f - step));

        // Both ends clamp instead of wrapping - a slider that wraps would turn a
        // held arrow key into a sawtooth, and there is nothing to "hide" like a
        // list's ends.
        key(InputAction::MenuLast);
        assert(Near(m.get_slider_value(s), 1.0f));
        key(InputAction::MenuDown);
        assert(Near(m.get_slider_value(s), 1.0f));
        key(InputAction::MenuFirst);
        assert(Near(m.get_slider_value(s), 0.0f));
        key(InputAction::MenuUp);
        assert(Near(m.get_slider_value(s), 0.0f));

        // A key at the bound is a NO-OP, not a spurious "changed to 0". The drag
        // path has no equivalent case (a drag always lands somewhere new), so this
        // is the one behaviour the keyboard adds and it has to be stated.
        g_last_value = -1.0f;
        key(InputAction::MenuFirst);
        assert(Near(m.get_slider_value(s), 0.0f));
        assert(g_last_value < 0.0f); // nothing fired

        // The focused widget OWNS the key: the host must not also act on it, or a
        // host font tuner would shrink the text on every arrow press.
        key(InputAction::MenuDown);
        assert(m.nav_consumed_this_frame());
        assert(m.focus_id == s); // ...and focus did not walk away
    }

    // ─── An unfocused slider ignores the keys entirely ───
    {
        ui::Manager  m;
        m.init();
        g_last_value = -1.0f;
        const u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.5f, CaptureChange);
        // focus_id stays UINT16_MAX
        InputState     in = FreshInput();
        in.action_count  = 1;
        in.actions[0]    = InputAction::MenuDown;
        m.handle(in);
        assert(Near(m.get_slider_value(s), 0.5f));
        assert(g_last_value < 0.0f);
        assert(!m.nav_consumed_this_frame()); // nobody owned it
    }

    // ─── A DISABLED slider answers nothing ───
    // The choke point at the top of handle() drops the focus, so this is really a
    // test that the branch sees no eligible focus rather than a second test of the
    // release. It is here because "disabled" is the state a player will actually
    // try it in.
    {
        ui::Manager  m;
        m.init();
        g_last_value = -1.0f;
        const u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.5f, CaptureChange);
        m.focus_id       = s;
        m.set_enabled(s, false);
        InputState     in = FreshInput();
        in.action_count  = 1;
        in.actions[0]    = InputAction::MenuDown;
        m.handle(in);
        assert(m.focus_id == UINT16_MAX);
        assert(Near(m.get_slider_value(s), 0.5f));
        assert(g_last_value < 0.0f);
    }

    // ─── Confirm does nothing to a focused Slider ───
    // Same defect class as the TabBar: slider() takes only a ChangeCallback, so
    // the permissive Confirm branch emitted a CLICK that no callback could
    // consume. A Slider has no discrete activation - arrows step it, Home/End
    // jump it - so there is nothing for Enter to confirm.
    {
        ui::Manager  m;
        m.init();
        g_last_value = -1.0f;
        const u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.5f, CaptureChange);
        m.focus_id       = s;
        InputState       in = FreshInput();
        in.action_count  = 1;
        in.actions[0]    = InputAction::Confirm;
        m.handle(in);
        assert(Near(m.get_slider_value(s), 0.5f));  // unchanged
        assert(g_last_value < 0.0f);                 // no callback
        assert(!m.nav_consumed_this_frame());        // nothing consumed it
        ui::UiEvent ev{};
        while (m.poll_event(ev)) {
            assert(ev.type != ui::ui_event_type::CLICK);
        }
    }

    // ─── Escape does NOT cancel a slider drag ───
    // Recorded because the contract table claimed it did. It does not: nothing
    // snapshots the pre-drag value, so there is nothing to restore, and a drag is
    // not a modal state that Escape would dismiss.
    {
        ui::Manager  m;
        m.init();
        g_last_value = -1.0f;
        const u16 s = m.slider(10.0f, 20.0f, 200.0f, 40.0f, 0.5f, CaptureChange);
        InputState     in = FreshInput();
        DragDown(in, 150.0f, 40.0f);
        m.handle(in);
        const f32 dragged = m.get_slider_value(s);
        assert(dragged > 0.6f); // the drag moved it
        InputState esc = FreshInput();
        esc.action_count = 1;
        esc.actions[0]   = InputAction::Pause;
        m.handle(esc);
        assert(Near(m.get_slider_value(s), dragged)); // Escape leaves it alone
    }

    printf("[slider] all tests passed\n");
    return 0;
}

// Resize-drop tests: input decoded against pre-resize dims must be
// discardable (action_count = 0 + touch.reset(), as game_frame does when
// resize_pending is set) without phantom taps afterwards.
// Plain main() + assert(), no framework. Headless: pure CPU state machines.
#include "../input/mm_input_event.hpp"
#include "core/mm_types.h"
#include "../input/mm_input_state.hpp"
#include <cassert>

static bool HasSelect(const InputState &in) noexcept {
    for (u8 i = 0; i < in.action_count; ++i) {
        if (in.actions[i] == InputAction::Select) {
            return true;
        }
    }
    return false;
}

int main() {
    // ─── Positive control: Down+Up in one frame emits Select ───
    {
        InputEventQueue q;
        InputState      in;
        in.init();
        q.push(InputEvent::make_touch_down(0, 100.0f, 200.0f));
        q.push(InputEvent::make_touch_up(0));
        in.process(q, 1.0f / 60.0f);
        assert(in.action_count == 1);
        assert(HasSelect(in));
    }

    // ─── Resize drop: decoded actions discarded, no phantom tap later ───
    {
        InputEventQueue q;
        InputState      in;
        in.init();
        q.push(InputEvent::make_touch_down(0, 100.0f, 200.0f));
        q.push(InputEvent::make_touch_up(0));
        in.process(q, 1.0f / 60.0f);
        assert(in.action_count == 1); // decoded pre-resize...
        in.action_count = 0;          // ...game_frame drops them...
        in.touch.reset();
        assert(in.action_count == 0);
        // A lone Up afterwards must not resurrect a tap (Idle guard).
        q.push(InputEvent::make_touch_up(0));
        in.process(q, 1.0f / 60.0f);
        assert(!HasSelect(in));
    }

    // ─── Press held across reset recovers: no stuck gesture, tracker reusable ───
    {
        InputEventQueue q;
        InputState      in;
        in.init();
        q.push(InputEvent::make_touch_down(0, 50.0f, 50.0f));
        in.process(q, 1.0f / 60.0f);
        assert(!HasSelect(in)); // still pressing, nothing decoded
        in.action_count = 0;
        in.touch.reset(); // resize lands mid-press
        q.push(InputEvent::make_touch_up(0));
        in.process(q, 1.0f / 60.0f);
        assert(!HasSelect(in)); // orphan Up ignored, not a tap
        // Fresh gesture afterwards works normally.
        q.push(InputEvent::make_touch_down(0, 50.0f, 50.0f));
        q.push(InputEvent::make_touch_up(0));
        in.process(q, 1.0f / 60.0f);
        assert(HasSelect(in));
    }

    return 0;
}

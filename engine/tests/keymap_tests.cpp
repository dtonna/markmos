// Keyboard-contract tests for the INPUT LAYER - the Tab / arrow-key split.
//
// Why this file exists at all: every other test in engine/tests builds an
// InputState by hand and drops an InputAction straight into `actions[]`. That
// bypasses process(), which is the ONLY place a key becomes an action - so a
// full revert of the keymap (Tab -> MenuDown again) left all 37 test binaries
// green. A test that passes against the code it is meant to catch is worse than
// no test: it launders the bug into "verified". The revert-check that found this
// is recorded in AGENTS.md.
//
// So these tests push real InputEvents through a real InputEventQueue and read
// the actions process() produced. Nothing here touches a widget: the contract
// being pinned is "which key means what", and it has to be pinned BELOW the UI
// layer because that is where it was wrong.
//
// Headless: process() is pure CPU over a fixed-capacity ring.
#include "../input/mm_input_event.hpp"
#include "core/mm_types.h"
#include "../input/mm_input_state.hpp"
#include <cassert>
#include <cstdio>

// Feed `keys` as real key-downs and return the actions one process() frame
// produced. With `shift`, Shift is HELD for a frame of its own FIRST: process()
// reads keys_down[Shift] to choose the direction, and a Shift pressed in the same
// frame would only raise just_pressed. Order matters for a second reason too -
// just_pressed is a RISING edge and keys_down persists across process() calls,
// so a key that is still down produces NO action when pushed again. Each key is
// therefore released in the same frame it is pressed: keys_just_pressed is
// cleared once per frame rather than per event, so the pair still emits exactly
// one action, and the next call starts from a released key.
//
// `mod` generalises that: Shift for Tab, Alt for Alt+Down. Written once instead of
// adding a Shift-only flag plus an Alt-only flag later - "held in its own frame" is
// the whole reason just_pressed works and it is not Shift-specific.
static u8 ActionsForMod(InputState &in, InputEventQueue &q, const KeyCode *keys, int n, KeyCode mod) noexcept {
    if (mod != KeyCode::Unknown) {
        q.reset();
        InputEvent s{};
        s.type = InputEventType::KeyDown;
        s.key  = mod;
        assert(q.push(s));
        in.process(q, 1.0f / 60.0f);
    }
    q.reset();
    for (int i = 0; i < n; ++i) {
        InputEvent ev{};
        ev.type = InputEventType::KeyDown;
        ev.key  = keys[i];
        assert(q.push(ev));
        ev.type = InputEventType::KeyUp; // see below
        assert(q.push(ev));
    }
    in.process(q, 1.0f / 60.0f);
    return in.action_count;
}

static u8 ActionsFor(InputState &in, InputEventQueue &q, const KeyCode *keys, int n, bool shift = false) noexcept {
    return ActionsForMod(in, q, keys, n, shift ? KeyCode::Shift : KeyCode::Unknown);
}

static bool Has(InputState &in, InputAction a) noexcept {
    for (u8 i = 0; i < in.action_count; ++i) {
        if (in.actions[i] == a) {
            return true;
        }
    }
    return false;
}

int main() {
    InputState      in;
    InputEventQueue q;
    in.init();

    // ─── Tab walks focus, and is NOT an arrow key ───
    // The whole bug in one assertion. While Tab produced MenuDown, every widget
    // that owned "up/down is mine" also owned Tab, and each of them CLAMPS
    // instead of wrapping - so focus went in and could not come out.
    {
        const KeyCode tab[] = {KeyCode::Tab};
        in.init();
        q.reset();
        ActionsFor(in, q, tab, 1);
        assert(in.action_count == 1);
        assert(in.actions[0] == InputAction::FocusNext);
        assert(!Has(in, InputAction::MenuDown));
        assert(!Has(in, InputAction::SwapUp));
    }

    // ─── Shift-Tab walks back, and is not MenuUp either ───
    {
        const KeyCode tab[] = {KeyCode::Tab};
        in.init();
        ActionsFor(in, q, tab, 1, /*shift=*/true);
        assert(in.action_count == 1);
        assert(in.actions[0] == InputAction::FocusPrev);
        assert(!Has(in, InputAction::MenuUp));
        assert(!Has(in, InputAction::SwapUp));
    }

    // ─── The arrow keys stay arrows, on both Shift states ───
    // The other half of the contract, and the reason Tab had to move: these are
    // what a focused ComboBox/ListBox/TabBar owns, so if Shift could turn one
    // into a focus key the same trap would reopen from the other direction.
    {
        const KeyCode dn[] = {KeyCode::Down};
        in.init();
        ActionsFor(in, q, dn, 1);
        assert(Has(in, InputAction::MenuDown));
        assert(!Has(in, InputAction::FocusNext));
        assert(!Has(in, InputAction::FocusPrev));

        in.init();
        ActionsFor(in, q, dn, 1, /*shift=*/true);
        assert(Has(in, InputAction::MenuDown)); // Shift+Down is still an arrow
        assert(!Has(in, InputAction::FocusNext));

        const KeyCode up[] = {KeyCode::Up};
        in.init();
        ActionsFor(in, q, up, 1);
        assert(Has(in, InputAction::MenuUp));
        assert(!Has(in, InputAction::FocusPrev));
    }

    // ─── W and S are arrows AND letters - unchanged, and still not Tab ───
    // This collision is why the TextField editor needed a `text_count == 0`
    // guard at all. It still exists (one keystroke, two deliveries), but it is
    // no longer load-bearing for focus: neither of these is a focus action.
    {
        const KeyCode w[] = {KeyCode::W};
        in.init();
        ActionsFor(in, q, w, 1);
        assert(Has(in, InputAction::MenuUp));
        assert(!Has(in, InputAction::FocusPrev));
        assert(!Has(in, InputAction::FocusNext));

        const KeyCode s[] = {KeyCode::S};
        in.init();
        ActionsFor(in, q, s, 1);
        assert(Has(in, InputAction::MenuDown));
        assert(!Has(in, InputAction::FocusNext));
    }

    // ─── Enter and Space are one action, Escape is Pause, Backspace is Back ───
    // Pinned because Space is the key the ComboBox contract opens the popup on:
    // it shares an action with Enter, so "Space opens, Enter commits" is a
    // UI-layer distinction, never a keymap one.
    {
        const KeyCode enter[] = {KeyCode::Enter};
        in.init();
        ActionsFor(in, q, enter, 1);
        assert(Has(in, InputAction::Confirm));

        const KeyCode space[] = {KeyCode::Space};
        in.init();
        ActionsFor(in, q, space, 1);
        assert(Has(in, InputAction::Confirm));

        const KeyCode esc[] = {KeyCode::Escape};
        in.init();
        ActionsFor(in, q, esc, 1);
        assert(Has(in, InputAction::Pause));
        assert(!Has(in, InputAction::Back)); // Escape closes the popup; Backspace deletes

        const KeyCode bksp[] = {KeyCode::Backspace};
        in.init();
        ActionsFor(in, q, bksp, 1);
        assert(Has(in, InputAction::Back));
        assert(!Has(in, InputAction::Pause));
    }

    // ─── Home / End are first / last - and NOT the caret keys ───
    // The collision this splits is the one the TextField editor has always had:
    // it reads these as RAW keys to move the caret, because macOS delivers Home
    // as Fn+Left and the host could not tell them apart for a long time. Now the
    // keymap emits them too, and the two uses are separated by the editor's early
    // return rather than by "is this the key I meant".
    {
        const KeyCode home[] = {KeyCode::Home};
        in.init();
        ActionsFor(in, q, home, 1);
        assert(in.action_count == 1);
        assert(in.actions[0] == InputAction::MenuFirst);
        assert(!Has(in, InputAction::FocusNext));
        assert(!Has(in, InputAction::MenuUp)); // and it is not an arrow

        const KeyCode end[] = {KeyCode::End};
        in.init();
        ActionsFor(in, q, end, 1);
        assert(in.action_count == 1);
        assert(in.actions[0] == InputAction::MenuLast);
        assert(!Has(in, InputAction::MenuDown));
    }

    // ─── Shift+Home / Shift+End are still first / last ───
    // Shift must not turn them into something else, for the same reason Shift+Down
    // has to stay an arrow: the widget branches claim Menu* actions by identity,
    // and a modifier that silently changed the identity would hand them a key
    // they do not own.
    {
        const KeyCode home[] = {KeyCode::Home};
        in.init();
        ActionsFor(in, q, home, 1, /*shift=*/true);
        assert(Has(in, InputAction::MenuFirst));
        assert(!Has(in, InputAction::FocusNext));

        const KeyCode end[] = {KeyCode::End};
        in.init();
        ActionsFor(in, q, end, 1, /*shift=*/true);
        assert(Has(in, InputAction::MenuLast));
    }

    // ─── Left / Right are Menu* horizontal nav, and NOT Swap* ───
    // This block used to assert `action_count == 0`: the arrows produced NOTHING,
    // which is why mm_03 had no desktop path for a horizontal swap and mm_07's
    // caret had to read the raw keys. The mapping now exists, so the pin moves
    // with it - and it pins the choice that matters: MenuLeft/MenuRight (the
    // keyboard family every other arrow uses) and NOT SwapLeft/SwapRight, whose
    // only other producer is GestureType::Swipe*.
    //
    // The two-signal warning from the old comment is now a hard assert, because the
    // two-signal case is REAL: mm_07's page nav reads raw Left/Right (line ~1944)
    // and mm_03/mm_04 have Swap* branches. If a future keymap change also emitted
    // SwapLeft here, a host reading both would act twice per press.
    {
        const KeyCode lf[] = {KeyCode::Left};
        in.init();
        u8 n = ActionsFor(in, q, lf, 1);
        assert(n == 1);
        assert(in.actions[0] == InputAction::MenuLeft);
        assert(!Has(in, InputAction::SwapLeft));
        assert(!Has(in, InputAction::MenuUp)); // horizontal is not vertical

        const KeyCode rt[] = {KeyCode::Right};
        in.init();
        n = ActionsFor(in, q, rt, 1);
        assert(n == 1);
        assert(in.actions[0] == InputAction::MenuRight);
        assert(!Has(in, InputAction::SwapRight));
    }

    // ─── F4 toggles a dropdown, and is not Confirm ───
    // Space/Enter already open a ComboBox; F4 is the platform-standard second way
    // in, and it has to be a DISTINCT action or a host that binds F4 would also
    // fire on Enter.
    {
        const KeyCode f4[] = {KeyCode::F4};
        in.init();
        ActionsFor(in, q, f4, 1);
        assert(in.action_count == 1);
        assert(in.actions[0] == InputAction::MenuToggle);
        assert(!Has(in, InputAction::Confirm));
    }

    // ─── Alt+Down is MenuToggle and NOTHING else ───
    // The load-bearing assertion is the negative one. Alt+Down and Down arrive as
    // the same physical key, so emitting both would hand a ComboBox two actions for
    // one keystroke: MenuDown opens the popup and moves the highlight, then
    // MenuToggle shuts it again - a dropdown that flickers and never opens. This is
    // the Tab/MenuDown trap again, and it is only reachable because a CHORD can
    // deliver two actions (a letter plus an arrow does not).
    {
        const KeyCode dn[] = {KeyCode::Down};
        in.init();
        ActionsForMod(in, q, dn, 1, KeyCode::Alt);
        assert(in.action_count == 1);
        assert(in.actions[0] == InputAction::MenuToggle);
        assert(!Has(in, InputAction::MenuDown));

        // Alt+W is not a thing: W is a letter AND MenuUp, and that collision is
        // harmless because the letter arrives as TEXT, not as a second action.
        // Alt+Down is different precisely because we emit two ACTIONS.
        const KeyCode w[] = {KeyCode::W};
        in.init();
        ActionsForMod(in, q, w, 1, KeyCode::Alt);
        assert(in.action_count == 1);
        assert(in.actions[0] == InputAction::MenuUp);
    }

    // ─── PageUp / PageDown are their own actions, not arrows ───
    // macOS gives them real virtual keycodes (kVK_PageUp 0x74, kVK_PageDown 0x79
    // in HIToolbox Events.h), so they are NOT "Down held longer" and must not
    // arrive as MenuUp/MenuDown - a list would move one row instead of one page.
    {
        const KeyCode pu[] = {KeyCode::PageUp};
        in.init();
        u8 n = ActionsFor(in, q, pu, 1);
        assert(n == 1);
        assert(in.actions[0] == InputAction::MenuPageUp);
        assert(!Has(in, InputAction::MenuUp));

        const KeyCode pd[] = {KeyCode::PageDown};
        in.init();
        n = ActionsFor(in, q, pd, 1);
        assert(n == 1);
        assert(in.actions[0] == InputAction::MenuPageDown);
        assert(!Has(in, InputAction::MenuDown));
        assert(!Has(in, InputAction::MenuToggle));
    }

    // ─── A chord works whether the modifier lands in the SAME frame or the one before ───
    // ActionsForMod holds the modifier in its own frame, which is the macOS shape -
    // but only because flagsChanged: is a SEPARATE event from the keyDown it
    // precedes. Android delivers the modifier keydown and the chord keydown as two
    // ordinary events in the same queue, so process() sees them in ONE frame, and
    // the mapping reads keys_down AFTER the queue is drained, so it has to work
    // there too. This is the invariant that makes both hosts correct, and it is
    // invisible until a host is added that batches.
    //
    // It is also the shape that catches a refactor which moves the key->action
    // mapping ABOVE the key-state update: the separate-frame helper would still
    // pass, because by then the modifier was already down from the previous frame.
    {
        auto chord = [](InputState &s, InputEventQueue &qq, KeyCode mod, KeyCode key) {
            qq.reset();
            InputEvent m{};
            m.type = InputEventType::KeyDown;
            m.key  = mod;
            assert(qq.push(m));
            InputEvent k{};
            k.type = InputEventType::KeyDown;
            k.key  = key;
            assert(qq.push(k));
            s.process(qq, 1.0f / 60.0f);
        };

        in.init();
        chord(in, q, KeyCode::Shift, KeyCode::Tab);
        assert(Has(in, InputAction::FocusPrev));
        assert(!Has(in, InputAction::FocusNext));

        in.init();
        chord(in, q, KeyCode::Alt, KeyCode::Down);
        assert(Has(in, InputAction::MenuToggle));
        assert(!Has(in, InputAction::MenuDown));
    }

    printf("[keymap] all tests passed\n");
    return 0;
}

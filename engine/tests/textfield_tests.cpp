// TextField widget tests — factory, tap-to-edit, typing, backspace,
// cursor Left/Right, buffer cap, Confirm finalize, tap-elsewhere dismiss.
// Plain main() + assert(), no framework.
// Headless: handle() is pure CPU (cursor/render need no font here).
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cstring>
#include <string>

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

static void Type(InputState& in, char c) noexcept {
    in.text_count = 1;
    in.text_input[0] = c;
}

// Type SEVERAL characters in one frame - the path a paste takes, and the path
// that proves a rejected character does not take the rest of the batch with it.
//
// Capped at 4 because that is all InputState::text_input holds, and
// InputState::process() drops the overflow silently (`if (text_count < 4)`).
static void TypeAll(InputState& in, const char* s) noexcept {
    in.text_count = 0;
    for (const char* q = s; *q != '\0' && in.text_count < 4; ++q) {
        in.text_input[in.text_count++] = *q;
    }
}

static void Key(InputState& in, KeyCode k) noexcept {
    in.keys_just_pressed[static_cast<size_t>(k)] = true;
}

static const char* Text(ui::Manager& m, u16 id) noexcept {
    return m.pool[id].text;
}

// Tap INSIDE a field at (10,20,200,32).
//
// The x is where the caret lands, and that is not decoration: a tap used to leave
// the caret wherever the factory had put it (the end), so every group below could
// tap at a convenient-looking spot and get "append" for free. Now the tap decides,
// and `TapEnd` is the one that means "I want the caret at the end" - which is
// what most of those groups actually meant. `TapStart` is its mirror.
static void TapField(InputState& in, f32 x) noexcept { Tap(in, 10.0f + x, 36.0f); }
// The far end of the field (x = 209 of 10..210) - caret to strlen.
static void TapEnd(InputState& in) noexcept { TapField(in, 199.0f); }
// The near end (x = 12) - caret to 0.
static void TapLeft(InputState& in) noexcept { TapField(in, 2.0f); }

int main() {
    // ─── Factory copies initial text, cursor at end ───
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "hi", 0xFF222222, 0xFFFFFFFF);
        assert(f != UINT16_MAX);
        assert(std::strcmp(Text(m, f), "hi") == 0);
        assert(m.cursor_pos[f] == 2);
        assert(!m.is_editing());
    }

    // ─── Tap field starts editing; tap elsewhere dismisses ───
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        assert(m.is_editing());
        InputState t2 = FreshInput();
        Tap(t2, 700.0f, 600.0f); // outside: dismiss (click-through)
        m.handle(t2);
        assert(!m.is_editing());
    }

    // ─── Typing appends at cursor ───
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "a", 0xFF222222, 0xFFFFFFFF);
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        InputState k = FreshInput();
        Type(k, 'b');
        m.handle(k);
        InputState k2 = FreshInput();
        Type(k2, 'c');
        m.handle(k2);
        assert(std::strcmp(Text(m, f), "abc") == 0);
        assert(m.cursor_pos[f] == 3);
        assert(m.is_editing()); // typing never finalizes
    }

    // ─── Left moves cursor; typing inserts mid-string ───
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        for (char c : {'a', 'b'}) {
            InputState k = FreshInput();
            Type(k, c);
            m.handle(k);
        }
        InputState l = FreshInput();
        Key(l, KeyCode::Left);
        m.handle(l);
        assert(m.cursor_pos[f] == 1);
        InputState k = FreshInput();
        Type(k, 'X');
        m.handle(k);
        assert(std::strcmp(Text(m, f), "aXb") == 0);
    }

    // ─── Backspace deletes before cursor; at 0 it is a noop ───
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "ab", 0xFF222222, 0xFFFFFFFF);
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        InputState b = FreshInput();
        Key(b, KeyCode::Backspace);
        m.handle(b);
        assert(std::strcmp(Text(m, f), "a") == 0);
        assert(m.cursor_pos[f] == 1);
        InputState b2 = FreshInput();
        Key(b2, KeyCode::Backspace);
        m.handle(b2);
        InputState b3 = FreshInput();
        Key(b3, KeyCode::Backspace); // at 0: noop, stays valid
        m.handle(b3);
        assert(std::strcmp(Text(m, f), "") == 0);
        assert(m.cursor_pos[f] == 0);
    }

    // ─── Buffer cap: text never exceeds 47 chars ───
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        for (int i = 0; i < 60; ++i) {
            InputState k = FreshInput();
            Type(k, 'z');
            m.handle(k);
        }
        assert(std::strlen(Text(m, f)) == 47);
    }

    // ─── Confirm finalizes editing ───
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "x", 0xFF222222, 0xFFFFFFFF);
        (void)f;
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        assert(m.is_editing());
        InputState c = FreshInput();
        Confirm(c);
        m.handle(c);
        assert(!m.is_editing());
        assert(std::strcmp(Text(m, f), "x") == 0); // text kept
    }

    // ─── Home / End ───
    // Home was unreachable before: macOS delivers it as Fn+Left, so the host
    // could only see Left and there was no KeyCode to map it to at all.
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "abcd", 0xFF222222, 0xFFFFFFFF);
        (void)f;
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        assert(m.is_editing());
        assert(m.cursor_pos[f] == 4); // tap-to-edit parks the caret at the end

        InputState h = FreshInput();
        Key(h, KeyCode::Home);
        m.handle(h);
        assert(m.cursor_pos[f] == 0);

        // Typing now inserts at the start.
        InputState ty = FreshInput();
        Type(ty, 'Z');
        m.handle(ty);
        assert(std::strcmp(Text(m, f), "Zabcd") == 0);

        InputState e = FreshInput();
        Key(e, KeyCode::End);
        m.handle(e);
        assert(m.cursor_pos[f] == 5);
    }

    // ─── Forward delete ───
    // A distinct virtual keycode from Backspace on every platform, and there
    // was no KeyCode for it: Delete did nothing at all.
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "abcd", 0xFF222222, 0xFFFFFFFF);
        (void)f;
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        InputState h = FreshInput();
        Key(h, KeyCode::Home);
        m.handle(h);
        InputState d = FreshInput();
        Key(d, KeyCode::Delete);
        m.handle(d);
        assert(std::strcmp(Text(m, f), "bcd") == 0); // removed 'a', not 'd'
        assert(m.cursor_pos[f] == 0);
        // At the end it is a no-op, not a wrap-around.
        InputState e = FreshInput();
        Key(e, KeyCode::End);
        m.handle(e);
        InputState d2 = FreshInput();
        Key(d2, KeyCode::Delete);
        m.handle(d2);
        assert(std::strcmp(Text(m, f), "bcd") == 0);
    }

    // ─── Escape cancels and restores the pre-edit text ───
    // Escape mapped to InputAction::Pause and the editing block ignored it, so
    // Escape ended the edit with every keystroke kept and no way to back out.
    {
        ui::Manager m;
        m.init();
        u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "orig", 0xFF222222, 0xFFFFFFFF);
        (void)f;
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        InputState ty = FreshInput();
        Type(ty, '!');
        m.handle(ty);
        assert(std::strcmp(Text(m, f), "orig!") == 0);

        InputState esc = FreshInput();
        esc.action_count = 1;
        esc.actions[0] = InputAction::Pause; // what Escape maps to
        m.handle(esc);
        assert(!m.is_editing());
        assert(std::strcmp(Text(m, f), "orig") == 0); // reverted

        // Confirm commits instead, so the two are distinguishable.
        InputState t2 = FreshInput();
        TapEnd(t2);
        m.handle(t2);
        InputState ty2 = FreshInput();
        Type(ty2, '?');
        m.handle(ty2);
        InputState c2 = FreshInput();
        Confirm(c2);
        m.handle(c2);
        assert(std::strcmp(Text(m, f), "orig?") == 0);
    }

    // ─── W and S are typable: their arrow action must not steal focus ───
    // W and S are MenuUp/MenuDown AND letters, so one keystroke arrives as both
    // a character and an arrow action. The editing block ends in
    // `if (!finalized) return;`, so the arrow half never reaches the keyboard
    // nav. Pin for that early return.
    {
        ui::Manager m;
        m.init();
        u16 f0 = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        u16 f1 = m.textfield(10.0f, 60.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        (void)f0;
        (void)f1;
        // Focus starts on the SECOND field, so a stolen arrow action has
        // somewhere to move it to. (With focus_id still UINT16_MAX the first
        // find_next_focus() lands on f0, which is also "not f1" - the assert
        // would pass with the gate removed.)
        m.focus_id = f1;
        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        assert(m.is_editing());
        assert(m.focus_id == f1);

        // A keystroke that is BOTH the letter and MenuDown (what 's' is).
        InputState s = FreshInput();
        Type(s, 's');
        s.action_count = 1;
        s.actions[0] = InputAction::MenuDown;
        m.handle(s);
        assert(std::strcmp(Text(m, f0), "s") == 0);
        assert(m.is_editing());
        assert(m.focus_id == f1); // focus did not move

        // ...and the arrow half of a PLAIN arrow key must stay in the field too.
        // This is the assertion that carries the weight now: with Tab split out,
        // MenuUp/MenuDown no longer mean "move focus" anywhere in the toolkit, so
        // a bare arrow can no longer be mistaken for the focus key. Before the
        // split this was the only thing keeping the editor honest.
        InputState dn = FreshInput();
        dn.action_count = 1;
        dn.actions[0] = InputAction::MenuDown;
        m.handle(dn);
        assert(m.is_editing());
        assert(m.focus_id == f1);
        assert(std::strcmp(Text(m, f0), "s") == 0); // and no phantom character
    }

    // ─── Tab commits the field and moves focus; the W/S letter does NOT ───
    // Two halves that used to be told apart by one thing: a real Tab brings NO
    // character with it, while W and S arrive as a character *and* as
    // MenuUp/MenuDown, so the editor had to gate on `text_count == 0` to avoid
    // committing on the letter's arrow half (which would drop a 'w' and jump to
    // the next field).
    //
    // Tab is now FocusNext/FocusPrev and never carries a character, so that guard
    // is gone - and the two halves are separated by the ACTION rather than by
    // inspecting what else arrived with it. Which means MenuUp/MenuDown must now
    // stay inside the field no matter what else comes with them, and the block
    // below pins that in both forms.
    {
        ui::Manager m;
        m.init();
        u16 f0 = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        u16 f1 = m.textfield(10.0f, 60.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        u16 f2 = m.textfield(10.0f, 100.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        assert(f0 == 0 && f1 == 1 && f2 == 2);

        // Tap into the middle field (a tap edits WITHOUT moving focus).
        InputState t = FreshInput();
        Tap(t, 50.0f, 76.0f);
        m.handle(t);
        assert(m.is_editing());

        InputState tab = FreshInput();
        tab.action_count = 1;
        tab.actions[0] = InputAction::FocusNext; // Tab
        m.handle(tab);
        assert(!m.is_editing());  // committed
        assert(m.focus_id == f2); // ...and moved FORWARD exactly one

        // One press, one move. The action list is NOT consumed, so without the
        // focus_moved flag the nav loop below would run the same FocusNext a
        // second time: from f2 that wraps to f0, which is ALSO where a single
        // wrap lands - so the pin that actually catches a f64 move is the
        // f1 -> f2 hop above, and this one just proves the wrap.
        m.handle(tab);
        assert(m.focus_id == f0); // wraps to the first

        InputState back = FreshInput();
        back.action_count = 1;
        back.actions[0] = InputAction::FocusPrev; // Shift-Tab
        m.handle(back);
        assert(!m.is_editing());
        assert(m.focus_id == f2); // wraps back to the last
    }

    // ─── Disabling a field mid-edit releases it - it must stop eating keys ───
    // The worst of the disabled-widget holes, because a field that is being typed
    // into swallows EVERY key: letters, Backspace, the caret arrows, Home/End.
    // Escape and Enter were the only ways out, and neither is reachable if the
    // field is gone from the player's point of view - the panel it lives in just
    // greyed out. A host that reads is_text_capture() also concludes something is
    // mid-edit and skips its own hotkeys, so the whole screen goes quiet.
    {
        ui::Manager  m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 300.0f, 200.0f, 0xFF222222);
        const u16 f     = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "draft", 0xFF222222, 0xFFFFFFFF, panel);

        InputState t = FreshInput();
        TapEnd(t);
        m.handle(t);
        assert(m.is_editing());
        assert(m.is_text_capture());

        m.set_enabled(panel, false);
        assert(m.pool[f].flags & ui::WF_ENABLED); // own flag untouched
        assert(!m.is_enabled(f));

        // One frame with no actions: the choke point at the top of handle() is
        // what releases it.
        InputState sweep = FreshInput();
        m.handle(sweep);
        assert(!m.is_editing());
        assert(!m.is_text_capture()); // so the host gets its hotkeys back

        // Now nothing reaches the dead field. Letters, Backspace and the caret
        // keys all have to be inert, and "hi" would be the proof if any leaked.
        const char *before = Text(m, f);
        for (char c : {'h', 'i'}) {
            InputState k = FreshInput();
            Type(k, c);
            m.handle(k);
        }
        InputState bs = FreshInput();
        bs.action_count = 1;
        bs.actions[0] = InputAction::Back;
        m.handle(bs);
        InputState left = FreshInput();
        left.action_count = 1;
        left.actions[0] = InputAction::MenuUp; // the caret reads raw keys, not this
        m.handle(left);
        assert(std::strcmp(Text(m, f), before) == 0);
        assert(!m.is_editing());

        // Re-enabling does not resume the edit: the text the player half-typed is
        // kept, but the caret is gone and focus is not handed back. Resuming
        // would mean a control that greyed out and came back silently grabs the
        // keyboard again, mid-word, with nothing on screen saying so.
        m.set_enabled(panel, true);
        m.handle(FreshInput());
        assert(!m.is_editing());
        assert(std::strcmp(Text(m, f), before) == 0);
    }

    // ─── Key auto-repeat: holding a key does something after the delay ───
    // macOS DOES deliver OS-level repeats, but process() only raised
    // just_pressed on a RISING edge, so every repeat was dropped: holding
    // Backspace deleted one byte, once.
    {
        InputState           in;
        in.init();
        InputEventQueue      q;
        q.push(InputEvent::make_key_down(KeyCode::Backspace));

        in.process(q, 0.0f);
        assert(in.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)]);

        // Nothing repeats while clearly under the delay. Stepping in delay/10
        // chunks and counting exact frames is deliberately avoided: 0.04f
        // accumulated ten times lands just UNDER 0.4f, so an exact-frame assert
        // would be testing f32 rounding rather than the timer.
        for (f32 t = 0.0f; t < in.key_repeat_delay * 0.5f; t += in.key_repeat_delay / 10.0f) {
            in.process(q, in.key_repeat_delay / 10.0f);
            assert(!in.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)]);
        }

        // It starts within a bounded time after the delay.
        bool started = false;
        for (int i = 0; i < 40 && !started; ++i) {
            in.process(q, in.key_repeat_delay / 10.0f);
            started = in.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)];
        }
        assert(started);

        // After the FIRST repeat it runs at the RATE, not the delay again: one
        // and a half rate steps must produce another. Pinned because the rate
        // switch has to happen - a delay-every-time key is unusable.
        bool at_rate = false;
        for (int i = 0; i < 3 && !at_rate; ++i) {
            in.process(q, in.key_repeat_rate * 0.5f);
            at_rate = in.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)];
        }
        assert(at_rate);

        // Releasing stops it dead.
        q.push(InputEvent::make_key_up(KeyCode::Backspace));
        in.process(q, 0.0f);
        for (int i = 0; i < 40; ++i) {
            in.process(q, in.key_repeat_rate * 2.0f);
            assert(!in.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)]);
        }
        // A key pressed AND released inside one frame never repeats.
        q.push(InputEvent::make_key_down(KeyCode::Left));
        q.push(InputEvent::make_key_up(KeyCode::Left));
        in.process(q, 0.0f);
        for (int i = 0; i < 40; ++i) {
            in.process(q, in.key_repeat_rate * 2.0f);
            assert(!in.keys_just_pressed[static_cast<size_t>(KeyCode::Left)]);
        }
    }

    // ─── NUMERIC accepts digits and refuses everything else ───
    // The mode is enforced AT THE KEYSTROKE. Scrubbing the string afterwards
    // would fight the caret, and validating at commit is the thing this replaced:
    // the player typed a deal, pressed Enter, and was told it was invalid.
    {
        ui::Manager m;
        m.init();
        const u16 f = m.textfield(0.0f, 0.0f, 200.0f, 32.0f, "", 0xFF333333, 0xFFFFFFFF);
        assert(m.get_text_input(f) == ui::text_input_mode::TEXT); // default
        m.set_text_input(f, ui::text_input_mode::NUMERIC);

        InputState in = FreshInput();
        m.handle(in); // tap: focus + start editing
        Tap(in, 100.0f, 16.0f);
        m.handle(in);

        for (char c : std::string("1234567890")) {
            InputState k = FreshInput();
            Type(k, c);
            m.handle(k);
        }
        assert(std::strcmp(Text(m, f), "1234567890") == 0);

        // Every kind of thing that is not a digit, including the ones a numeric
        // field is most likely to be handed by a keyboard: sign, decimal point,
        // thousands separator, space, and the multi-byte lead byte of a Thai
        // character (which arrives as a byte, so the per-byte test catches it).
        for (char c : std::string("aZ+-.,: /\t\'\"%")) {
            InputState k = FreshInput();
            Type(k, c);
            m.handle(k);
            assert(std::strcmp(Text(m, f), "1234567890") == 0);
        }
        // ...and the same for the first byte of a 3-byte UTF-8 sequence.
        InputState th = FreshInput();
        Type(th, static_cast<char>(0xE0));
        m.handle(th);
        assert(std::strcmp(Text(m, f), "1234567890") == 0);
    }

    // ─── NUMERIC does not refuse the SAME digit twice ───
    // A filter that latched "this character was rejected" would lock the field
    // after one refused letter, which reads as a dead keyboard.
    {
        ui::Manager m;
        m.init();
        const u16 f = m.textfield(0.0f, 0.0f, 200.0f, 32.0f, "", 0xFF333333, 0xFFFFFFFF);
        m.set_text_input(f, ui::text_input_mode::NUMERIC);
        InputState in = FreshInput();
        Tap(in, 100.0f, 16.0f);
        m.handle(in);
        for (char c : std::string("x7x7")) {
            InputState k = FreshInput();
            Type(k, c);
            m.handle(k);
        }
        assert(std::strcmp(Text(m, f), "77") == 0);
    }

    // ─── A multi-character paste is filtered per character, not dropped whole ───
    {
        ui::Manager m;
        m.init();
        const u16 f = m.textfield(0.0f, 0.0f, 200.0f, 32.0f, "", 0xFF333333, 0xFFFFFFFF);
        m.set_text_input(f, ui::text_input_mode::NUMERIC);
        InputState in = FreshInput();
        Tap(in, 100.0f, 16.0f);
        m.handle(in);

        // 4 characters: the batch limit.
        TypeAll(in, "1a2b");
        m.handle(in);
        // Both letters are dropped; both digits after them still land. A
        // whole-batch rejection would have produced "" or "1".
        assert(std::strcmp(Text(m, f), "12") == 0);
    }

    // ─── Backspace, arrows and the caret still work in NUMERIC ───
    // The mode filters what ARRIVES; it must not change any of the editing keys,
    // or a numeric field would be the one field you cannot correct.
    {
        ui::Manager m;
        m.init();
        const u16 f = m.textfield(0.0f, 0.0f, 200.0f, 32.0f, "", 0xFF333333, 0xFFFFFFFF);
        m.set_text_input(f, ui::text_input_mode::NUMERIC);
        InputState in = FreshInput();
        Tap(in, 100.0f, 16.0f);
        m.handle(in);
        for (char c : std::string("987")) {
            InputState k = FreshInput();
            Type(k, c);
            m.handle(k);
        }

        InputState l = FreshInput();
        Key(l, KeyCode::Left);
        m.handle(l);
        // "987", cursor 3 -> Left -> 2, so '4' lands BETWEEN '8' and '7'.
        InputState k = FreshInput();
        Type(k, '4');
        m.handle(k);
        assert(std::strcmp(Text(m, f), "9847") == 0);
        assert(m.cursor_pos[f] == 3);

        // Backspace takes the '4' back out - the mode must not have cost the
        // field the ability to be corrected.
        InputState b = FreshInput();
        Key(b, KeyCode::Backspace);
        m.handle(b);
        assert(std::strcmp(Text(m, f), "987") == 0);
    }

    // ─── A recycled slot must NOT inherit NUMERIC ───
    // alloc() resets per-widget arrays because a slot comes back from the
    // freelist holding whatever the PREVIOUS widget stored - and this is not a
    // "predicate" a factory covers: set_text_input() is a SETTER, so only
    // alloc() can undo it. This group is the regression guard for the rule in
    // alloc()'s own comment.
    {
        ui::Manager m;
        m.init();
        const u16 numeric = m.textfield(0.0f, 0.0f, 200.0f, 32.0f, "", 0xFF333333, 0xFFFFFFFF);
        m.set_text_input(numeric, ui::text_input_mode::NUMERIC);
        // remove() does NOT shrink `count` - it hides the widget and pushes the
        // slot onto the freelist, which is what makes the recycling reachable.
        const u16 before = m.count;
        m.remove(numeric);
        assert(m.count == before);
        assert(!(m.pool[numeric].flags & ui::WF_VISIBLE));

        const u16 plain = m.textfield(0.0f, 0.0f, 200.0f, 32.0f, "", 0xFF333333, 0xFFFFFFFF);
        assert(plain == numeric); // same slot, off the freelist
        assert(m.get_text_input(plain) == ui::text_input_mode::TEXT);
        // ...and it really types letters again, which is the part that would
        // otherwise show up only as "the field feels broken".
        InputState in = FreshInput();
        Tap(in, 100.0f, 16.0f);
        m.handle(in);
        InputState k = FreshInput();
        Type(k, 'a');
        m.handle(k);
        assert(std::strcmp(Text(m, plain), "a") == 0);
    }

    // ─── init() must not leave the array indeterminate ───
    // Poison first: a "fresh Manager reads 0" assert passes against a build that
    // never writes the field, because the stack happened to be zero.
    {
        ui::Manager m;
        m.text_input[0] = ui::text_input_mode::NUMERIC;
        m.text_input[1] = ui::text_input_mode::NUMERIC;
        m.init();
        assert(m.text_input[0] == ui::text_input_mode::TEXT);
        assert(m.text_input[1] == ui::text_input_mode::TEXT);
    }

    // ─── THE REPORTED BUG: tapping does not move the caret ───
    // "I tapped to delete a character, pressed Backspace, and the character I
    // wanted was still there."
    //
    // cursor_pos is written in exactly seven places - the factory, insert,
    // Backspace, Home, End, forward-Delete and the Escape restore - and NONE of
    // them is "the tap". So a tap in the middle of a field left the caret wherever
    // it already was (strlen, from the factory), and Backspace then deleted the
    // LAST character instead of the one under the finger.
    //
    // The property pinned here is MONOTONICITY, not an exact index: handle() has
    // no Renderer (see the update() no-Renderer rule), so the caret is placed by
    // ESTIMATING the advance. A hand-computed pixel expectation would be testing
    // the estimate rather than the behaviour, and would rot with the font.
    {
        ui::Manager m;
        m.init();
        const u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "12345", 0xFF222222, 0xFFFFFFFF);

        // Tap the LEFT edge: the caret belongs before the '1'.
        {
            InputState t = FreshInput();
            Tap(t, 12.0f, 36.0f);
            m.handle(t);
            assert(m.is_editing());
            assert(m.cursor_pos[f] == 0);
        }
        // Tap the RIGHT edge: after the '5'.
        {
            InputState t = FreshInput();
            Tap(t, 208.0f, 36.0f);
            m.handle(t);
            assert(m.cursor_pos[f] == 5);
        }
        // Monotonic across the field - this is the part that makes "tap then
        // Backspace" delete what the player aimed at.
        u16 last = 0;
        for (int x = 12; x <= 208; x += 8) {
            InputState t = FreshInput();
            Tap(t, static_cast<f32>(x), 36.0f);
            m.handle(t);
            assert(m.cursor_pos[f] >= last);
            last = m.cursor_pos[f];
        }
        assert(last == 5);

        // Backspace at caret 0 is CORRECTLY a no-op - there is nothing before the
        // caret. That is why the X button matters: with the caret pinned at 0,
        // Backspace can never reach the first character.
        {
            InputState t = FreshInput();
            TapLeft(t);
            m.handle(t);
            assert(m.cursor_pos[f] == 0);
            InputState b = FreshInput();
            Key(b, KeyCode::Backspace);
            m.handle(b);
            assert(std::strcmp(Text(m, f), "12345") == 0);
        }

        // The payoff: tap just inside the left edge (caret 1), Backspace, and the
        // FIRST character goes. Before the fix the caret stayed at the end, so this
        // deleted '5' and left '1234' behind - which is the whole report.
        {
            InputState t = FreshInput();
            TapField(t, 40.0f);
            m.handle(t);
            assert(m.cursor_pos[f] == 1);
            InputState b = FreshInput();
            Key(b, KeyCode::Backspace);
            m.handle(b);
            assert(std::strcmp(Text(m, f), "2345") == 0);
        }
    }

    // ─── A caret past the end of the text makes Backspace do NOTHING ───
    // set_text() rewrites the string and does not move the caret, so a shorter
    // string leaves cursor_pos beyond the end. Backspace's shift-left loop is
    // `for (j = cursor; j < len; ++j)`, which does not execute at all when
    // cursor >= len - it decrements the caret and deletes nothing, so every press
    // looks like a dead key until the caret walks back into range.
    {
        ui::Manager m;
        m.init();
        const u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "12345", 0xFF222222, 0xFFFFFFFF);
        InputState t = FreshInput();
        Tap(t, 208.0f, 36.0f);
        m.handle(t);
        assert(m.cursor_pos[f] == 5);

        assert(m.set_text(f, "1"));        // shorter, caret left at 5
        assert(std::strcmp(Text(m, f), "1") == 0);

        InputState b = FreshInput();
        Key(b, KeyCode::Backspace);
        m.handle(b);
        // The single character is gone. Before the clamp this deleted nothing.
        assert(std::strcmp(Text(m, f), "") == 0);
        assert(m.cursor_pos[f] == 0);
    }

    // ─── X button: clears the field in one tap ───
    // Not decoration. With the caret placement above this is the OTHER reliable
    // gesture, and it is the only one that works when the caret happens to be at 0
    // (where Backspace is correctly a no-op) or when the player cannot see where
    // the caret is.
    {
        ui::Manager m;
        m.init();
        const u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        assert(!m.get_textfield_clear(f)); // off by default: every existing field is untouched

        InputState t = FreshInput();
        Type(t, '7');
        m.handle(t); // not editing yet - the first character is dropped
        t = FreshInput();
        TapEnd(t);
        m.handle(t);
        assert(m.is_editing());
        for (char c : std::string("4242")) {
            InputState k = FreshInput();
            Type(k, c);
            m.handle(k);
        }
        assert(std::strcmp(Text(m, f), "4242") == 0);

        // Turn it on and tap the clear zone, which is the RIGHT end of the field.
        m.set_textfield_clear(f, true);
        InputState x = FreshInput();
        Tap(x, 10.0f + 200.0f - 6.0f, 36.0f);
        m.handle(x);
        assert(std::strcmp(Text(m, f), "") == 0);
        assert(m.cursor_pos[f] == 0);
        // Still editing, so the next keystroke starts a new seed rather than
        // needing a second tap.
        assert(m.is_editing());

        // ...and the text side of the field still edits normally, i.e. the clear
        // zone did not swallow the whole field.
        InputState k = FreshInput();
        Type(k, '9');
        m.handle(k);
        assert(std::strcmp(Text(m, f), "9") == 0);
    }

    // ─── X button: a tap on the TEXT side is not a clear ───
    {
        ui::Manager m;
        m.init();
        const u16 f = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "abcd", 0xFF222222, 0xFFFFFFFF);
        m.set_textfield_clear(f, true);
        InputState t = FreshInput();
        Tap(t, 40.0f, 36.0f); // left of the clear zone
        m.handle(t);
        assert(m.is_editing());
        assert(std::strcmp(Text(m, f), "abcd") == 0);
    }

    // ─── X button: a recycled slot does not inherit it ───
    {
        ui::Manager m;
        m.init();
        const u16 withx = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "", 0xFF222222, 0xFFFFFFFF);
        m.set_textfield_clear(withx, true);
        m.remove(withx);

        const u16 plain = m.textfield(10.0f, 20.0f, 200.0f, 32.0f, "abc", 0xFF222222, 0xFFFFFFFF);
        assert(plain == withx); // same slot, off the freelist
        assert(!m.get_textfield_clear(plain));
        // A tap at the right end must now EDIT, not clear.
        InputState t = FreshInput();
        Tap(t, 10.0f + 200.0f - 6.0f, 36.0f);
        m.handle(t);
        assert(std::strcmp(Text(m, plain), "abc") == 0);
    }

    printf("[textfield] all tests passed\n");
    return 0;
}

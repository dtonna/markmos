// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "mm_input_event.hpp"
#include "core/mm_types.h"
#include "mm_touch_gesture.hpp"
#include <cstdint>

// Input Actions — semantic, game-facing action enum
// Maps raw touch/gesture/keyboard events → game actions
// Game code reads actions[] — never touches TouchTracker directly
enum class InputAction : u8 {
    None = 0,
    Select,        // Tap → select tile / confirm
    Deselect,      // Tap on already-selected → deselect
    SwapUp,
    SwapDown,
    SwapLeft,
    SwapRight,
    Pause,         // Long press / Escape
    Back,          // Cancel / go back
    Confirm,       // Enter / Space
    MenuUp,        // Navigate up
    MenuDown,      // Navigate down
    MenuConfirm,   // Confirm menu selection
    // Focus walk — deliberately NOT MenuUp/MenuDown. Tab used to be emitted as
    // MenuDown (Shift-Tab as SwapUp), which made it indistinguishable from the
    // arrow keys. Every widget that owns "up/down is mine" therefore also owned
    // Tab, and since each of them CLAMPS rather than wraps, focus became a
    // one-way door: Tab into a focused ComboBox/ListBox/TabBar and you could
    // never Tab out again. Splitting them here is the single point where the
    // keyboard contract becomes true in code.
    FocusNext,     // Tab - move focus to the next focusable widget
    FocusPrev,     // Shift-Tab - move focus to the previous one
    // Home / End - jump to the first / last item of whatever owns the value
    // (Slider min/max, ListBox / ComboBox / TabBar first / last). Separate from
    // the caret keys on purpose: a TextField reading Home/End as a CARET move is
    // the same key, and only the editing block's early return tells the two uses
    // apart. See Manager::handle - the editor returns before any widget branch.
    MenuFirst,
    MenuLast,
    // Toggle a dropdown popup: open it if shut, cancel it shut if open. F4 and
    // Alt+Down both mean this (the Windows / browser convention), and both are
    // separate actions from MenuDown for the same reason Tab is: a widget that
    // owns "down is mine" must not also own "Alt+Down is mine", or the chord
    // arrives as two actions and the popup opens and shuts in one keystroke.
    // Closing CANCELS - the highlighted row is not committed. Alt+Down putting
    // the list back the way it was is not the same as Enter saying "yes, this
    // one".
    MenuToggle,
    // Page by one VIEWPORT. A page is defined by what is on screen, not by a
    // fixed count, so it is ListboxMetrics::visible - 1 for a ListBox and the
    // popup's own row cap for a ComboBox (the engine computes it per widget).
    MenuPageUp,
    MenuPageDown,
    // Horizontal nav. Separate from SwapLeft/SwapRight on purpose: Swap* is the
    // TOUCH SWIPE family (their only other producer is GestureType::Swipe*),
    // while every keyboard arrow in here is Menu*. mm_03 / mm_04 deliberately
    // migrated OFF Swap* for keyboard use, and a TabBar is a horizontal strip,
    // so these are what its cells answer to.
    MenuLeft,
    MenuRight,
    _Count
};

// Per-frame input snapshot — processed from queue, consumed by game
// Cache reason: ~160 bytes, fits in L1 cache line
struct InputState {
    // Touch gesture tracking (persistent between frames)
    TouchTracker    touch;

    // Key state arrays (just_pressed/released are transient per frame)
    bool            keys_down[static_cast<size_t>(KeyCode::_Count)];
    bool            keys_just_pressed[static_cast<size_t>(KeyCode::_Count)];
    bool            keys_just_released[static_cast<size_t>(KeyCode::_Count)];

    // Mouse state
    f32           mouse_x, mouse_y;
    f32           mouse_scroll_dx, mouse_scroll_dy;
    // True once a MouseMove has actually arrived. mouse_x/mouse_y are 0,0
    // until then, and on a platform with no pointer at all (iOS, Android) they
    // STAY 0,0 forever - only mm_app_mac.mm ever emits MouseMove. A consumer
    // that reads them unconditionally therefore hit-tests (0,0) every frame,
    // which pinned whatever widget lives in the top-left corner to the hover
    // state and popped its tooltip with no pointer anywhere near it.
    // Not derived from platform #ifdefs: a Mac with no mouse attached should
    // behave the same way.
    bool            has_pointer = false;

    // Key auto-repeat. macOS DOES deliver OS-level repeats (keyDown: fires
    // again), but process() only set keys_just_pressed on a RISING edge, so
    // every one of them was dropped: holding Backspace deleted one byte, once.
    // Every UI key path reads just_pressed, so this belongs here, not in the
    // UI - it also fixes holding Up/Down on a focused ListBox.
    //   repeat_delay: seconds held before the first repeat
    //   repeat_rate:  seconds between repeats after that
    f32           key_repeat_timer = 0.0f;
    f32           key_repeat_delay = 0.4f;
    f32           key_repeat_rate  = 0.05f;

    // Current frame actions
    InputAction     actions[8];
    u8         action_count;
    f32           action_x, action_y;  // position of most recent Select action

    // Text input (typed chars this frame)
    char            text_input[4];
    u8         text_count;

    void init() noexcept {
        touch = TouchTracker{};
        for (auto& k : keys_down) k = false;
        for (auto& k : keys_just_pressed) k = false;
        for (auto& k : keys_just_released) k = false;
        mouse_x = mouse_y = 0.0f;
        mouse_scroll_dx = mouse_scroll_dy = 0.0f;
        has_pointer        = false;
        key_repeat_timer   = 0.0f;
        key_repeat_key     = UINT16_MAX;
        key_repeat_held    = 0.0f;
        key_repeat_started = false;
        action_count = 0;
        action_x = action_y = 0.0f;
        text_count = 0;
    }

    // One key repeats at a time (the first still-held key wins). Enough for
    // every UI path: a text field only ever has one caret, and a held arrow in
    // a list should not fight another held key for the repeat.
    u16        key_repeat_key     = UINT16_MAX;
    f32           key_repeat_held    = 0.0f;
    bool            key_repeat_started = false;

    // Drain event queue, update touch state, map to actions — call once per frame
    void process(InputEventQueue& queue, f32 dt) noexcept {
        // Clear transient per-frame state
        for (auto& k : keys_just_pressed) k = false;
        for (auto& k : keys_just_released) k = false;
        mouse_scroll_dx = mouse_scroll_dy = 0.0f;
        action_count = 0;
        text_count = 0;

        // Drain all pending events from queue
        InputEvent ev;
        while (queue.pop(ev)) {
            switch (ev.type) {
                case InputEventType::TouchDown:
                    touch.on_touch_down(ev.touch_id, ev.x, ev.y);
                    break;
                case InputEventType::TouchMove:
                    touch.on_touch_move(ev.touch_id, ev.x, ev.y);
                    break;
                case InputEventType::TouchUp:
                    touch.on_touch_up(ev.touch_id);
                    break;
                case InputEventType::TouchCancel:
                    touch.on_touch_cancel(ev.touch_id);
                    break;
                case InputEventType::KeyDown:
                    if (!keys_down[static_cast<size_t>(ev.key)]) {
                        keys_just_pressed[static_cast<size_t>(ev.key)] = true;
                    }
                    keys_down[static_cast<size_t>(ev.key)] = true;
                    break;
                case InputEventType::KeyUp:
                    keys_down[static_cast<size_t>(ev.key)] = false;
                    keys_just_released[static_cast<size_t>(ev.key)] = true;
                    if (key_repeat_key == static_cast<u16>(ev.key)) {
                        key_repeat_key     = UINT16_MAX;
                        key_repeat_held    = 0.0f;
                        key_repeat_started = false;
                    }
                    break;
                case InputEventType::MouseMove:
                    mouse_x      = ev.x;
                    mouse_y      = ev.y;
                    has_pointer  = true;
                    break;
                case InputEventType::MouseScroll:
                    mouse_scroll_dx += ev.dx;
                    mouse_scroll_dy += ev.dy;
                    break;
                case InputEventType::TextInput:
                    if (text_count < 4)
                        text_input[text_count++] = ev.ch;
                    break;
                default: break;
            }
        }

        // Update touch tracker durations
        touch.update(dt);

        // Map ended gestures to semantic actions
        for (u8 i = 0; i < touch.active_count; ++i) {
            auto& finger = touch.fingers[i];
            if (finger.phase != TouchPhase::Ended) continue;

            InputAction action = InputAction::None;
            switch (finger.type) {
                case GestureType::Tap:
                    action = InputAction::Select;
                    action_x = finger.curr_x;
                    action_y = finger.curr_y;
                    break;
                case GestureType::SwipeUp:
                    action = InputAction::SwapUp;
                    break;
                case GestureType::SwipeDown:
                    action = InputAction::SwapDown;
                    break;
                case GestureType::SwipeLeft:
                    action = InputAction::SwapLeft;
                    break;
                case GestureType::SwipeRight:
                    action = InputAction::SwapRight;
                    break;
                case GestureType::LongPress:
                    action = InputAction::Pause;
                    break;
                default: break;
            }

            if (action != InputAction::None && action_count < 8) {
                actions[action_count++] = action;
                touch.consume(i);
            }
        }

        // ── Key auto-repeat ───────────────────────────────────────
        // Raise keys_just_pressed again while a key is held past the delay.
        // Every UI key path reads just_pressed, so this is the one place that
        // can make holding a key do anything at all.
        //
        // The delay->rate switch is a LATCH rather than a comparison against the
        // running timer. The comparison (`held < delay ? delay : rate`) happens
        // AFTER dt is added, so it also selects the rate correctly once held
        // crosses the delay - the latch is not fixing an observed stutter, it
        // removes the dependency on that ordering so a large dt or a future
        // clamp cannot quietly put the key back on the delay.
        if (key_repeat_key == UINT16_MAX) {
            for (size_t k = 1; k < static_cast<size_t>(KeyCode::_Count); ++k) {
                if (keys_down[k]) {
                    key_repeat_key           = static_cast<u16>(k);
                    key_repeat_held          = 0.0f;
                    key_repeat_started       = false;
                    break;
                }
            }
        } else {
            key_repeat_held += dt;
            const f32 limit = key_repeat_started ? key_repeat_rate : key_repeat_delay;
            if (key_repeat_held >= limit) {
                keys_just_pressed[key_repeat_key] = true;
                key_repeat_held -= limit;
                key_repeat_started = true;
            }
        }

        // Map keyboard shortcuts to actions.
        //
        // W and S are MenuUp/MenuDown AND letters, so a UI with a focused text
        // field sees both for one keystroke. That collision is the reason Tab
        // used to be emitted as MenuDown too - a UI could not tell "the player
        // pressed Down" from "the player pressed Tab". Tab now has its own
        // actions (FocusNext/FocusPrev), so the editor no longer needs a
        // text_count guard to tell them apart: W/S stay MenuUp/MenuDown and
        // stay inside the field, Tab leaves. Escape and Enter are needed by the
        // editor, so they are always mapped.
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Escape)]) {
            if (action_count < 8) actions[action_count++] = InputAction::Pause;
        }
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Enter)] ||
            keys_just_pressed[static_cast<size_t>(KeyCode::Space)]) {
            if (action_count < 8) actions[action_count++] = InputAction::Confirm;
        }
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)]) {
            if (action_count < 8) actions[action_count++] = InputAction::Back;
        }
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Up)] ||
            keys_just_pressed[static_cast<size_t>(KeyCode::W)]) {
            if (action_count < 8) actions[action_count++] = InputAction::MenuUp;
        }
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Down)] ||
            keys_just_pressed[static_cast<size_t>(KeyCode::S)]) {
            // Alt+Down is "toggle the dropdown", NOT "down". Emitting both would
            // hand a ComboBox two actions for one keystroke: MenuDown opens the
            // popup and moves the highlight, then MenuToggle shuts it again.
            // A chord delivers ONE meaning - the same rule as Tab vs MenuDown.
            const bool alt = keys_down[static_cast<size_t>(KeyCode::Alt)];
            if (keys_just_pressed[static_cast<size_t>(KeyCode::Down)] && alt) {
                if (action_count < 8) actions[action_count++] = InputAction::MenuToggle;
            } else if (action_count < 8) {
                actions[action_count++] = InputAction::MenuDown;
            }
        }
        // F4 and Alt+Down are the same action, so a host that wants "open the
        // dropdown" binds one thing. F4 arrives here directly; Alt+Down is
        // handled in the Down branch above so it cannot also be MenuDown.
        if (keys_just_pressed[static_cast<size_t>(KeyCode::F4)]) {
            if (action_count < 8) actions[action_count++] = InputAction::MenuToggle;
        }
        // Horizontal nav for a horizontal strip (TabBar). Left/Right used to
        // produce NO action at all, which is why mm_03's horizontal swap had no
        // desktop path and mm_07's caret reads the raw keys - the caret is safe
        // because the TextField editing block returns before any widget branch.
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Left)]) {
            if (action_count < 8) actions[action_count++] = InputAction::MenuLeft;
        }
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Right)]) {
            if (action_count < 8) actions[action_count++] = InputAction::MenuRight;
        }
        // Tab walks focus. Shift-Tab walks it back. Deliberately NOT
        // MenuDown/SwapUp - see the FocusNext comment in the enum above.
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Tab)]) {
            if (action_count < 8) actions[action_count++] = keys_down[static_cast<size_t>(KeyCode::Shift)] ? InputAction::FocusPrev : InputAction::FocusNext;
        }
        // Home / End are first / last. They have NO keyboard producer until now,
        // even though the TextField editor has been reading the raw keys for the
        // caret since Home became reachable (macOS sends it as Fn+Left, which is
        // why the host could only ever see Left before). One key, two meanings,
        // separated by the editing block's early return - the same split W/S and
        // Tab needed, and the reason these get their own action rather than being
        // read raw in each widget.
        if (keys_just_pressed[static_cast<size_t>(KeyCode::Home)]) {
            if (action_count < 8) actions[action_count++] = InputAction::MenuFirst;
        }
        if (keys_just_pressed[static_cast<size_t>(KeyCode::End)]) {
            if (action_count < 8) actions[action_count++] = InputAction::MenuLast;
        }
        if (keys_just_pressed[static_cast<size_t>(KeyCode::PageUp)]) {
            if (action_count < 8) actions[action_count++] = InputAction::MenuPageUp;
        }
        if (keys_just_pressed[static_cast<size_t>(KeyCode::PageDown)]) {
            if (action_count < 8) actions[action_count++] = InputAction::MenuPageDown;
        }
    }

    bool is_down(KeyCode key) const noexcept {
        return keys_down[static_cast<size_t>(key)];
    }

    bool just_pressed(KeyCode key) const noexcept {
        return keys_just_pressed[static_cast<size_t>(key)];
    }

    bool just_released(KeyCode key) const noexcept {
        return keys_just_released[static_cast<size_t>(key)];
    }
};

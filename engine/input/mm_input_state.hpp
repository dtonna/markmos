#pragma once
#include "mm_input_event.hpp"
#include "mm_touch_gesture.hpp"
#include <cstdint>

// Input Actions — semantic, game-facing action enum
// Maps raw touch/gesture/keyboard events → game actions
// Game code reads actions[] — never touches TouchTracker directly
enum class InputAction : uint8_t {
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
    float           mouse_x, mouse_y;
    float           mouse_scroll_dx, mouse_scroll_dy;

    // Current frame actions
    InputAction     actions[8];
    uint8_t         action_count;
    float           action_x, action_y;  // position of most recent Select action

    // Text input (typed chars this frame)
    char            text_input[4];
    uint8_t         text_count;

    void init() noexcept {
        touch = TouchTracker{};
        for (auto& k : keys_down) k = false;
        for (auto& k : keys_just_pressed) k = false;
        for (auto& k : keys_just_released) k = false;
        mouse_x = mouse_y = 0.0f;
        mouse_scroll_dx = mouse_scroll_dy = 0.0f;
        action_count = 0;
        action_x = action_y = 0.0f;
        text_count = 0;
    }

    // Drain event queue, update touch state, map to actions — call once per frame
    void process(InputEventQueue& queue, float dt) noexcept {
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
                    break;
                case InputEventType::MouseMove:
                    mouse_x = ev.x;
                    mouse_y = ev.y;
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
        for (uint8_t i = 0; i < touch.active_count; ++i) {
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

        // Map keyboard shortcuts to actions
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
            if (action_count < 8) actions[action_count++] = InputAction::MenuDown;
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

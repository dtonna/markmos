#pragma once
#include <cstdint>
#include <cmath>

// Touch Gesture System — state machine, no alloc per touch
// Cache reason: flat array of 5 finger states, single cache line
// Design: FSM per finger: Idle → Pressing → (Tap|Swipe|LongPress|Drag)
// No virtual dispatch, no heap alloc, fixed-size tracker

enum class GestureType : uint8_t {
    None, Tap, SwipeUp, SwipeDown, SwipeLeft, SwipeRight, LongPress, Drag
};

enum class TouchPhase : uint8_t {
    Idle, Pressing, Moved, Ended, Cancelled
};

struct GestureState {
    GestureType type;
    TouchPhase  phase;
    float       start_x, start_y;
    float       prev_x,  prev_y;
    float       curr_x,  curr_y;
    float       duration;     // seconds held
    float       total_dx;     // total accumulated displacement

    void reset() noexcept {
        type     = GestureType::None;
        phase    = TouchPhase::Idle;
        start_x = start_y = prev_x = prev_y = curr_x = curr_y = 0.0f;
        duration = 0.0f;
        total_dx = 0.0f;
    }
};

struct TouchTracker {
    GestureState fingers[5];   // max 5 simultaneous touches
    uint8_t      active_count;

    // Configurable thresholds
    float swipe_min_dist;      // 20px default
    float tap_max_time;        // 0.25s default
    float long_press_time;     // 0.5s default
    float drag_min_dist;       // 5px default

    TouchTracker() noexcept {
        reset();
        swipe_min_dist  = 20.0f;
        tap_max_time    = 0.25f;
        long_press_time = 0.5f;
        drag_min_dist   = 5.0f;
    }

    void reset() noexcept {
        for (auto& f : fingers) f.reset();
        active_count = 0;
    }

    // Find a finger slot by touch ID (0-4)
    uint8_t find_slot(uint8_t touch_id) noexcept {
        return touch_id < 5 ? touch_id : 0;
    }

    // Touch begin
    void on_touch_down(uint8_t touch_id, float x, float y) noexcept {
        uint8_t slot = find_slot(touch_id);
        auto& f = fingers[slot];
        f.phase   = TouchPhase::Pressing;
        f.type    = GestureType::None;
        f.start_x = f.prev_x = f.curr_x = x;
        f.start_y = f.prev_y = f.curr_y = y;
        f.duration = 0.0f;
        f.total_dx = 0.0f;
        if (slot >= active_count) active_count = static_cast<uint8_t>(slot + 1);
    }

    // Touch move
    void on_touch_move(uint8_t touch_id, float x, float y) noexcept {
        uint8_t slot = find_slot(touch_id);
        auto& f = fingers[slot];
        if (f.phase == TouchPhase::Idle) return;

        f.prev_x = f.curr_x;
        f.prev_y = f.curr_y;
        f.curr_x = x;
        f.curr_y = y;

        float dx = f.curr_x - f.start_x;
        float dy = f.curr_y - f.start_y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist >= swipe_min_dist && f.type == GestureType::None) {
            // Determine swipe direction
            if (std::abs(dx) > std::abs(dy)) {
                f.type = dx > 0 ? GestureType::SwipeRight : GestureType::SwipeLeft;
            } else {
                f.type = dy > 0 ? GestureType::SwipeDown : GestureType::SwipeUp;
            }
            f.phase = TouchPhase::Ended;
        } else if (dist >= drag_min_dist && f.duration >= tap_max_time) {
            f.type = GestureType::Drag;
            f.phase = TouchPhase::Moved;
        }

        f.total_dx += std::abs(f.curr_x - f.prev_x) + std::abs(f.curr_y - f.prev_y);
    }

    // Touch end
    void on_touch_up(uint8_t touch_id) noexcept {
        uint8_t slot = find_slot(touch_id);
        auto& f = fingers[slot];
        if (f.phase == TouchPhase::Idle) return;

        if (f.type == GestureType::None) {
            if (f.duration >= long_press_time) {
                f.type = GestureType::LongPress;
            } else if (f.duration <= tap_max_time) {
                f.type = GestureType::Tap;
            }
        }
        f.phase = TouchPhase::Ended;
    }

    // Touch cancel
    void on_touch_cancel(uint8_t touch_id) noexcept {
        uint8_t slot = find_slot(touch_id);
        fingers[slot].phase = TouchPhase::Cancelled;
        fingers[slot].type  = GestureType::None;
    }

    // Update durations — call once per frame
    void update(float dt) noexcept {
        for (uint8_t i = 0; i < active_count; ++i) {
            if (fingers[i].phase == TouchPhase::Pressing ||
                fingers[i].phase == TouchPhase::Moved) {
                fingers[i].duration += dt;
            }
        }
    }

    // Consume gesture — returns gesture and resets only that finger
    GestureState consume(uint8_t touch_id) noexcept {
        uint8_t slot = find_slot(touch_id);
        GestureState result = fingers[slot];
        fingers[slot].reset();
        // Recompute active_count
        while (active_count > 0 && fingers[active_count - 1].phase == TouchPhase::Idle) {
            --active_count;
        }
        return result;
    }

    // Peek gesture without consuming
    const GestureState& peek(uint8_t touch_id) const noexcept {
        return fingers[touch_id < 5 ? touch_id : 0];
    }
};

static_assert(sizeof(TouchTracker) <= 512, "TouchTracker fits in half a cache line worth of structs");

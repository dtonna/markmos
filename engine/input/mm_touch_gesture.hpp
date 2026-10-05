// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include "core/mm_types.h"
#include <cmath>

// Touch Gesture System — state machine, no alloc per touch
// Cache reason: flat array of 5 finger states, single cache line
// Design: FSM per finger: Idle → Pressing → (Tap|Swipe|LongPress|Drag)
// No virtual dispatch, no heap alloc, fixed-size tracker

enum class GestureType : u8 {
    None, Tap, SwipeUp, SwipeDown, SwipeLeft, SwipeRight, LongPress, Drag
};

enum class TouchPhase : u8 {
    Idle, Pressing, Moved, Ended, Cancelled
};

struct GestureState {
    GestureType type;
    TouchPhase  phase;
    f32       start_x, start_y;
    f32       prev_x,  prev_y;
    f32       curr_x,  curr_y;
    f32       duration;     // seconds held
    f32       total_dx;     // total accumulated displacement

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
    u8      active_count;

    // Configurable thresholds
    f32 swipe_min_dist;      // 20px default
    f32 tap_max_time;        // 0.25s default
    f32 long_press_time;     // 0.5s default
    f32 drag_min_dist;       // 5px default

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
    u8 find_slot(u8 touch_id) noexcept {
        return touch_id < 5 ? touch_id : 0;
    }

    // Touch begin
    void on_touch_down(u8 touch_id, f32 x, f32 y) noexcept {
        u8 slot = find_slot(touch_id);
        auto& f = fingers[slot];
        f.phase   = TouchPhase::Pressing;
        f.type    = GestureType::None;
        f.start_x = f.prev_x = f.curr_x = x;
        f.start_y = f.prev_y = f.curr_y = y;
        f.duration = 0.0f;
        f.total_dx = 0.0f;
        if (slot >= active_count) active_count = static_cast<u8>(slot + 1);
    }

    // Touch move
    void on_touch_move(u8 touch_id, f32 x, f32 y) noexcept {
        u8 slot = find_slot(touch_id);
        auto& f = fingers[slot];
        if (f.phase == TouchPhase::Idle) return;

        f.prev_x = f.curr_x;
        f.prev_y = f.curr_y;
        f.curr_x = x;
        f.curr_y = y;

        f32 dx = f.curr_x - f.start_x;
        f32 dy = f.curr_y - f.start_y;
        f32 dist = __builtin_sqrtf(dx * dx + dy * dy);

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
    void on_touch_up(u8 touch_id) noexcept {
        u8 slot = find_slot(touch_id);
        auto& f = fingers[slot];
        if (f.phase == TouchPhase::Idle) return;

        if (f.type == GestureType::None) {
            if (f.duration >= long_press_time) {
                f.type = GestureType::LongPress;
            } else {
                // Any completed press shorter than long_press → Tap
                f.type = GestureType::Tap;
            }
        }
        f.phase = TouchPhase::Ended;
    }

    // Touch cancel
    void on_touch_cancel(u8 touch_id) noexcept {
        u8 slot = find_slot(touch_id);
        fingers[slot].phase = TouchPhase::Cancelled;
        fingers[slot].type  = GestureType::None;
    }

    // Update durations — call once per frame.
    // Also re-evaluate drag: a finger that moved to the drag threshold
    // in one frame (before tap_max_time elapsed) and then stopped needs
    // to be reclassified once enough time passes.
    void update(f32 dt) noexcept {
        for (u8 i = 0; i < active_count; ++i) {
            if (fingers[i].phase == TouchPhase::Pressing ||
                fingers[i].phase == TouchPhase::Moved) {
                fingers[i].duration += dt;
            }
            if (fingers[i].phase == TouchPhase::Pressing &&
                fingers[i].type == GestureType::None) {
                f32 dx   = fingers[i].curr_x - fingers[i].start_x;
                f32 dy   = fingers[i].curr_y - fingers[i].start_y;
                f32 dist = __builtin_sqrtf(dx * dx + dy * dy);
                if (dist >= drag_min_dist && fingers[i].duration >= tap_max_time) {
                    fingers[i].type  = GestureType::Drag;
                    fingers[i].phase = TouchPhase::Moved;
                }
            }
        }
    }

    // Consume gesture — returns gesture and resets only that finger
    GestureState consume(u8 touch_id) noexcept {
        u8 slot = find_slot(touch_id);
        GestureState result = fingers[slot];
        fingers[slot].reset();
        // Recompute active_count
        while (active_count > 0 && fingers[active_count - 1].phase == TouchPhase::Idle) {
            --active_count;
        }
        return result;
    }

    // Peek gesture without consuming
    const GestureState& peek(u8 touch_id) const noexcept {
        return fingers[touch_id < 5 ? touch_id : 0];
    }
};

static_assert(sizeof(TouchTracker) <= 512, "TouchTracker fits in half a cache line worth of structs");

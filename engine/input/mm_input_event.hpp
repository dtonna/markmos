// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <atomic>
#include "core/mm_types.h"
#include <cstdint>

// Input Event Types — tagged union, 32 bytes each
// SPSC ring buffer queues events from platform callbacks to game loop
// No virtual, no alloc, trivially copyable

enum class InputEventType : u8 { None, TouchDown, TouchMove, TouchUp, TouchCancel, KeyDown, KeyUp, MouseMove, MouseScroll, TextInput };

enum class KeyCode : u16 {
    Unknown = 0,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    D0,
    D1,
    D2,
    D3,
    D4,
    D5,
    D6,
    D7,
    D8,
    D9,
    Left,
    Right,
    Up,
    Down,
    Shift,
    Ctrl,
    Alt,
    Space,
    Enter,
    Escape,
    Backspace,
    Tab,
    // Added for TextField editing. Home/End were unreachable before: macOS
    // delivers them as Fn+Left / Fn+Right, so the host could only ever see
    // Left/Right and could not tell the two apart. Delete is a distinct
    // virtual keycode from Backspace on every platform.
    Delete,
    Home,
    End,
    // F4 + PageUp/PageDown complete the standard ComboBox / scrolling-list
    // contract (see InputAction::MenuToggle and MenuPageUp). F4 has no other
    // meaning in this engine, and macOS gives PageUp/PageDown real virtual
    // keycodes of their own (0x74 / 0x79 in HIToolbox Events.h) - they are NOT
    // the arrow keys, so nothing collides with Down.
    F4,
    PageUp,
    PageDown,
    GameA,
    GameB,
    GameX,
    GameY,
    GameLB,
    GameRB,
    GameLT,
    GameRT,
    GameStart,
    GameSelect,
    GameHome,
    _Count
};

struct InputEvent {
    InputEventType    type;
    u8           touch_id;
    char              ch;         // TextInput character
    u8           _pad0;
    f32             x, y;
    f32             dx, dy;
    KeyCode           key;
    u16          _pad1;

    static InputEvent make_touch_down(u8 id, f32 x, f32 y) noexcept {
        InputEvent e{};
        e.type     = InputEventType::TouchDown;
        e.touch_id = id;
        e.x        = x;
        e.y        = y;
        return e;
    }

    static InputEvent make_touch_move(u8 id, f32 x, f32 y) noexcept {
        InputEvent e{};
        e.type     = InputEventType::TouchMove;
        e.touch_id = id;
        e.x        = x;
        e.y        = y;
        return e;
    }

    static InputEvent make_touch_up(u8 id) noexcept {
        InputEvent e{};
        e.type     = InputEventType::TouchUp;
        e.touch_id = id;
        return e;
    }

    static InputEvent make_touch_cancel(u8 id) noexcept {
        InputEvent e{};
        e.type     = InputEventType::TouchCancel;
        e.touch_id = id;
        return e;
    }

    static InputEvent make_key_down(KeyCode k) noexcept {
        InputEvent e{};
        e.type = InputEventType::KeyDown;
        e.key  = k;
        return e;
    }

    static InputEvent make_key_up(KeyCode k) noexcept {
        InputEvent e{};
        e.type = InputEventType::KeyUp;
        e.key  = k;
        return e;
    }

    static InputEvent make_mouse_move(f32 x, f32 y) noexcept {
        InputEvent e{};
        e.type = InputEventType::MouseMove;
        e.x    = x;
        e.y    = y;
        return e;
    }

    static InputEvent make_mouse_scroll(f32 dx, f32 dy) noexcept {
        InputEvent e{};
        e.type = InputEventType::MouseScroll;
        e.dx   = dx;
        e.dy   = dy;
        return e;
    }

    static InputEvent make_text_input(char c) noexcept {
        InputEvent e{};
        e.type = InputEventType::TextInput;
        e.ch   = c;
        return e;
    }
};

static_assert(sizeof(InputEvent) <= 32, "InputEvent <= 32 bytes");

// Lock-free SPSC ring buffer — 256 event slots
// Push from platform callback thread, pop from game loop thread
struct InputEventQueue {
    static constexpr u32 kCapacity = 256;

    alignas(64) std::atomic<u32> head{0};
    alignas(64) std::atomic<u32> tail{0};
    alignas(64) InputEvent slots[kCapacity];

    bool push(const InputEvent &event) noexcept {
        u32 h    = head.load(std::memory_order_relaxed);
        u32 next = (h + 1) % kCapacity;

        if (next == tail.load(std::memory_order_acquire)) {
            return false;
        }
        slots[h] = event;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(InputEvent &out) noexcept {
        u32 t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) {
            return false;
        }
        out = slots[t];
        tail.store((t + 1) % kCapacity, std::memory_order_release);
        return true;
    }

    void reset() noexcept {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_release);
    }
};

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <atomic>
#include <cstdint>

// Input Event Types — tagged union, 32 bytes each
// SPSC ring buffer queues events from platform callbacks to game loop
// No virtual, no alloc, trivially copyable

enum class InputEventType : uint8_t { None, TouchDown, TouchMove, TouchUp, TouchCancel, KeyDown, KeyUp, MouseMove, MouseScroll, TextInput };

enum class KeyCode : uint16_t {
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
    uint8_t           touch_id;
    char              ch;         // TextInput character
    uint8_t           _pad0;
    float             x, y;
    float             dx, dy;
    KeyCode           key;
    uint16_t          _pad1;

    static InputEvent make_touch_down(uint8_t id, float x, float y) noexcept {
        InputEvent e{};
        e.type     = InputEventType::TouchDown;
        e.touch_id = id;
        e.x        = x;
        e.y        = y;
        return e;
    }

    static InputEvent make_touch_move(uint8_t id, float x, float y) noexcept {
        InputEvent e{};
        e.type     = InputEventType::TouchMove;
        e.touch_id = id;
        e.x        = x;
        e.y        = y;
        return e;
    }

    static InputEvent make_touch_up(uint8_t id) noexcept {
        InputEvent e{};
        e.type     = InputEventType::TouchUp;
        e.touch_id = id;
        return e;
    }

    static InputEvent make_touch_cancel(uint8_t id) noexcept {
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

    static InputEvent make_mouse_move(float x, float y) noexcept {
        InputEvent e{};
        e.type = InputEventType::MouseMove;
        e.x    = x;
        e.y    = y;
        return e;
    }

    static InputEvent make_mouse_scroll(float dx, float dy) noexcept {
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
    static constexpr uint32_t kCapacity = 256;

    alignas(64) std::atomic<uint32_t> head{0};
    alignas(64) std::atomic<uint32_t> tail{0};
    alignas(64) InputEvent slots[kCapacity];

    bool push(const InputEvent &event) noexcept {
        uint32_t h    = head.load(std::memory_order_relaxed);
        uint32_t next = (h + 1) % kCapacity;

        if (next == tail.load(std::memory_order_acquire)) {
            return false;
        }
        slots[h] = event;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(InputEvent &out) noexcept {
        uint32_t t = tail.load(std::memory_order_relaxed);
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

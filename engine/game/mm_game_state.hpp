// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "mm_board_grid.hpp"
#include "../input/mm_input_state.hpp"
#include <variant>
#include <cstdint>
#include <array>

// Game State Machine — std::variant, zero virtual
// Cache reason:
//   - std::visit generates jump table, not virtual dispatch
//   - State data fits in cache (each state is small POD)
//   - Stack-based overlay allows pause without heap
// Design:
//   - Flat variant, no inheritance, no dynamic_cast
//   - State stack for overlays (pause on top of play)
//   - Transition via std::visit + tagged return
//   - InputState& passed to each update() for action-driven transitions

struct BoardGrid;  // forward decl

// State data — flat structs, no virtual
struct MenuState {
    uint8_t  selected_level;
    uint8_t  scroll_offset;
};

struct PlayState {
    BoardGrid* board;
    uint32_t   score;
    uint32_t   target_score;
    uint8_t    moves_left;
    uint8_t    combo_count;
    float      timer;
};

struct PauseState {};

struct WinState {
    uint32_t score;
    uint8_t  stars;
    float    anim_t;
    float    duration;
};

struct FailState {
    uint8_t  reason;
    float    anim_t;
    float    duration;
};

struct SwapAnimState {
    uint8_t  r1, c1, r2, c2;
    float    t;
    float    duration;
};

// All game states — tagged union
using GameState = std::variant<MenuState, PlayState, PauseState, WinState, FailState, SwapAnimState>;
using GameStateStack = std::array<GameState, 4>;

// State transition helpers — no virtual, pure constexpr
// Each update() receives InputState& for action-driven transitions

template<typename State>
GameState update(const MenuState& s, InputState& input) noexcept {
    uint8_t level = s.selected_level;
    for (uint8_t i = 0; i < input.action_count; ++i) {
        switch (input.actions[i]) {
            case InputAction::MenuUp:
                if (level > 0) --level;
                break;
            case InputAction::MenuDown:
                ++level;
                break;
            case InputAction::Select:
            case InputAction::Confirm:
                return PlayState{nullptr, 0, 1000, 30, 0, 60.0f};
            default: break;
        }
    }
    return MenuState{level, s.scroll_offset};
}

template<typename State>
GameState update(const PlayState& s, InputState& input) noexcept {
    for (uint8_t i = 0; i < input.action_count; ++i) {
        switch (input.actions[i]) {
            case InputAction::Pause:
                return PauseState{};
            case InputAction::Back:
                return MenuState{0, 0};
            default: break;
        }
    }
    // Board-level input (tap/swap) handled by game struct
    return PlayState{s.board, s.score, s.target_score, s.moves_left, s.combo_count, s.timer};
}

template<typename State>
GameState update(const PauseState&, InputState& input) noexcept {
    for (uint8_t i = 0; i < input.action_count; ++i) {
        switch (input.actions[i]) {
            case InputAction::Select:
            case InputAction::Confirm:
            case InputAction::Pause:
                // Resume — no new state, just pop back
                break;
            case InputAction::Back:
                // Quit to menu
                return MenuState{0, 0};
            default: break;
        }
    }
    return PauseState{};
}

template<typename State>
GameState update(const WinState& s, InputState& input) noexcept {
    float t = s.anim_t + 1.0f / 60.0f;
    // Any action dismisses early
    if (t >= s.duration || input.action_count > 0) {
        return MenuState{0, 0};
    }
    return WinState{s.score, s.stars, t, s.duration};
}

template<typename State>
GameState update(const FailState& s, InputState& input) noexcept {
    float t = s.anim_t + 1.0f / 60.0f;
    if (t >= s.duration || input.action_count > 0) {
        return MenuState{0, 0};
    }
    return FailState{s.reason, t, s.duration};
}

template<typename State>
GameState update(const SwapAnimState& s, InputState&) noexcept {
    float t = s.t + 1.0f / 60.0f;
    if (t >= s.duration) {
        return PlayState{nullptr, 0, 0, 0, 0, 0.0f};
    }
    return SwapAnimState{s.r1, s.c1, s.r2, s.c2, t, s.duration};
}

// Dispatch via std::visit — no virtual
inline GameState update_state(const GameState& state, InputState& input) noexcept {
    return std::visit([&input](const auto& s) { return update<decltype(s)>(s, input); }, state);
}

// State stack operations
struct StateStack {
    GameStateStack stack;
    uint8_t        depth = 0;

    void push(GameState s) noexcept {
        if (depth < stack.size()) {
            stack[depth++] = s;
        }
    }

    GameState pop() noexcept {
        if (depth > 0) {
            GameState top = stack[--depth];
            stack[depth] = MenuState{0, 0};
            return top;
        }
        return MenuState{0, 0};
    }

    GameState& current() noexcept { return stack[depth > 0 ? depth - 1 : 0]; }
    const GameState& current() const noexcept { return stack[depth > 0 ? depth - 1 : 0]; }

    void update_all(InputState& input) noexcept {
        // Process from bottom to top; inner states don't get input if top state consumes it
        for (uint8_t i = 0; i < depth; ++i) {
            stack[i] = update_state(stack[i], input);
        }
    }
};

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "mm_event_bus.hpp"
#include "core/mm_types.h"
#include "mm_scene.hpp"
#include <functional>
#include <type_traits>
#include <variant>

// g_event_bus declared in mm_event_bus.hpp (defined in mm_event_bus.cpp).

template <typename... S> struct GameScene {
    static_assert((MainScene<S, GameScene<S...>> && ...), "All types in S... must satisfy the MainScene concept with GameScene<S...> as the game type.");

    using Variant = std::variant<std::monostate, S...>;
    Variant current_scene;
    Variant pending_scene;
    bool    has_pending_scene = false;

    GameScene()               = default;

    template <typename SceneT> void request(SceneT &&s) {
        static_assert(MainScene<SceneT, GameScene<S...>>, "SceneT must satisfy the MainScene concept with GameScene<S...> as the game type.");
        pending_scene     = std::forward<SceneT>(s);
        has_pending_scene = true;
    }

    // The single dispatch point: runs fn(scene) for the current scene,
    // skipping monostate. Keeps update/render/process_pending readable
    // without changing codegen (one jump table, fully inlineable).
    template <typename F> void visit_current(F &&fn) {
        std::visit(
            [&fn](auto &scene) {
                using SceneType = std::decay_t<decltype(scene)>;
                if constexpr (!std::is_same_v<SceneType, std::monostate>) {
                    fn(scene);
                }
            },
            current_scene);
    }

    // Applies a pending request() immediately (exit old + enter new).
    // update() calls this at its start; hosts may call it again after
    // update() so transitions requested mid-frame are visible to UI/render
    // in the SAME frame instead of the next one. No-op when nothing pending.
    void process_pending() {
        if (!has_pending_scene) {
            return;
        }
        visit_current([this](auto &scene) { scene.on_exit(*this); });

        current_scene     = std::move(pending_scene);
        pending_scene     = std::monostate{};
        has_pending_scene = false;

        visit_current([this](auto &scene) { scene.on_enter(*this); });
    }

    void update(f32 dt, InputState &input) {
        g_event_bus.flush(); // Process all queued events before updating the current scene

        process_pending();

        visit_current([this, dt, &input](auto &scene) { scene.update(*this, dt, input); });
    }

    void render() {
        visit_current([this](auto &scene) { scene.render(*this); });
    }
};

template <typename T>
concept LoadingContent = requires(T t, f32 alpha, f32 time, typename T::PhaseForAnim phase) {
    { t.on_enter() } -> std::same_as<void>;
    { t.update(alpha, time) } -> std::same_as<void>;
    { t.load(alpha, time) } -> std::same_as<void>;
};

template <typename GameT, typename ContentT> struct LoadingScene {
    using Factory = std::function<typename GameT::Variant()>;
    Factory  next_scene_factory;
    ContentT content;

    enum class phase { FADE_IN, ANIMATE, FADE_OUT } current_phase = phase::FADE_IN;
    f32 t                                                       = 0.0f;

    void  on_enter(GameT &) noexcept {
        current_phase = phase::FADE_IN;
        t             = 0.0f;
        content.on_enter();
    }

    void on_exit(GameT &) noexcept {
        g_event_bus.off(this);
        content.on_exit();
    }

    void update(GameT &g, f32 dt, InputState &) noexcept {
        t           += dt;
        f32 alpha  = 1.0f;

        if (current_phase == phase::FADE_IN) {
            alpha = t / 0.25f;
        } else if (current_phase == phase::FADE_OUT) {
            alpha = 1.0f - (t / 0.25f);
        }
        content.update(alpha, t);

        if (current_phase == phase::FADE_IN && t > 0.25f) {
            current_phase = phase::ANIMATE;
            t             = 0.0f;
        } else if (current_phase == phase::ANIMATE && t > 1.0f) {
            current_phase = phase::FADE_OUT;
            t             = 0.0f;
        } else if (current_phase == phase::FADE_OUT && t > 0.25f) {
            g.request(next_scene_factory());
        }
    }

    void render(GameT &) noexcept {
        f32 alpha = 1.0f;
        if (current_phase == phase::FADE_IN) {
            alpha = t / 0.25f;
        } else if (current_phase == phase::FADE_OUT) {
            alpha = 1.0f - (t / 0.25f);
        }
        content.render(alpha, t);
    }
};

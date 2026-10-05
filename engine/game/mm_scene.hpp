// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../audio/mm_audio_system.hpp"
#include "core/mm_types.h"
#include "../core/mm_handle.hpp"
#include "../input/mm_input_state.hpp"
#include "../render/mm_sprite.hpp"
#include "../render/mm_sprite_batch.hpp"
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>

// ─── Constants ───────────────────────────────────────────────────
static constexpr u16 MAX_ENTITIES = 4096;

// ─── EntityType ──────────────────────────────────────────────────
enum class EntityType : u8 {
    Generic = 0,
};

// ─── EntityHandle ────────────────────────────────────────────────
using EntityHandle = TypedHandle<class EntityTag>;

// ─── Scene — SoA Entity Pool ─────────────────────────────────────
// Cache reason: SoA layout = sequential hot data (pos/scale/active) in cache,
//               cold data (sprite/color/type) accessed only during render.
// Design: Fixed-capacity pool, swap-with-last despawn, no heap in hot path.
struct Scene {
    // Hot data — updated every frame
    alignas(64) f32 x[MAX_ENTITIES];
    alignas(64) f32 y[MAX_ENTITIES];
    alignas(64) f32 scale_x[MAX_ENTITIES];
    alignas(64) f32 scale_y[MAX_ENTITIES];
    alignas(64) f32 rotation[MAX_ENTITIES];
    alignas(64) u8 active[MAX_ENTITIES];
    alignas(64) u8 layer[MAX_ENTITIES];

    // Cold data — accessed during render / type-specific logic
    u32           color[MAX_ENTITIES];
    EntityType         type[MAX_ENTITIES];
    const SpriteFrame *sprite[MAX_ENTITIES];
    u32           user_data[MAX_ENTITIES]; // game-specific value

    // Per-entity sound IDs (UINT8_MAX = none)
    u8            sfx_spawn[MAX_ENTITIES];
    u8            sfx_hit[MAX_ENTITIES];
    u8            sfx_death[MAX_ENTITIES];

    u16           count;

    void               init() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
    }

    // ─── Spawn ──────────────────────────────────────────────────
    EntityHandle spawn(f32 sx, f32 sy, const SpriteFrame *spr = nullptr) noexcept {
        if (count >= MAX_ENTITIES) {
            return EntityHandle::invalid();
        }
        u16 i   = count++;
        x[i]         = sx;
        y[i]         = sy;
        scale_x[i]   = 1.0f;
        scale_y[i]   = 1.0f;
        rotation[i]  = 0.0f;
        active[i]    = 1;
        layer[i]     = 0;
        color[i]     = 0xFFFFFFFF;
        type[i]      = EntityType::Generic;
        sprite[i]    = spr;
        user_data[i] = 0;
        if (spr) {
            sfx_spawn[i] = spr->sfx_id;
        } else {
            sfx_spawn[i] = UINT8_MAX;
        }
        sfx_hit[i]   = UINT8_MAX;
        sfx_death[i] = UINT8_MAX;
        // Auto-play spawn sound
        if (sfx_spawn[i] != UINT8_MAX) {
            g_audio_system.sfx.play_id(sfx_spawn[i]);
        }
        return EntityHandle{SlotHandle{i, 0, 0}};
    }

    // ─── Despawn (swap-with-last) ───────────────────────────────
    void despawn(EntityHandle h) noexcept {
        if (!h.is_valid()) {
            return;
        }
        despawn_at(h.handle.id);
    }

    void despawn_at(u16 idx) noexcept {
        if (idx >= count) {
            return;
        }
        // Play death sound
        if (sfx_death[idx] != UINT8_MAX) {
            g_audio_system.sfx.play_id(sfx_death[idx]);
        }
        u16 last = count - 1;
        if (idx != last) {
            x[idx]         = x[last];
            y[idx]         = y[last];
            scale_x[idx]   = scale_x[last];
            scale_y[idx]   = scale_y[last];
            rotation[idx]  = rotation[last];
            active[idx]    = active[last];
            layer[idx]     = layer[last];
            color[idx]     = color[last];
            type[idx]      = type[last];
            sprite[idx]    = sprite[last];
            user_data[idx] = user_data[last];
            sfx_spawn[idx] = sfx_spawn[last];
            sfx_hit[idx]   = sfx_hit[last];
            sfx_death[idx] = sfx_death[last];
        }
        --count;
    }

    // ─── Iterate active entities ────────────────────────────────
    // Calls fn(i) for each active entity. Template avoids function pointer overhead.
    template <typename F> void each(F &&fn) const noexcept {
        for (u16 i = 0; i < count; ++i) {
            if (active[i]) {
                fn(i);
            }
        }
    }

    template <typename F> void each(F &&fn) noexcept {
        for (u16 i = 0; i < count; ++i) {
            if (active[i]) {
                fn(i);
            }
        }
    }

    // ─── Fill SpriteBatch ───────────────────────────────────────
    // Adds all active entities that have a sprite to the batch.
    void render(SpriteBatch &batch) const noexcept {
        for (u16 i = 0; i < count; ++i) {
            if (!active[i] || !sprite[i]) {
                continue;
            }
            batch.add_frame(x[i], y[i], scale_x[i], scale_y[i], rotation[i], color[i], layer[i], *sprite[i]);
        }
    }

    // ─── World-space AABB query ─────────────────────────────────
    // Returns count of entities within a rectangular area.
    u16 query_aabb(f32 qx, f32 qy, f32 qw, f32 qh, u16 *out_indices, u16 max_out) const noexcept {
        u16 found = 0;
        for (u16 i = 0; i < count && found < max_out; ++i) {
            if (!active[i]) {
                continue;
            }
            f32 ex = x[i], ey = y[i];
            f32 hs = fmaxf(scale_x[i], scale_y[i]) * 0.5f;
            if (ex + hs >= qx && ex - hs <= qx + qw && ey + hs >= qy && ey - hs <= qy + qh) {
                out_indices[found++] = i;
            }
        }
        return found;
    }

    // ─── Type query ─────────────────────────────────────────────
    u16 find_by_type(EntityType t, u16 *out_indices, u16 max_out) const noexcept {
        u16 found = 0;
        for (u16 i = 0; i < count && found < max_out; ++i) {
            if (active[i] && type[i] == t) {
                out_indices[found++] = i;
            }
        }
        return found;
    }
};
template <typename T, typename G>
concept MainScene = requires(T s, G &g, f32 dt, InputState &input) {
    { s.on_enter(g) } -> std::same_as<void>;
    { s.on_exit(g) } -> std::same_as<void>;
    { s.update(g, dt, input) } -> std::same_as<void>;
    { s.render(g) } -> std::same_as<void>;
};

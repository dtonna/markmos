// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Ember pool — extracted from mm_06_shader_lab's fire light (F mode).
// Fixed-count fireflies orbiting an anchor: rise + sine sway, yellow-white
// newborns aging to deep red, respawn on death (endless flame).
// Header-only, no alloc, no exceptions. Draw with SPRITEADDITIVE +
// a radial glow texture (see MakeEmberGlowTexture) like the lab does.

#pragma once
#include "../math/mm_math.h"
#include "../math/mm_vec2.h"
#include <cstdint>
#include <cmath>

static constexpr u16 EMBER_POOL_MAX = 64;
static constexpr u32 EMBER_GLOW_SIZE = 64;

struct EmberParticle {
    mm_math::vec2 pos; // position (screen points)
    mm_math::vec2 vel; // velocity (points/sec, vel.y negative = rises)
    f32 life, max_life;// remaining / total life (seconds)
    f32 size;          // quad size (points)
    f32 seed;          // per-particle phase for sway
};

struct EmberPool {
    EmberParticle embers[EMBER_POOL_MAX];
    u16      count     = 0;
    f32         anchor_x  = 0.0f;
    f32         anchor_y  = 0.0f;
    u32      rng       = 12345u;

    f32 rand01() noexcept {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<f32>(rng >> 8) / 16777216.0f;
    }

    void init(f32 ax, f32 ay, u16 n, u32 seed) noexcept {
        anchor_x = ax;
        anchor_y = ay;
        rng      = seed ? seed : 12345u;
        count    = (n > EMBER_POOL_MAX) ? EMBER_POOL_MAX : n;
        for (u16 i = 0; i < count; ++i) {
            respawn(i);
            embers[i].life = rand01() * embers[i].max_life; // stagger phases
        }
    }

    void respawn(u16 i) noexcept {
        if (i >= count) {
            return;
        }
        f32           a      = rand01() * mm_math::MM_TWO_PI;
        f32           r      = rand01() * 30.0f;
        EmberParticle  &e      = embers[i];
        e.pos.x               = anchor_x + __builtin_cosf(a) * r;
        e.pos.y               = anchor_y + __builtin_sinf(a) * r;
        e.vel.x               = (rand01() - 0.5f) * 20.0f;
        e.vel.y               = -40.0f - rand01() * 40.0f;
        e.max_life            = 1.0f + rand01() * 1.5f;
        e.life                = e.max_life;
        e.size                = 10.0f + rand01() * 18.0f;
        e.seed                = rand01() * mm_math::MM_TWO_PI;
    }

    void update(f32 dt, f32 time) noexcept {
        for (u16 i = 0; i < count; ++i) {
            EmberParticle &e = embers[i];
            e.life -= dt;
            if (e.life <= 0.0f) {
                respawn(i);
                continue;
            }
            e.pos.x += (e.vel.x + __builtin_sinf(time * 3.0f + e.seed) * 20.0f) * dt;
            e.pos.y += e.vel.y * dt;
        }
    }
};

// Color ramp: yellow-white newborns -> deep red elders, alpha = life frac.
// t = life / max_life in [0, 1]; output packed 0xAARRGGBB.
static inline u32 EmberColor(f32 t) noexcept {
    u32 base = (t > 0.5f) ? 0xFFFFAA33u : 0xFFFF4400u;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    u32 alpha = static_cast<u32>(t * 255.0f);
    return (base & 0x00FFFFFFu) | (alpha << 24);
}

// Display size shrinks as the ember ages: s = size * (0.5 + 0.5 * t).
static inline f32 EmberSize(const EmberParticle &e) noexcept {
    f32 t = (e.max_life > 0.0f) ? (e.life / e.max_life) : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    return e.size * (0.5f + 0.5f * t);
}

// Soft radial glow texture (white core -> transparent edge), 64x64.
// Tint per ember via vertex color; sample with linear filtering.
static inline void MakeEmberGlowTexture(u32 out_px[EMBER_GLOW_SIZE * EMBER_GLOW_SIZE]) noexcept {
    for (u32 y = 0; y < EMBER_GLOW_SIZE; ++y) {
        for (u32 x = 0; x < EMBER_GLOW_SIZE; ++x) {
            f32 dx = (static_cast<f32>(x) + 0.5f - EMBER_GLOW_SIZE * 0.5f) / (EMBER_GLOW_SIZE * 0.5f);
            f32 dy = (static_cast<f32>(y) + 0.5f - EMBER_GLOW_SIZE * 0.5f) / (EMBER_GLOW_SIZE * 0.5f);
            f32 r  = __builtin_sqrtf(dx * dx + dy * dy);
            f32 a  = 1.0f - r;
            if (a < 0.0f) {
                a = 0.0f;
            }
            a *= a; // tighter falloff
            u32 ai        = static_cast<u32>(a * 255.0f + 0.5f);
            out_px[y * EMBER_GLOW_SIZE + x] = (ai << 24) | 0x00FFFFFFu;
        }
    }
}

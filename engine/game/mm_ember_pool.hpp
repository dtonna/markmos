// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Ember pool — extracted from mm_06_shader_lab's fire light (F mode).
// Fixed-count fireflies orbiting an anchor: rise + sine sway, yellow-white
// newborns aging to deep red, respawn on death (endless flame).
// Header-only, no alloc, no exceptions. Draw with SPRITEADDITIVE +
// a radial glow texture (see MakeEmberGlowTexture) like the lab does.

#pragma once
#include "../math/mm_math.h"
#include <cstdint>
#include <cmath>

static constexpr uint16_t EMBER_POOL_MAX = 64;
static constexpr uint32_t EMBER_GLOW_SIZE = 64;

struct EmberParticle {
    float x, y;          // position (screen points)
    float vx, vy;        // velocity (points/sec, vy negative = rises)
    float life, max_life;// remaining / total life (seconds)
    float size;          // quad size (points)
    float seed;          // per-particle phase for sway
};

struct EmberPool {
    EmberParticle embers[EMBER_POOL_MAX];
    uint16_t      count     = 0;
    float         anchor_x  = 0.0f;
    float         anchor_y  = 0.0f;
    uint32_t      rng       = 12345u;

    float rand01() noexcept {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(rng >> 8) / 16777216.0f;
    }

    void init(float ax, float ay, uint16_t n, uint32_t seed) noexcept {
        anchor_x = ax;
        anchor_y = ay;
        rng      = seed ? seed : 12345u;
        count    = (n > EMBER_POOL_MAX) ? EMBER_POOL_MAX : n;
        for (uint16_t i = 0; i < count; ++i) {
            respawn(i);
            embers[i].life = rand01() * embers[i].max_life; // stagger phases
        }
    }

    void respawn(uint16_t i) noexcept {
        if (i >= count) {
            return;
        }
        float           a      = rand01() * mm_math::MM_TWO_PI;
        float           r      = rand01() * 30.0f;
        EmberParticle  &e      = embers[i];
        e.x                   = anchor_x + std::cos(a) * r;
        e.y                   = anchor_y + std::sin(a) * r;
        e.vx                  = (rand01() - 0.5f) * 20.0f;
        e.vy                  = -40.0f - rand01() * 40.0f;
        e.max_life            = 1.0f + rand01() * 1.5f;
        e.life                = e.max_life;
        e.size                = 10.0f + rand01() * 18.0f;
        e.seed                = rand01() * mm_math::MM_TWO_PI;
    }

    void update(float dt, float time) noexcept {
        for (uint16_t i = 0; i < count; ++i) {
            EmberParticle &e = embers[i];
            e.life -= dt;
            if (e.life <= 0.0f) {
                respawn(i);
                continue;
            }
            e.x += (e.vx + std::sin(time * 3.0f + e.seed) * 20.0f) * dt;
            e.y += e.vy * dt;
        }
    }
};

// Color ramp: yellow-white newborns -> deep red elders, alpha = life frac.
// t = life / max_life in [0, 1]; output packed 0xAARRGGBB.
static inline uint32_t EmberColor(float t) noexcept {
    uint32_t base = (t > 0.5f) ? 0xFFFFAA33u : 0xFFFF4400u;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    uint32_t alpha = static_cast<uint32_t>(t * 255.0f);
    return (base & 0x00FFFFFFu) | (alpha << 24);
}

// Display size shrinks as the ember ages: s = size * (0.5 + 0.5 * t).
static inline float EmberSize(const EmberParticle &e) noexcept {
    float t = (e.max_life > 0.0f) ? (e.life / e.max_life) : 0.0f;
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
static inline void MakeEmberGlowTexture(uint32_t out_px[EMBER_GLOW_SIZE * EMBER_GLOW_SIZE]) noexcept {
    for (uint32_t y = 0; y < EMBER_GLOW_SIZE; ++y) {
        for (uint32_t x = 0; x < EMBER_GLOW_SIZE; ++x) {
            float dx = (static_cast<float>(x) + 0.5f - EMBER_GLOW_SIZE * 0.5f) / (EMBER_GLOW_SIZE * 0.5f);
            float dy = (static_cast<float>(y) + 0.5f - EMBER_GLOW_SIZE * 0.5f) / (EMBER_GLOW_SIZE * 0.5f);
            float r  = std::sqrt(dx * dx + dy * dy);
            float a  = 1.0f - r;
            if (a < 0.0f) {
                a = 0.0f;
            }
            a *= a; // tighter falloff
            uint32_t ai        = static_cast<uint32_t>(a * 255.0f + 0.5f);
            out_px[y * EMBER_GLOW_SIZE + x] = (ai << 24) | 0x00FFFFFFu;
        }
    }
}

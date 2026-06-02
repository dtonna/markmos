// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <random>

// Particle System — GPU Instanced, SoA, Batch SIMD Update
// Cache reason:
//   - SoA layout: px[], py[], vx[], vy[], life[] — SIMD-friendly contiguous arrays
//   - Update loop = 5 sequential float arrays × MAX_PARTICLES → prefetch streams
//   - Single instanced draw call for all active particles
//   - No per-particle object overhead
// Design:
//   - MAX_PARTICLES = 4096 (32KB per float array × 8 = 256KB hot data)
//   - Swap-with-last despawn (O(1) remove)
//   - Spawn burst: writes sequentially into active range, no heap alloc
//   - GPU: single instance buffer upload + single draw call

static constexpr uint16_t MAX_PARTICLES = 4096;

struct alignas(64) ParticlePool {
    // Hot data — updated every frame, SIMD batches
    alignas(64) float px       [MAX_PARTICLES];
    alignas(64) float py       [MAX_PARTICLES];
    alignas(64) float vx       [MAX_PARTICLES];
    alignas(64) float vy       [MAX_PARTICLES];
    alignas(64) float life     [MAX_PARTICLES];
    alignas(64) float life_max [MAX_PARTICLES];
    alignas(64) float scale    [MAX_PARTICLES];
    alignas(64) float initial_scale[MAX_PARTICLES];
    alignas(64) float rotation [MAX_PARTICLES];
    alignas(64) uint32_t color [MAX_PARTICLES];   // RGBA packed
    alignas(64) uint8_t  atlas_id [MAX_PARTICLES]; // sprite index in atlas
    alignas(64) uint8_t  active   [MAX_PARTICLES];

    uint16_t count;

    ParticlePool() { reset(); }

    void reset() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
    }

    // Spawn a single particle — O(1), no alloc
    void spawn(float x, float y, float vx_, float vy_, float life_,
               float sc, float rot, uint32_t col, uint8_t atlas = 0) noexcept {
        if (count >= MAX_PARTICLES) return;
        px[count]       = x;
        py[count]       = y;
        vx[count]       = vx_;
        vy[count]       = vy_;
        life[count]     = life_;
        life_max[count] = life_;
        scale[count]    = sc;
        initial_scale[count] = sc;
        rotation[count] = rot;
        color[count]    = col;
        atlas_id[count] = atlas;
        active[count]   = 1;
        ++count;
    }

    // Burst spawn — multi-particle emission from single point
    // Used for: tile clear, explosion, star burst
    void spawn_burst(float x, float y, uint16_t num,
                     float speed_min, float speed_max, float life_,
                     uint32_t col, uint8_t atlas = 0) noexcept {
        // Simple deterministic spread using golden angle
        float angle_inc = 2.399963229f;  // golden angle in radians
        float angle = 0.0f;
        for (uint16_t i = 0; i < num && count < MAX_PARTICLES; ++i) {
            float speed = speed_min + (speed_max - speed_min) * (static_cast<float>(i) / num);
            float vx_ = std::cos(angle) * speed;
            float vy_ = std::sin(angle) * speed;
            spawn(x, y, vx_, vy_, life_,
                  1.0f - 0.5f * (static_cast<float>(i) / num),  // scale fade
                  angle, col, atlas);
            angle += angle_inc;
        }
    }

    // Cone emitter — particles within angle cone
    void spawn_cone(float x, float y, uint16_t num,
                    float dir_angle, float spread, float speed, float life_,
                    uint32_t col, uint8_t atlas = 0) noexcept {
        float half = spread * 0.5f;
        for (uint16_t i = 0; i < num && count < MAX_PARTICLES; ++i) {
            float a = dir_angle - half + spread * (static_cast<float>(i) / num);
            float vx_ = std::cos(a) * speed;
            float vy_ = std::sin(a) * speed;
            spawn(x, y, vx_, vy_, life_,
                  1.0f, 0.0f, col, atlas);
        }
    }

    // Rain emitter — particles from top of screen
    void spawn_rain(float x, float y, float speed_y, float spread_x,
                    uint16_t num, float life_, uint32_t col) noexcept {
        for (uint16_t i = 0; i < num && count < MAX_PARTICLES; ++i) {
            float ox = (static_cast<float>(i) / num - 0.5f) * spread_x;
            spawn(x + ox, y, 0.0f, speed_y, life_,
                  0.5f + 0.5f * (static_cast<float>(i) / num),  // random-ish scale
                  0.0f, col);
        }
    }

    // Update all active particles — batch SIMD-friendly loop
    // Cache: sequential SoA access, predictable branch on active[i]
    void update(float dt, float gravity_x = 0.0f, float gravity_y = 200.0f) noexcept {
        for (uint16_t i = 0; i < count; ++i) {
            if (!active[i]) continue;

            life[i] -= dt;
            if (life[i] <= 0.0f) {
                deswap(i);
                --i;  // re-check this index after swap
                continue;
            }

            // Physics update
            vx[i] += gravity_x * dt;
            vy[i] += gravity_y * dt;
            px[i] += vx[i] * dt;
            py[i] += vy[i] * dt;

            // Life ratio for alpha/scale fade
            float life_ratio = life[i] / life_max[i];
            // Alpha is packed in color[31:24]
            uint32_t alpha = static_cast<uint32_t>((life_ratio * 255.0f));
            color[i] = (color[i] & 0x00FFFFFF) | (alpha << 24);
            // Shrink from initial to 20% over particle lifetime
            scale[i] = initial_scale[i] * (0.2f + 0.8f * life_ratio);
        }
    }

    // Despawn — swap-with-last (O(1), no shift)
    void deswap(uint16_t idx) noexcept {
        if (idx >= count) return;
        uint16_t last = count - 1;
        if (idx != last) {
            px[idx]       = px[last];
            py[idx]       = py[last];
            vx[idx]       = vx[last];
            vy[idx]       = vy[last];
            life[idx]     = life[last];
            life_max[idx] = life_max[last];
            scale[idx]    = scale[last];
            initial_scale[idx] = initial_scale[last];
            rotation[idx] = rotation[last];
            color[idx]    = color[last];
            atlas_id[idx] = atlas_id[last];
            active[idx]   = active[last];
        }
        --count;
    }
};

static_assert(sizeof(ParticlePool) <= 1024 * 1024, "ParticlePool < 1MB");

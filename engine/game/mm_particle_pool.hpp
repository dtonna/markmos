// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include "core/mm_types.h"
#include <cstddef>
#include <cstring>
#include <cmath>
#include <random>

// Particle System — GPU Instanced, SoA, Batch SIMD Update
// Cache reason:
//   - SoA layout: px[], py[], vx[], vy[], life[] — SIMD-friendly contiguous arrays
//   - Update loop = 5 sequential f32 arrays × MAX_PARTICLES → prefetch streams
//   - Single instanced draw call for all active particles
//   - No per-particle object overhead
// Design:
//   - MAX_PARTICLES = 4096 (32KB per f32 array × 8 = 256KB hot data)
//   - Swap-with-last despawn (O(1) remove)
//   - Spawn burst: writes sequentially into active range, no heap alloc
//   - GPU: single instance buffer upload + single draw call

static constexpr u16 MAX_PARTICLES = 4096;

struct alignas(64) ParticlePool {
    // Hot data — updated every frame, SIMD batches
    alignas(64) f32 px       [MAX_PARTICLES];
    alignas(64) f32 py       [MAX_PARTICLES];
    alignas(64) f32 vx       [MAX_PARTICLES];
    alignas(64) f32 vy       [MAX_PARTICLES];
    alignas(64) f32 life     [MAX_PARTICLES];
    alignas(64) f32 life_max [MAX_PARTICLES];
    alignas(64) f32 scale    [MAX_PARTICLES];
    alignas(64) f32 initial_scale[MAX_PARTICLES];
    alignas(64) f32 rotation [MAX_PARTICLES];
    alignas(64) u32 color [MAX_PARTICLES];   // RGBA packed
    alignas(64) u8  atlas_id [MAX_PARTICLES]; // sprite index in atlas
    alignas(64) u8  active   [MAX_PARTICLES];

    u16 count;

    ParticlePool() { reset(); }

    void reset() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
    }

    // Spawn a single particle — O(1), no alloc
    void spawn(f32 x, f32 y, f32 vx_, f32 vy_, f32 life_,
               f32 sc, f32 rot, u32 col, u8 atlas = 0) noexcept {
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
    void spawn_burst(f32 x, f32 y, u16 num,
                     f32 speed_min, f32 speed_max, f32 life_,
                     u32 col, u8 atlas = 0) noexcept {
        // Simple deterministic spread using golden angle
        f32 angle_inc = 2.399963229f;  // golden angle in radians
        f32 angle = 0.0f;
        for (u16 i = 0; i < num && count < MAX_PARTICLES; ++i) {
            f32 speed = speed_min + (speed_max - speed_min) * (static_cast<f32>(i) / num);
            f32 vx_ = __builtin_cosf(angle) * speed;
            f32 vy_ = __builtin_sinf(angle) * speed;
            spawn(x, y, vx_, vy_, life_,
                  1.0f - 0.5f * (static_cast<f32>(i) / num),  // scale fade
                  angle, col, atlas);
            angle += angle_inc;
        }
    }

    // Cone emitter — particles within angle cone
    void spawn_cone(f32 x, f32 y, u16 num,
                    f32 dir_angle, f32 spread, f32 speed, f32 life_,
                    u32 col, u8 atlas = 0) noexcept {
        f32 half = spread * 0.5f;
        for (u16 i = 0; i < num && count < MAX_PARTICLES; ++i) {
            f32 a = dir_angle - half + spread * (static_cast<f32>(i) / num);
            f32 vx_ = __builtin_cosf(a) * speed;
            f32 vy_ = __builtin_sinf(a) * speed;
            spawn(x, y, vx_, vy_, life_,
                  1.0f, 0.0f, col, atlas);
        }
    }

    // Rain emitter — particles from top of screen
    void spawn_rain(f32 x, f32 y, f32 speed_y, f32 spread_x,
                    u16 num, f32 life_, u32 col) noexcept {
        for (u16 i = 0; i < num && count < MAX_PARTICLES; ++i) {
            f32 ox = (static_cast<f32>(i) / num - 0.5f) * spread_x;
            spawn(x + ox, y, 0.0f, speed_y, life_,
                  0.5f + 0.5f * (static_cast<f32>(i) / num),  // random-ish scale
                  0.0f, col);
        }
    }

    // Update all active particles — batch SIMD-friendly loop
    // Cache: sequential SoA access, predictable branch on active[i]
    void update(f32 dt, f32 gravity_x = 0.0f, f32 gravity_y = 200.0f) noexcept {
        for (u16 i = 0; i < count; ++i) {
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
            f32 life_ratio = life[i] / life_max[i];
            // Alpha is packed in color[31:24]
            u32 alpha = static_cast<u32>((life_ratio * 255.0f));
            color[i] = (color[i] & 0x00FFFFFF) | (alpha << 24);
            // Shrink from initial to 20% over particle lifetime
            scale[i] = initial_scale[i] * (0.2f + 0.8f * life_ratio);
        }
    }

    // Despawn — swap-with-last (O(1), no shift)
    void deswap(u16 idx) noexcept {
        if (idx >= count) return;
        u16 last = count - 1;
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

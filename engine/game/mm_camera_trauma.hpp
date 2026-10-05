// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include "core/mm_types.h"
#include <cmath>
#include <algorithm>

// Camera Trauma — screen shake system
// Cache reason: single cache line struct, no heap, no virtual
// Design: trauma decays exponentially, offset computed from sin(time * freq)
// API: add_trauma(amount) — 0.3 small, 0.6 medium, 1.0 max
// Haptic integration: external callback for iOS CoreHaptics / Android VibrationEffect

struct CameraTrauma {
    f32 trauma;        // 0..1, decays per frame
    f32 decay;         // multiplier per frame (0.9 = fast, 0.95 = slow)
    f32 max_offset_x;  // max horizontal offset in pixels
    f32 max_offset_y;  // max vertical offset in pixels
    f32 max_angle;     // max rotation in radians
    f32 frequency;     // shake frequency

    CameraTrauma() noexcept
        : trauma(0.0f), decay(0.9f)
        , max_offset_x(12.0f), max_offset_y(12.0f)
        , max_angle(0.05f), frequency(10.0f) {}

    void add_trauma(f32 amount) noexcept {
        trauma = std::min(1.0f, trauma + amount);
    }

    void update(f32 dt) noexcept {
        trauma *= __builtin_powf(decay, dt * 60.0f);  // frame-independent decay
        if (trauma < 0.001f) trauma = 0.0f;
    }

    // Get current shake offset — call per frame before rendering
    void get_offset(f32 time, f32& out_x, f32& out_y, f32& out_angle) const noexcept {
        if (trauma <= 0.0f) {
            out_x = out_y = 0.0f;
            out_angle = 0.0f;
            return;
        }

        f32 shake = trauma * trauma;  // quadratic: subtle shake at low trauma
        out_x = max_offset_x * shake * __builtin_sinf(time * frequency);
        out_y = max_offset_y * shake * __builtin_cosf(time * frequency * 1.3f);  // different freq for y
        out_angle = max_angle * shake * __builtin_sinf(time * frequency * 0.7f);
    }

    bool is_shaking() const noexcept { return trauma > 0.001f; }
};

// Convenience constants
inline constexpr f32 SHAKE_SMALL  = 0.3f;
inline constexpr f32 SHAKE_MEDIUM = 0.6f;
inline constexpr f32 SHAKE_LARGE  = 1.0f;

#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

// Camera Trauma — screen shake system
// Cache reason: single cache line struct, no heap, no virtual
// Design: trauma decays exponentially, offset computed from sin(time * freq)
// API: add_trauma(amount) — 0.3 small, 0.6 medium, 1.0 max
// Haptic integration: external callback for iOS CoreHaptics / Android VibrationEffect

struct CameraTrauma {
    float trauma;        // 0..1, decays per frame
    float decay;         // multiplier per frame (0.9 = fast, 0.95 = slow)
    float max_offset_x;  // max horizontal offset in pixels
    float max_offset_y;  // max vertical offset in pixels
    float max_angle;     // max rotation in radians
    float frequency;     // shake frequency

    CameraTrauma() noexcept
        : trauma(0.0f), decay(0.9f)
        , max_offset_x(12.0f), max_offset_y(12.0f)
        , max_angle(0.05f), frequency(10.0f) {}

    void add_trauma(float amount) noexcept {
        trauma = std::min(1.0f, trauma + amount);
    }

    void update(float dt) noexcept {
        trauma *= std::pow(decay, dt * 60.0f);  // frame-independent decay
        if (trauma < 0.001f) trauma = 0.0f;
    }

    // Get current shake offset — call per frame before rendering
    void get_offset(float time, float& out_x, float& out_y, float& out_angle) const noexcept {
        if (trauma <= 0.0f) {
            out_x = out_y = 0.0f;
            out_angle = 0.0f;
            return;
        }

        float shake = trauma * trauma;  // quadratic: subtle shake at low trauma
        out_x = max_offset_x * shake * std::sin(time * frequency);
        out_y = max_offset_y * shake * std::cos(time * frequency * 1.3f);  // different freq for y
        out_angle = max_angle * shake * std::sin(time * frequency * 0.7f);
    }

    bool is_shaking() const noexcept { return trauma > 0.001f; }
};

// Convenience constants
inline constexpr float SHAKE_SMALL  = 0.3f;
inline constexpr float SHAKE_MEDIUM = 0.6f;
inline constexpr float SHAKE_LARGE  = 1.0f;

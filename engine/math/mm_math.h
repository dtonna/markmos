// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include <cmath>

namespace mm_math {

constexpr f32 MM_PI       = 3.14159265358979323846F;
constexpr f32 MM_TWO_PI   = 6.28318530717958647693F;
constexpr f32 MM_HALF_PI  = 1.57079632679489661923F;
constexpr f32 MM_DEG_TO_RAD = MM_PI / 180.0F;
constexpr f32 MM_RAD_TO_DEG = 180.0F / MM_PI;
constexpr f32 MM_E        = 2.71828182845904523536F;
constexpr f32 MM_SQRT2    = 1.41421356237309504880F;
constexpr f32 MM_EPSILON  = 1e-6F;

// Scalar helpers that were previously re-typed inline at every animation/UI site.
// Keeping them here means one definition, tested once, instead of a hand-rolled
// `a + (b - a) * t` / `fmaxf(lo, fminf(hi, v)) / 1-expf(-k*dt)` at each call.
MM_FORCE_INLINE constexpr f32 lerp(f32 a, f32 b, f32 t) noexcept { return a + (b - a) * t; }

// Clamp v into [lo, hi]. Note this is intentionally the same as
// v < lo ? lo : (v > hi ? hi : v) so a NaN input passes through unchanged,
// matching min/max-style clamp implementations.
MM_FORCE_INLINE constexpr f32 clamp(f32 v, f32 lo, f32 hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// 1 - exp(-rate*dt), the per-frame factor for exponential-smoothing chases:
//     v += (target - v) * exp_damp(rate, dt);
// The same formula apps repeatedly spelt out as `1.0f - expf(-k*dt)`.
// rate is the responsiveness: larger `rate` converges faster. Fades to 1 as
// rate*dt grows, so a big frame never overshoots past the target.
MM_FORCE_INLINE f32 exp_damp(f32 rate, f32 dt) noexcept { return 1.0f - std::expf(-rate * dt); }

} // namespace mm_math

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "core/mm_types.h"

namespace mm_math {

struct vec4 {
    union {
        float4 v;
        struct alignas(16) {
            f32 x, y, z, w;
        };
        f32 data[4];
    };

    MM_FORCE_INLINE constexpr vec4() : v{0.0F, 0.0F, 0.0F, 0.0F} {}
    MM_FORCE_INLINE constexpr vec4(f32 x_, f32 y_, f32 z_, f32 w_) : v{x_, y_, z_, w_} {}
    MM_FORCE_INLINE explicit constexpr vec4(float4 v_) : v(v_) {}
    MM_FORCE_INLINE explicit constexpr vec4(f32 s_) : v{s_, s_, s_, s_} {}

    MM_FORCE_INLINE constexpr vec4 &operator=(const vec4 &other) {
        v = other.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr vec4 operator+(const vec4 &r) const { return vec4(v + r.v); }
    MM_FORCE_INLINE constexpr vec4 operator-(const vec4 &r) const { return vec4(v - r.v); }
    MM_FORCE_INLINE constexpr vec4 operator*(const f32 s) const { return vec4(x * s, y * s, z * s, w * s); }
    MM_FORCE_INLINE constexpr vec4 operator/(const f32 s) const {
        f32 inv_s = 1.0F / s;
        return (*this) * inv_s;
    }
    MM_FORCE_INLINE constexpr vec4        operator/(const vec4 &r) const { return vec4(v / r.v); }
    MM_FORCE_INLINE constexpr vec4        operator*(const vec4 &r) const { return vec4(v * r.v); }
    MM_FORCE_INLINE constexpr friend vec4 operator*(const f32 s, const vec4 &r) { return r * s; }
    MM_FORCE_INLINE constexpr friend vec4 operator/(const f32 s, const vec4 &r) { return vec4(s / r.x, s / r.y, s / r.z, s / r.w); }
    MM_FORCE_INLINE constexpr vec4        operator-() const { return vec4(-v); }
    MM_FORCE_INLINE constexpr vec4       &operator+=(const vec4 &r) {
        v += r.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr vec4 &operator-=(const vec4 &r) {
        v -= r.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr vec4 &operator*=(const vec4 &r) {
        v *= r.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr vec4 &operator*=(const f32 s) {
        v *= float4{s, s, s, s};
        return *this;
    }
    MM_FORCE_INLINE constexpr vec4 &operator/=(const f32 s) {
        f32 inv_s       = 1.0F / s;
        return (*this) *= inv_s;
    }
    MM_FORCE_INLINE constexpr f32  operator[](int index) const { return data[index]; }
    MM_FORCE_INLINE constexpr f32 &operator[](int index) { return data[index]; }

    MM_FORCE_INLINE bool           operator==(const vec4 &other) const noexcept { return nearly_equal(other); }
    MM_FORCE_INLINE bool           operator!=(const vec4 &other) const noexcept { return !nearly_equal(other); }

    // =============================================================================
    // Math functions (intrinsics cannot be constexpr)
    // =============================================================================
    MM_FORCE_INLINE f32            dot(const vec4 &r) const noexcept {
        float4 mul = v * r.v;
        return mul.x + mul.y + mul.z + mul.w;
    }
    MM_FORCE_INLINE f32  length_squared() const noexcept { return dot(*this); }
    MM_FORCE_INLINE f32  length() const { return __builtin_sqrtf(length_squared()); }
    MM_FORCE_INLINE vec4 normalized() const {
        f32 len = length();
        if (len > 1e-6F) {
            return (*this) / len;
        }
        return vec4(0.0F);
    }

    MM_FORCE_INLINE vec4 cross(const vec4 &r) const noexcept { return vec4(y * r.z - z * r.y, z * r.x - x * r.z, x * r.y - y * r.x, 0.0F); }
    MM_FORCE_INLINE vec4 abs() const noexcept { return vec4(__builtin_elementwise_abs(v)); }
    MM_FORCE_INLINE vec4 floor() const noexcept { return vec4(__builtin_elementwise_floor(v)); }
    MM_FORCE_INLINE vec4 ceil() const noexcept { return vec4(__builtin_elementwise_ceil(v)); }
    MM_FORCE_INLINE vec4 min(const vec4 &other) const noexcept { return vec4(__builtin_elementwise_min(v, other.v)); }
    MM_FORCE_INLINE vec4 max(const vec4 &other) const noexcept { return vec4(__builtin_elementwise_max(v, other.v)); }
    MM_FORCE_INLINE vec4 clamp(const vec4 &minv, const vec4 &maxv) const noexcept {
        return vec4(__builtin_elementwise_min(__builtin_elementwise_max(v, minv.v), maxv.v));
    }

    // =============================================================================
    // utility functions
    // =============================================================================
    MM_FORCE_INLINE bool nearly_equal(const vec4 &other, f32 eps = 1e-6F) const noexcept {
        f32 dist2 = (*this - other).length_squared();
        return dist2 < eps * eps;
    }

    MM_FORCE_INLINE constexpr vec4 lerp(const vec4 &b, f32 t) const noexcept { return *this + (b - *this) * t; }
    MM_FORCE_INLINE f32            distance(const vec4 &other) const noexcept { return (*this - other).length(); }
    MM_FORCE_INLINE vec4           reflect(const vec4 &normal) const noexcept {
        f32 dot_product = this->dot(normal);
        return *this - 2.0F * dot_product * normal;
    }
    MM_FORCE_INLINE vec4 reflect_normalized(const vec4 &normal_unit) const noexcept {
        f32 dp = dot(normal_unit);
        return *this - 2.0F * dp * normal_unit;
    }
    MM_FORCE_INLINE vec4 project_onto(const vec4 &other) const noexcept {
        f32 dot_product = this->dot(other);
        f32 other_len2  = other.length_squared();
        if (other_len2 < 1e-12F) {
            return vec4(0.0F);
        }
        return (dot_product / other_len2) * other;
    }
    MM_FORCE_INLINE vec4 project_onto_normalized(const vec4 &n) const noexcept { return dot(n) * n; }
    MM_FORCE_INLINE vec4 reject(const vec4 &onto) const noexcept { return *this - project_onto(onto); }
    MM_FORCE_INLINE f32  angle_between(const vec4 &other) const noexcept {
        f32 len2  = length_squared();
        f32 olen2 = other.length_squared();
        f32 denom = __builtin_sqrtf(len2 * olen2);
        if (denom < 1e-12F) {
            return 0.0F;
        }
        f32 cos_angle = dot(other) / denom;
        cos_angle     = MM_CLAMP(cos_angle, -1.0F, 1.0F);
        return __builtin_acosf(cos_angle);
    }
    MM_FORCE_INLINE vec4 rotate_around(const vec4 &center, f32 angle_rad) const noexcept {
        vec4 translated = *this - center;
        f32  cos_a      = __builtin_cosf(angle_rad);
        f32  sin_a      = __builtin_sinf(angle_rad);
        vec4 rotated    = vec4(translated.x * cos_a - translated.y * sin_a, translated.x * sin_a + translated.y * cos_a, translated.z, translated.w);
        return rotated + center;
    }
    MM_FORCE_INLINE constexpr vec4 rotate90() const noexcept { return vec4(-y, x, z, w); }
    MM_FORCE_INLINE constexpr vec4 rotate270() const noexcept { return vec4(y, -x, z, w); }

    static const vec4              ZERO;
    static const vec4              ONE;
    static const vec4              UNIT_X;
    static const vec4              UNIT_Y;
    static const vec4              UNIT_Z;
    static const vec4              UNIT_W;
};

inline const mm_math::vec4 mm_math::vec4::ZERO   = mm_math::vec4(0.0F, 0.0F, 0.0F, 0.0F);
inline const mm_math::vec4 mm_math::vec4::ONE    = mm_math::vec4(1.0F, 1.0F, 1.0F, 1.0F);
inline const mm_math::vec4 mm_math::vec4::UNIT_X = mm_math::vec4(1.0F, 0.0F, 0.0F, 0.0F);
inline const mm_math::vec4 mm_math::vec4::UNIT_Y = mm_math::vec4(0.0F, 1.0F, 0.0F, 0.0F);
inline const mm_math::vec4 mm_math::vec4::UNIT_Z = mm_math::vec4(0.0F, 0.0F, 1.0F, 0.0F);
inline const mm_math::vec4 mm_math::vec4::UNIT_W = mm_math::vec4(0.0F, 0.0F, 0.0F, 1.0F);

MM_FORCE_INLINE vec4       lerp(const vec4 &a, const vec4 &b, f32 t) noexcept {
    auto s = vec4{t, t, t, t};
    return a + (b - a) * s;
}
MM_FORCE_INLINE vec4 clamp(const vec4 &x, const vec4 &minv, const vec4 &maxv) {
    return vec4(__builtin_elementwise_min(__builtin_elementwise_max(x.v, minv.v), maxv.v));
}
MM_FORCE_INLINE vec4 smoothstep2(const vec4 &x) {
    auto t = clamp(x, vec4{0, 0, 0, 0}, vec4{1, 1, 1, 1});
    return t * t * (vec4{3, 3, 3, 3} - vec4{2, 2, 2, 2} * t);
}
MM_FORCE_INLINE vec4 smoothstep2(const vec4 &edge0, const vec4 &edge1, const vec4 &x) {
    auto t = clamp((x - edge0) / (edge1 - edge0), vec4{0, 0, 0, 0}, vec4{1, 1, 1, 1});
    return t * t * (vec4{3, 3, 3, 3} - vec4{2, 2, 2, 2} * t);
}

} // namespace mm_math

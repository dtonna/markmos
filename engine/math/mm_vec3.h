// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "core/mm_types.h"

namespace mm_math {
struct vec3 {
    union {
        float3 v;
        struct alignas(16) {
            f32 x, y, z, w;
        };
        f32 data[4];
    };

    MM_FORCE_INLINE constexpr vec3() : v{0.0F, 0.0F, 0.0F, 0.0F} {}
    MM_FORCE_INLINE constexpr vec3(f32 x_, f32 y_, f32 z_) : v{x_, y_, z_, 0.0F} {}
    MM_FORCE_INLINE explicit constexpr vec3(float3 v_) : v(v_) {}
    MM_FORCE_INLINE explicit constexpr vec3(f32 s_) : v{s_, s_, s_, 0.0F} {}

    MM_FORCE_INLINE constexpr vec3 &operator=(const vec3 &other) {
        v = other.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr vec3        operator+(const vec3 &r) const { return vec3(v + r.v); }
    MM_FORCE_INLINE constexpr vec3        operator-(const vec3 &r) const { return vec3(v - r.v); }
    MM_FORCE_INLINE constexpr friend vec3 operator-(const f32 s, const vec3 &r) { return vec3(s - r.x, s - r.y, s - r.z); }
    MM_FORCE_INLINE constexpr vec3        operator*(const f32 s) const { return vec3(x * s, y * s, z * s); }
    MM_FORCE_INLINE constexpr vec3        operator/(const f32 s) const {
        f32 inv_s = 1.0F / s;
        return (*this) * inv_s;
    }
    MM_FORCE_INLINE constexpr vec3        operator/(const vec3 &r) const { return vec3(v / r.v); }
    MM_FORCE_INLINE constexpr vec3        operator*(const vec3 &r) const { return vec3(v * r.v); }
    MM_FORCE_INLINE constexpr friend vec3 operator*(const f32 s, const vec3 &r) { return r * s; }
    MM_FORCE_INLINE constexpr vec3        operator-() const { return vec3(-v); }
    MM_FORCE_INLINE constexpr vec3       &operator-=(const vec3 &r) {
        v -= r.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr vec3 &operator+=(const vec3 &r) {
        v += r.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr vec3 &operator*=(const vec3 &r) {
        v *= r.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr vec3 &operator*=(const f32 s) {
        v *= float3{s, s, s, 0.0F};
        return *this;
    }
    MM_FORCE_INLINE constexpr vec3 &operator/=(const f32 s) {
        f32 inv_s       = 1.0F / s;
        return (*this) *= inv_s;
    }
    MM_FORCE_INLINE constexpr f32  operator[](int i) const { return data[i]; }
    MM_FORCE_INLINE constexpr f32 &operator[](int i) { return data[i]; }

    MM_FORCE_INLINE bool           operator==(const vec3 &other) const noexcept { return nearly_equal(other); }
    MM_FORCE_INLINE bool           operator!=(const vec3 &other) const noexcept { return !nearly_equal(other); }

    // =============================================================================
    // Math functions (intrinsics cannot be constexpr)
    // =============================================================================
    MM_FORCE_INLINE f32            dot(const vec3 &r) const noexcept {
        float3 mul = v * r.v;
        return mul.x + mul.y + mul.z;
    }
    MM_FORCE_INLINE f32  length_squared() const noexcept { return dot(*this); }
    MM_FORCE_INLINE f32  length() const { return __builtin_sqrtf(length_squared()); }
    MM_FORCE_INLINE vec3 normalized() const {
        f32 len = length();
        if (len > 1e-6F) {
            return (*this) / len;
        }
        return vec3(0.0F);
    }
    MM_FORCE_INLINE vec3 cross(const vec3 &r) const { return vec3(y * r.z - z * r.y, z * r.x - x * r.z, x * r.y - y * r.x); }
    MM_FORCE_INLINE vec3 abs() const { return vec3(__builtin_elementwise_abs(v)); }
    MM_FORCE_INLINE vec3 floor() const { return vec3(__builtin_elementwise_floor(v)); }
    MM_FORCE_INLINE vec3 ceil() const { return vec3(__builtin_elementwise_ceil(v)); }
    MM_FORCE_INLINE vec3 min(const vec3 &other) const { return vec3(__builtin_elementwise_min(v, other.v)); }
    MM_FORCE_INLINE vec3 max(const vec3 &other) const { return vec3(__builtin_elementwise_max(v, other.v)); }
    MM_FORCE_INLINE vec3 clamp(const vec3 &minv, const vec3 &maxv) const {
        return vec3(__builtin_elementwise_min(__builtin_elementwise_max(v, minv.v), maxv.v));
    }

    // =============================================================================
    // utility functions
    // =============================================================================
    MM_FORCE_INLINE bool nearly_equal(const vec3 &other, f32 eps = 1e-6F) const noexcept {
        f32 dist2 = (*this - other).length_squared();
        return dist2 < eps * eps;
    }

    MM_FORCE_INLINE constexpr vec3 lerp(const vec3 &b, f32 t) const noexcept { return *this + (b - *this) * t; }
    MM_FORCE_INLINE f32            distance(const vec3 &other) const noexcept { return (*this - other).length(); }
    MM_FORCE_INLINE vec3           reflect(const vec3 &normal) const noexcept {
        f32 dot_product = this->dot(normal);
        return *this - 2.0F * dot_product * normal;
    }
    MM_FORCE_INLINE vec3 reflect_normalized(const vec3 &normal_unit) const noexcept {
        f32 dp = dot(normal_unit);
        return *this - 2.0F * dp * normal_unit;
    }
    MM_FORCE_INLINE vec3 project_onto(const vec3 &other) const noexcept {
        f32 dot_product = this->dot(other);
        f32 other_len2  = other.length_squared();
        if (other_len2 < 1e-12F) {
            return vec3(0.0F);
        }
        return (dot_product / other_len2) * other;
    }
    MM_FORCE_INLINE vec3 project_onto_normalized(const vec3 &n) const noexcept { return dot(n) * n; }
    MM_FORCE_INLINE vec3 reject(const vec3 &onto) const noexcept { return *this - project_onto(onto); }
    MM_FORCE_INLINE f32  angle_between(const vec3 &other) const noexcept {
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

    static const vec3 ZERO;
    static const vec3 ONE;
    static const vec3 UNIT_X;
    static const vec3 UNIT_Y;
    static const vec3 UNIT_Z;
};

inline const mm_math::vec3 mm_math::vec3::ZERO   = mm_math::vec3(0.0F, 0.0F, 0.0F);
inline const mm_math::vec3 mm_math::vec3::ONE    = mm_math::vec3(1.0F, 1.0F, 1.0F);
inline const mm_math::vec3 mm_math::vec3::UNIT_X = mm_math::vec3(1.0F, 0.0F, 0.0F);
inline const mm_math::vec3 mm_math::vec3::UNIT_Y = mm_math::vec3(0.0F, 1.0F, 0.0F);
inline const mm_math::vec3 mm_math::vec3::UNIT_Z = mm_math::vec3(0.0F, 0.0F, 1.0F);

MM_FORCE_INLINE vec3       lerp(const vec3 &a, const vec3 &b, f32 t) {
    auto s = vec3{t, t, t};
    return a + (b - a) * s;
}

MM_FORCE_INLINE vec3 clamp(const vec3 &x, const vec3 &minv, const vec3 &maxv) {
    return vec3(__builtin_elementwise_min(__builtin_elementwise_max(x.v, minv.v), maxv.v));
}

MM_FORCE_INLINE vec3 smoothstep2(const vec3 &x) {
    auto t = clamp(x, vec3{0, 0, 0}, vec3{1, 1, 1});
    return t * t * (vec3{3, 3, 3} - vec3{2, 2, 2} * t);
}

MM_FORCE_INLINE vec3 smoothstep2(const vec3 &edge0, const vec3 &edge1, const vec3 &x) {
    auto t = clamp((x - edge0) / (edge1 - edge0), vec3{0, 0, 0}, vec3{1, 1, 1});
    return t * t * (vec3{3, 3, 3} - vec3{2, 2, 2} * t);
}

} // namespace mm_math

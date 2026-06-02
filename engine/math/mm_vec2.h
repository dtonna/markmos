// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include <math.h>

namespace mm_math {
// =============================================================================
// Vec2 struct
// =============================================================================
struct vec2 {
    // =============================================================================
    // Data members
    // =============================================================================
    union {
        float2 v;
        struct alignas(8) {
            f32 x, y;
        };
        f32 data[2];
    };

    // =============================================================================
    // Constructors
    // =============================================================================
    MM_FORCE_INLINE constexpr vec2() : v{0.0F, 0.0F} {}
    MM_FORCE_INLINE constexpr vec2(f32 x_, f32 y_) : v{x_, y_} {}
    MM_FORCE_INLINE constexpr vec2(const vec2 &) noexcept = default;
    MM_FORCE_INLINE constexpr vec2(vec2 &&) noexcept      = default;
    MM_FORCE_INLINE explicit constexpr vec2(float2 v_) : v(v_) {}
    MM_FORCE_INLINE explicit constexpr vec2(f32 s_) : v{s_, s_} {}

    // =============================================================================
    // Operator overloads
    // =============================================================================
    MM_FORCE_INLINE constexpr vec2 &operator=(const vec2 &other) noexcept {
        v = other.v;
        return *this;
    }

    MM_FORCE_INLINE constexpr bool operator==(const vec2 &other) const noexcept { return nearly_equal(other); }
    MM_FORCE_INLINE constexpr bool operator!=(const vec2 &other) const noexcept { return !nearly_equal(other); }

    MM_FORCE_INLINE constexpr vec2 operator+(const vec2 &r) const noexcept { return vec2(v + r.v); }

    MM_FORCE_INLINE constexpr vec2 operator-(const vec2 &r) const noexcept { return vec2(v - r.v); }

    MM_FORCE_INLINE constexpr vec2 operator*(const vec2 &r) const noexcept { return vec2(v * r.v); }

    MM_FORCE_INLINE constexpr vec2 operator*(const f32 s) const noexcept { return vec2(x * s, y * s); }

    MM_FORCE_INLINE constexpr vec2 operator/(const f32 s) const noexcept {
        MM_ASSERT(s != 0.0F && "vec2: division by zero");
        return (*this) * (1.0F / s);
    }
    MM_FORCE_INLINE constexpr vec2  operator/(const vec2 &r) const noexcept { return vec2(v / r.v); }

    // =============================================================================
    // Compound assignment operators
    // =============================================================================
    MM_FORCE_INLINE constexpr vec2 &operator+=(const vec2 &r) noexcept {
        v += r.v;
        return *this;
    }

    MM_FORCE_INLINE constexpr vec2 &operator-=(const vec2 &r) noexcept {
        v -= r.v;
        return *this;
    }

    MM_FORCE_INLINE constexpr vec2 &operator*=(const vec2 &r) noexcept {
        v *= r.v;
        return *this;
    }

    MM_FORCE_INLINE constexpr vec2 &operator*=(const f32 s) noexcept {
        v *= float2{s, s};
        return *this;
    }

    MM_FORCE_INLINE constexpr vec2 &operator/=(const f32 s) noexcept {
        f32 inv_s       = 1.0F / s;
        return (*this) *= inv_s;
    }
    MM_FORCE_INLINE constexpr friend vec2 operator*(const f32 s, const vec2 &r) noexcept { return r * s; }
    MM_FORCE_INLINE constexpr friend vec2 operator/(const f32 s, const vec2 &r) noexcept { return vec2(s / r.x, s / r.y); }

    MM_FORCE_INLINE constexpr f32         operator[](int i) const noexcept { return data[i]; }
    MM_FORCE_INLINE constexpr f32        &operator[](int i) noexcept { return data[i]; }
    MM_FORCE_INLINE constexpr vec2        operator-() const noexcept { return vec2(-v); }

    // =============================================================================
    // Math functions
    // =============================================================================
    // Note: functions using intrinsics (sqrt, trig, etc.) are NOT constexpr
    MM_FORCE_INLINE f32                   dot(const vec2 &r) const noexcept {
        float2 mul = v * r.v;
        return mul.x + mul.y;
    }

    MM_FORCE_INLINE constexpr f32 length_squared() const noexcept { return dot(*this); }

    MM_FORCE_INLINE f32           length() const noexcept { return __builtin_sqrtf(length_squared()); }

    MM_FORCE_INLINE vec2          normalized() const noexcept {
        f32 len = length();
        if (len > 1e-6F) {
            return (*this) / len;
        }
        return vec2(0.0F);
    }

    MM_FORCE_INLINE bool nearly_equal(const vec2 &other, f32 eps = 1e-6F) const noexcept {
        f32 dist2 = (*this - other).length_squared();
        return dist2 < eps * eps;
    }

    MM_FORCE_INLINE f32            cross(const vec2 &r) const noexcept { return x * r.y - y * r.x; }
    MM_FORCE_INLINE vec2           abs() const noexcept { return vec2(__builtin_elementwise_abs(v)); }
    MM_FORCE_INLINE vec2           floor() const noexcept { return vec2(__builtin_elementwise_floor(v)); }
    MM_FORCE_INLINE vec2           ceil() const noexcept { return vec2(__builtin_elementwise_ceil(v)); }
    MM_FORCE_INLINE vec2           min(const vec2 &other) const noexcept { return vec2(__builtin_elementwise_min(v, other.v)); }
    MM_FORCE_INLINE vec2           max(const vec2 &other) const noexcept { return vec2(__builtin_elementwise_max(v, other.v)); }

    // =============================================================================
    // utility functions
    // =============================================================================
    MM_FORCE_INLINE constexpr vec2 lerp(const vec2 &b, f32 t) const noexcept { return *this + (b - *this) * t; }
    MM_FORCE_INLINE f32            distance(const vec2 &other) const noexcept { return (*this - other).length(); }
    MM_FORCE_INLINE constexpr vec2 perpendicular() const noexcept { return vec2(-y, x); }
    MM_FORCE_INLINE vec2           reflect(const vec2 &normal) const noexcept {
        f32 dot_product = this->dot(normal);
        return *this - 2.0F * dot_product * normal;
    }
    MM_FORCE_INLINE vec2 reflect_normalized(const vec2 &normal_unit) const noexcept {
        f32 dp = dot(normal_unit);
        return *this - 2.0F * dp * normal_unit;
    }
    MM_FORCE_INLINE vec2 project_onto(const vec2 &other) const noexcept {
        f32 dot_product = this->dot(other);
        f32 other_len2  = other.length_squared();
        if (other_len2 < 1e-12F) {
            return vec2(0.0F); // Avoid division by zero
        }
        return (dot_product / other_len2) * other;
    }
    MM_FORCE_INLINE vec2 project_onto_normalized(const vec2 &n) const noexcept { return dot(n) * n; }
    MM_FORCE_INLINE vec2 reject(const vec2 &onto) const noexcept { return *this - project_onto(onto); }

    MM_FORCE_INLINE f32  angle_between(const vec2 &other) const noexcept {
        f32 len2  = length_squared();
        f32 olen2 = other.length_squared();
        f32 denom = __builtin_sqrtf(len2 * olen2);
        if (denom < 1e-12F) {
            return 0.0F; // undefined, return 0
        }
        f32 cos_angle = dot(other) / denom;
        cos_angle     = MM_CLAMP(cos_angle, -1.0F, 1.0F);
        return __builtin_acosf(cos_angle);
    }
    MM_FORCE_INLINE vec2 rotate(f32 angle_rad) const noexcept {
        f32 cos_a = __builtin_cosf(angle_rad);
        f32 sin_a = __builtin_sinf(angle_rad);
        return vec2(x * cos_a - y * sin_a, x * sin_a + y * cos_a);
    }
    MM_FORCE_INLINE vec2 rotate_around(const vec2 &center, f32 angle_rad) const noexcept { return (*this - center).rotate(angle_rad) + center; }
    MM_FORCE_INLINE vec2 rotate90() const noexcept { return vec2(-y, x); }
    MM_FORCE_INLINE vec2 rotate270() const noexcept { return vec2(y, -x); }

    static const vec2    ZERO;
    static const vec2    ONE;
    static const vec2    UNIT_X;
    static const vec2    UNIT_Y;
};

inline const mm_math::vec2 mm_math::vec2::ZERO   = mm_math::vec2(0.0F, 0.0F);
inline const mm_math::vec2 mm_math::vec2::ONE    = mm_math::vec2(1.0F, 1.0F);
inline const mm_math::vec2 mm_math::vec2::UNIT_X = mm_math::vec2(1.0F, 0.0F);
inline const mm_math::vec2 mm_math::vec2::UNIT_Y = mm_math::vec2(0.0F, 1.0F);

MM_FORCE_INLINE vec2       lerp(const vec2 &a, const vec2 &b, f32 t) {
    auto s = vec2{t, t};
    return a + (b - a) * s;
}

MM_FORCE_INLINE vec2 clamp(const vec2 &x, const vec2 &minv, const vec2 &maxv) {
    return vec2(__builtin_elementwise_min(__builtin_elementwise_max(x.v, minv.v), maxv.v));
}

MM_FORCE_INLINE vec2 smoothstep2(const vec2 &x) {
    auto t = clamp(x, vec2{0, 0}, vec2{1, 1});
    return t * t * (vec2{3, 3} - vec2{2, 2} * t);
}

MM_FORCE_INLINE vec2 smoothstep2(const vec2 &edge0, const vec2 &edge1, const vec2 &x) {
    auto t = clamp((x - edge0) / (edge1 - edge0), vec2{0, 0}, vec2{1, 1});
    return t * t * (vec2{3, 3} - vec2{2, 2} * t);
}

struct aabb2 {
    vec2                 min;
    vec2                 max;

    MM_FORCE_INLINE vec2 center() const { return (min + max) * 0.5F; }
    MM_FORCE_INLINE vec2 size() const { return max - min; }
    MM_FORCE_INLINE vec2 half_size() const { return size() * 0.5F; }
    MM_FORCE_INLINE bool contains(const vec2 &point) const { return (point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y); }
};

MM_FORCE_INLINE aabb2 aabb2_from_center_size(const vec2 &center, const vec2 &size) {
    vec2 half_size = size * 0.5F;
    return aabb2{center - half_size, center + half_size};
}

MM_FORCE_INLINE bool intersects(const aabb2 &a, const aabb2 &b) {
    vec2 a_min = a.min;
    vec2 a_max = a.max;

    vec2 b_min = b.min;
    vec2 b_max = b.max;

    // SIMD compare → returns int2 mask (0 or -1)
    int2 sep   = (a_max.v < b_min.v) | (b_max.v < a_min.v);

    // If either axis is separated → intersection = false
    return !(sep.x || sep.y);
}

} // namespace mm_math

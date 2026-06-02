// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include "mm_vec2.h"
#include "mm_vec3.h"

namespace mm_math {
// =============================================================================
// mat3 struct
// =============================================================================
struct mat3 {
    float3 rows[3]; // Row-major order for better cache performance
    // Constructors
    MM_FORCE_INLINE constexpr mat3() noexcept : rows{float3{1, 0, 0}, float3{0, 1, 0}, float3{0, 0, 1}} {}
    MM_FORCE_INLINE constexpr mat3(f32 diagonal) noexcept : rows{float3{diagonal, 0, 0}, float3{0, diagonal, 0}, float3{0, 0, diagonal}} {}
    MM_FORCE_INLINE constexpr mat3(float3 r0, float3 r1, float3 r2) noexcept : rows{r0, r1, r2} {}
    MM_FORCE_INLINE static constexpr mat3 identity() noexcept { return mat3(1.0F); }

    MM_FORCE_INLINE constexpr mat3        operator+(const mat3 &other) const noexcept {
        return mat3(rows[0] + other.rows[0], rows[1] + other.rows[1], rows[2] + other.rows[2]);
    }

    MM_FORCE_INLINE constexpr mat3 operator-(const mat3 &other) const noexcept {
        return mat3(rows[0] - other.rows[0], rows[1] - other.rows[1], rows[2] - other.rows[2]);
    }

    MM_FORCE_INLINE mat3 operator*(const mat3 &other) const noexcept {
        mat3 result;
        for (int i = 0; i < 3; ++i) {
            auto r         = rows[i];
            result.rows[i] = float3{r[0] * other.rows[0][0] + r[1] * other.rows[1][0] + r[2] * other.rows[2][0],
                                    r[0] * other.rows[0][1] + r[1] * other.rows[1][1] + r[2] * other.rows[2][1],
                                    r[0] * other.rows[0][2] + r[1] * other.rows[1][2] + r[2] * other.rows[2][2]};
        }
        return result;
    }

    MM_FORCE_INLINE vec3 operator*(const vec3 &vec) const noexcept {
        return vec3{rows[0][0] * vec[0] + rows[0][1] * vec[1] + rows[0][2] * vec[2], rows[1][0] * vec[0] + rows[1][1] * vec[1] + rows[1][2] * vec[2],
                    rows[2][0] * vec[0] + rows[2][1] * vec[1] + rows[2][2] * vec[2]};
    }

    MM_FORCE_INLINE constexpr mat3        operator*(f32 scalar) const noexcept { return mat3(rows[0] * scalar, rows[1] * scalar, rows[2] * scalar); }

    MM_FORCE_INLINE constexpr friend mat3 operator*(f32 scalar, const mat3 &m) noexcept { return m * scalar; }

    // =============================================================================
    // Transpose
    // =============================================================================
    MM_FORCE_INLINE mat3                  transpose() const noexcept {
        return mat3(float3{rows[0][0], rows[1][0], rows[2][0]}, float3{rows[0][1], rows[1][1], rows[2][1]}, float3{rows[0][2], rows[1][2], rows[2][2]});
    }

    // =============================================================================
    // Translation, rotation, scaling
    // =============================================================================
    MM_FORCE_INLINE static constexpr mat3 translation(f32 tx, f32 ty) noexcept { return mat3(float3{1, 0, 0}, float3{0, 1, 0}, float3{tx, ty, 1}); }
    MM_FORCE_INLINE static constexpr mat3 translation(const vec2 &t) noexcept { return translation(t.x, t.y); }
    MM_FORCE_INLINE static mat3           rotation(f32 angle_rad) noexcept {
        f32 c = __builtin_cosf(angle_rad);
        f32 s = __builtin_sinf(angle_rad);
        return mat3(float3{c, s, 0}, float3{-s, c, 0}, float3{0, 0, 1});
    }
    MM_FORCE_INLINE static mat3 rotation_around(f32 angle_rad, const vec2 &center) noexcept {
        return translation(center) * rotation(angle_rad) * translation(-center);
    }
    MM_FORCE_INLINE static constexpr mat3 scaling(f32 sx, f32 sy) noexcept { return mat3(float3{sx, 0, 0}, float3{0, sy, 0}, float3{0, 0, 1}); }

    // =============================================================================
    // Transformations
    // =============================================================================
    MM_FORCE_INLINE vec3                  transform_point(const vec3 &point) const noexcept { return (*this) * point; }
    MM_FORCE_INLINE constexpr vec3        transform_vector(const vec3 &vec) const noexcept {
        // For affine transformations, the w component of a vector is 0
        vec3 v = (*this) * vec3{vec[0], vec[1], 0};
        return vec3{v[0], v[1], 0};
    }

    // =============================================================================
    // Transformation 2D convenience functions
    // =============================================================================
    MM_FORCE_INLINE vec2 transform_point(const vec2 &point) const noexcept {
        vec3 p = (*this) * vec3{point[0], point[1], 1};
        return vec2{p[0], p[1]};
    }
    MM_FORCE_INLINE constexpr vec2 transform_vector(const vec2 &vec) const noexcept {
        vec3 v = (*this) * vec3{vec[0], vec[1], 0};
        return vec2{v[0], v[1]};
    }
    MM_FORCE_INLINE static vec2 rotate_point(const vec2 &point, f32 angle_rad) noexcept { return rotation(angle_rad).transform_point(point); }
};

} // namespace mm_math

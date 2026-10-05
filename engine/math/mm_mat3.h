// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include "mm_vec2.h"
#include "mm_vec3.h"

namespace mm_math {
// =============================================================================
// mat3 struct (column-major 3x3 for 2D affine transforms)
// =============================================================================
struct mat3 {
    // Column-major storage: cols[0] = col0 (X axis), cols[1] = col1 (Y axis), cols[2] = col2 (translation)
    // Stored as vec3 (x,y,z) where z=1 for translation column, 0 for rotation/scale columns
    vec3 cols[3];

    // Constructors
    MM_FORCE_INLINE constexpr mat3() noexcept
        : cols{vec3{1, 0, 0}, vec3{0, 1, 0}, vec3{0, 0, 1}} {}

    MM_FORCE_INLINE constexpr mat3(f32 diagonal) noexcept
        : cols{vec3{diagonal, 0, 0}, vec3{0, diagonal, 0}, vec3{0, 0, diagonal}} {}

    MM_FORCE_INLINE constexpr mat3(vec3 c0, vec3 c1, vec3 c2) noexcept
        : cols{c0, c1, c2} {}

    MM_FORCE_INLINE static constexpr mat3 identity() noexcept { return mat3(1.0F); }

    MM_FORCE_INLINE constexpr mat3        operator+(const mat3 &other) const noexcept {
        return mat3(cols[0] + other.cols[0], cols[1] + other.cols[1], cols[2] + other.cols[2]);
    }

    MM_FORCE_INLINE constexpr mat3 operator-(const mat3 &other) const noexcept {
        return mat3(cols[0] - other.cols[0], cols[1] - other.cols[1], cols[2] - other.cols[2]);
    }

    // mat3 × mat3 (column-major): result.col[j] = this * other.col[j]
    MM_FORCE_INLINE mat3 operator*(const mat3 &other) const noexcept {
        return mat3(
            *this * other.cols[0],
            *this * other.cols[1],
            *this * other.cols[2]
        );
    }

    // mat3 × vec3 (column-major): col0*v.x + col1*v.y + col2*v.z
    MM_FORCE_INLINE vec3 operator*(const vec3 &vec) const noexcept {
        vec3 r = cols[0] * vec.x;
        r += cols[1] * vec.y;
        r += cols[2] * vec.z;
        return r;
    }

    MM_FORCE_INLINE constexpr mat3        operator*(f32 scalar) const noexcept { return mat3(cols[0] * scalar, cols[1] * scalar, cols[2] * scalar); }

    MM_FORCE_INLINE mat3 &operator*=(const mat3 &other) noexcept {
        *this = *this * other;
        return *this;
    }

    MM_FORCE_INLINE constexpr friend mat3 operator*(f32 scalar, const mat3 &m) noexcept { return m * scalar; }

    // =============================================================================
    // Transpose
    // =============================================================================
    MM_FORCE_INLINE mat3 transpose() const noexcept {
        return mat3(
            vec3{cols[0][0], cols[1][0], cols[2][0]},
            vec3{cols[0][1], cols[1][1], cols[2][1]},
            vec3{cols[0][2], cols[1][2], cols[2][2]}
        );
    }

    // =============================================================================
    // Inverse (general 3x3, adjugate / determinant)
    // =============================================================================
    // With columns a=c0, b=c1, c=c2 and det = a·(b×c), the ROWS of
    // M^-1 are (b×c)/det, (c×a)/det, (a×b)/det, so column j of the
    // inverse is ((b×c)[j], (c×a)[j], (a×b)[j]) / det. Returns the
    // identity when det is (near) zero — a singular matrix has no
    // inverse, and identity is the safe no-op for the 2D affine
    // transforms mat3 is used for.
    MM_FORCE_INLINE mat3 inverse() const noexcept {
        const vec3 &a = cols[0];
        const vec3 &b = cols[1];
        const vec3 &c = cols[2];
        const vec3 bc = b.cross(c);
        const vec3 ca = c.cross(a);
        const vec3 ab = a.cross(b);
        const f32 det = a.dot(bc);
        if (__builtin_fabsf(det) < 1e-9F) {
            return mat3(1.0F);
        }
        const f32 inv = 1.0F / det;
        return mat3(
            vec3{bc.x * inv, ca.x * inv, ab.x * inv},
            vec3{bc.y * inv, ca.y * inv, ab.y * inv},
            vec3{bc.z * inv, ca.z * inv, ab.z * inv}
        );
    }

    // =============================================================================
    // Translation, rotation, scaling
    // =============================================================================
    MM_FORCE_INLINE static constexpr mat3 translation(f32 tx, f32 ty) noexcept {
        return mat3(vec3{1, 0, 0}, vec3{0, 1, 0}, vec3{tx, ty, 1});
    }

    MM_FORCE_INLINE static constexpr mat3 translation(const vec2 &t) noexcept { return translation(t.x, t.y); }

    MM_FORCE_INLINE static mat3           rotation(f32 angle_rad) noexcept {
        f32 c = __builtin_cosf(angle_rad);
        f32 s = __builtin_sinf(angle_rad);
        return mat3(vec3{c, s, 0}, vec3{-s, c, 0}, vec3{0, 0, 1});
    }

    MM_FORCE_INLINE static mat3 rotation_around(f32 angle_rad, const vec2 &center) noexcept {
        return translation(center) * rotation(angle_rad) * translation(-center);
    }

    MM_FORCE_INLINE static constexpr mat3 scaling(f32 sx, f32 sy) noexcept {
        return mat3(vec3{sx, 0, 0}, vec3{0, sy, 0}, vec3{0, 0, 1});
    }

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

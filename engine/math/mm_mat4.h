// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include "mm_vec3.h"
#include "mm_vec4.h"
#include <cstring>

namespace mm_math {

struct mat4 {
    // Column-major storage: cols[0] = column 0 (X axis), cols[3] = column 3 (translation)
    // This matches GPU expectations (OpenGL/Metal/Vulkan all expect column-major)
    // mat4 * vec4 = col0*v.x + col1*v.y + col2*v.z + col3*v.w (fused MAD)
    float4 cols[4];

    MM_FORCE_INLINE constexpr mat4() noexcept
        : cols{float4{1, 0, 0, 0}, float4{0, 1, 0, 0}, float4{0, 0, 1, 0}, float4{0, 0, 0, 1}} {}

    MM_FORCE_INLINE constexpr mat4(f32 diagonal) noexcept
        : cols{float4{diagonal, 0, 0, 0}, float4{0, diagonal, 0, 0},
               float4{0, 0, diagonal, 0}, float4{0, 0, 0, diagonal}} {}

    // Column-major constructor: c0=col0, c1=col1, c2=col2, c3=col3
    MM_FORCE_INLINE constexpr mat4(float4 c0, float4 c1, float4 c2, float4 c3) noexcept
        : cols{c0, c1, c2, c3} {}

    MM_FORCE_INLINE static constexpr mat4 identity() noexcept { return mat4(1.0F); }

    // =============================================================================
    // Projection matrices — API-specific variants (column-major)
    // =============================================================================
    MM_FORCE_INLINE static mat4 ortho_gl(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        f32 rcp_width  = 1.0F / (right - left);
        f32 rcp_height = 1.0F / (top - bottom);
        f32 rcp_depth  = 1.0F / (far - near);
        // +2 * rcp_depth, NOT -2: the depth scale and the depth offset have to agree
        // on which end "near" is. This used to carry a -2 with the standard
        // -(far+near) offset, i.e. only the scale was flipped, so the depth range
        // mapped to [-(3f+n)/(f-n), -(f+3n)/(f-n)] instead of [-1, 1] - outside the
        // clip volume for every textbook near>0/far>near pair. It survived because
        // every call site passes near = -1, far = +1, and then far + near == 0 kills
        // the offset term, leaving a coincidentally in-range flip. perspective_gl two
        // functions down is the textbook form, which is what gave it away.
        return mat4(
            float4{2.0F * rcp_width, 0, 0, 0},
            float4{0, 2.0F * rcp_height, 0, 0},
            float4{0, 0, 2.0F * rcp_depth, 0},
            float4{-(right + left) * rcp_width, -(top + bottom) * rcp_height, -(far + near) * rcp_depth, 1.0F}
        );
    }

    MM_FORCE_INLINE static mat4 ortho_vk(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        f32 rcp_width  = 1.0F / (right - left);
        f32 rcp_height = 1.0F / (top - bottom);
        f32 rcp_depth  = 1.0F / (far - near);
        // Vulkan: depth [0,1] and Y flipped (its NDC y points down).
        return mat4(
            float4{2.0F * rcp_width, 0, 0, 0},
            float4{0, -2.0F * rcp_height, 0, 0},
            float4{0, 0, rcp_depth, 0},
            float4{-(right + left) * rcp_width, (top + bottom) * rcp_height, -near * rcp_depth, 1.0F}
        );
    }

    MM_FORCE_INLINE static mat4 ortho_mt(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        f32 rcp_width  = 1.0F / (right - left);
        f32 rcp_height = 1.0F / (top - bottom);
        f32 rcp_depth  = 1.0F / (far - near);
        // Metal: depth [0,1] like Vulkan, but Y NOT flipped (Metal's NDC y is up,
        // same as GL). This used to be a byte-for-byte copy of ortho_gl - both the
        // inverted scale above and GL's [-1,1] depth range, where Metal clips to
        // [0,1] and the whole near half of the range lands at z < 0.
        return mat4(
            float4{2.0F * rcp_width, 0, 0, 0},
            float4{0, 2.0F * rcp_height, 0, 0},
            float4{0, 0, rcp_depth, 0},
            float4{-(right + left) * rcp_width, -(top + bottom) * rcp_height, -near * rcp_depth, 1.0F}
        );
    }

    MM_FORCE_INLINE static mat4 perspective_gl(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept {
        f32 tan_half_fov = __builtin_tanf(fov_y * 0.5F);
        f32 rcp_range    = 1.0F / (near - far);
        return mat4(
            float4{1.0F / (aspect * tan_half_fov), 0, 0, 0},
            float4{0, 1.0F / tan_half_fov, 0, 0},
            float4{0, 0, (far + near) * rcp_range, -1.0F},
            float4{0, 0, 2.0F * far * near * rcp_range, 0}
        );
    }

    MM_FORCE_INLINE static mat4 perspective_vk(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept {
        f32 tan_half_fov = __builtin_tanf(fov_y * 0.5F);
        f32 rcp_range    = 1.0F / (near - far);
        return mat4(
            float4{1.0F / (aspect * tan_half_fov), 0, 0, 0},
            float4{0, -1.0F / tan_half_fov, 0, 0},
            float4{0, 0, far * rcp_range, -1.0F},
            float4{0, 0, far * near * rcp_range, 0}
        );
    }

    MM_FORCE_INLINE static mat4 perspective_mt(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept {
        f32 tan_half_fov = __builtin_tanf(fov_y * 0.5F);
        f32 rcp_range    = 1.0F / (near - far);
        return mat4(
            float4{1.0F / (aspect * tan_half_fov), 0, 0, 0},
            float4{0, 1.0F / tan_half_fov, 0, 0},
            float4{0, 0, far * rcp_range, -1.0F},
            float4{0, 0, far * near * rcp_range, 0}
        );
    }

    // =============================================================================
    // Projection matrices — compile-time wrapper (depends on MM_VULKAN / MM_METAL / MM_OPENGLES)
    // =============================================================================
#if MM_VULKAN
    MM_FORCE_INLINE static mat4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        return ortho_vk(left, right, bottom, top, near, far);
    }
    MM_FORCE_INLINE static mat4 perspective(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept { return perspective_vk(fov_y, aspect, near, far); }
#elif MM_METAL
    MM_FORCE_INLINE static mat4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        return ortho_mt(left, right, bottom, top, near, far);
    }
    MM_FORCE_INLINE static mat4 perspective(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept { return perspective_mt(fov_y, aspect, near, far); }
#else
    MM_FORCE_INLINE static mat4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        return ortho_gl(left, right, bottom, top, near, far);
    }
    MM_FORCE_INLINE static mat4 perspective(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept { return perspective_gl(fov_y, aspect, near, far); }
#endif

    // =============================================================================
    // Translation, rotation, scaling (column-major)
    // =============================================================================
    MM_FORCE_INLINE static mat4 translation(f32 tx, f32 ty, f32 tz) noexcept {
        return mat4(
            float4{1, 0, 0, 0},
            float4{0, 1, 0, 0},
            float4{0, 0, 1, 0},
            float4{tx, ty, tz, 1.0F}
        );
    }
    MM_FORCE_INLINE static mat4 translation(const vec3 &t) noexcept { return translation(t.x, t.y, t.z); }

    MM_FORCE_INLINE static mat4 rotation_x(f32 angle_rad) noexcept {
        f32 c = __builtin_cosf(angle_rad);
        f32 s = __builtin_sinf(angle_rad);
        return mat4(
            float4{1, 0, 0, 0},
            float4{0, c, s, 0},
            float4{0, -s, c, 0},
            float4{0, 0, 0, 1.0F}
        );
    }

    MM_FORCE_INLINE static mat4 rotation_y(f32 angle_rad) noexcept {
        f32 c = __builtin_cosf(angle_rad);
        f32 s = __builtin_sinf(angle_rad);
        return mat4(
            float4{c, 0, -s, 0},
            float4{0, 1, 0, 0},
            float4{s, 0, c, 0},
            float4{0, 0, 0, 1.0F}
        );
    }

    MM_FORCE_INLINE static mat4 rotation_z(f32 angle_rad) noexcept {
        f32 c = __builtin_cosf(angle_rad);
        f32 s = __builtin_sinf(angle_rad);
        return mat4(
            float4{c, s, 0, 0},
            float4{-s, c, 0, 0},
            float4{0, 0, 1, 0},
            float4{0, 0, 0, 1.0F}
        );
    }

    MM_FORCE_INLINE static mat4 scaling(f32 sx, f32 sy, f32 sz) noexcept {
        return mat4(
            float4{sx, 0, 0, 0},
            float4{0, sy, 0, 0},
            float4{0, 0, sz, 0},
            float4{0, 0, 0, 1.0F}
        );
    }
    MM_FORCE_INLINE static mat4 scaling(const vec3 &s) noexcept { return scaling(s.x, s.y, s.z); }

    // =============================================================================
    // Look-at matrix (column-major)
    // =============================================================================
    MM_FORCE_INLINE static mat4 look_at(const vec3 &eye, const vec3 &target, const vec3 &up) noexcept {
        vec3 f = (target - eye).normalized();
        vec3 s = f.cross(up).normalized();
        vec3 u = s.cross(f);

        return mat4(
            float4{s.x, u.x, -f.x, 0},
            float4{s.y, u.y, -f.y, 0},
            float4{s.z, u.z, -f.z, 0},
            float4{-s.dot(eye), -u.dot(eye), f.dot(eye), 1.0F}
        );
    }

    // =============================================================================
    // Operators (column-major)
    // =============================================================================
    MM_FORCE_INLINE constexpr mat4 operator+(const mat4 &other) const noexcept {
        return mat4(cols[0] + other.cols[0], cols[1] + other.cols[1], cols[2] + other.cols[2], cols[3] + other.cols[3]);
    }

    MM_FORCE_INLINE constexpr mat4 operator-(const mat4 &other) const noexcept {
        return mat4(cols[0] - other.cols[0], cols[1] - other.cols[1], cols[2] - other.cols[2], cols[3] - other.cols[3]);
    }

    // mat4 × mat4 (column-major): result.col[j] = this * other.col[j]
    MM_FORCE_INLINE mat4 operator*(const mat4 &other) const noexcept {
        return mat4(
            *this * other.cols[0],
            *this * other.cols[1],
            *this * other.cols[2],
            *this * other.cols[3]
        );
    }

    // mat4 × vec4 = col0*v.x + col1*v.y + col2*v.z + col3*v.w (fused MAD)
    MM_FORCE_INLINE vec4 operator*(const vec4 &vec) const noexcept {
        float4 r = cols[0] * vec.x;
        r += cols[1] * vec.y;
        r += cols[2] * vec.z;
        r += cols[3] * vec.w;
        return vec4(r);
    }

    MM_FORCE_INLINE float4 operator*(const float4 &v) const noexcept {
        float4 r = cols[0] * v[0];
        r += cols[1] * v[1];
        r += cols[2] * v[2];
        r += cols[3] * v[3];
        return r;
    }

    MM_FORCE_INLINE constexpr mat4 operator*(f32 scalar) const noexcept {
        return mat4(cols[0] * scalar, cols[1] * scalar, cols[2] * scalar, cols[3] * scalar);
    }

    MM_FORCE_INLINE mat4 &operator*=(const mat4 &other) noexcept {
        *this = *this * other;
        return *this;
    }

    MM_FORCE_INLINE constexpr friend mat4 operator*(f32 scalar, const mat4 &m) noexcept { return m * scalar; }

    // =============================================================================
    // Transpose (column-major ↔ row-major)
    // =============================================================================
    MM_FORCE_INLINE mat4 transpose() const noexcept {
        return mat4(
            float4{cols[0][0], cols[1][0], cols[2][0], cols[3][0]},
            float4{cols[0][1], cols[1][1], cols[2][1], cols[3][1]},
            float4{cols[0][2], cols[1][2], cols[2][2], cols[3][2]},
            float4{cols[0][3], cols[1][3], cols[2][3], cols[3][3]}
        );
    }

    // =============================================================================
    // Inverse (for orthogonal matrices, i.e. rotation + translation)
    // =============================================================================
    MM_FORCE_INLINE mat4 inverse_ortho() const noexcept {
        // For M = [R | t; 0 0 0 1] with R orthonormal (rotation, NO scale):
        //     M^-1 = [R^T | -R^T * t]
        //
        // The one thing to get right here, and what this function got wrong for its
        // whole life: in column-major storage `cols[c][r]` is the element at ROW r,
        // COLUMN c. So (cols[0][i], cols[1][i], cols[2][i]) is R's ROW i, and
        // (cols[i][0], cols[i][1], cols[i][2]) is R's COLUMN i.
        //
        // Both halves of the old version read the row and used it as a column:
        //  - the 3x3 block came out as R instead of R^T (the ctor takes COLUMNS, so
        //    column j of the result must be R's row j — the code interleaved them);
        //  - inv_t dotted R's ROWS with t, which is R*t, but -R^T*t component i is
        //    R's COLUMN i dotted with t.
        // Fixing only the first half still fails: on translate(5,6,7)*rot_x(90°) the
        // 3x3 came out right and inv_t.y was +7 where it must be -7. Same bug class
        // as the SIMD `look_at` rewrite that stored basis rows as columns — assert
        // the INVARIANT (m * inverse_ortho(m) == I), never hand-computed numbers.
        const vec3 r0{cols[0][0], cols[1][0], cols[2][0]}; // R row 0 = R^T column 0
        const vec3 r1{cols[0][1], cols[1][1], cols[2][1]}; // R row 1 = R^T column 1
        const vec3 r2{cols[0][2], cols[1][2], cols[2][2]}; // R row 2 = R^T column 2
        const vec3 c0{cols[0][0], cols[0][1], cols[0][2]}; // R column 0 = R^T row 0
        const vec3 c1{cols[1][0], cols[1][1], cols[1][2]};
        const vec3 c2{cols[2][0], cols[2][1], cols[2][2]};
        const vec3 t{cols[3][0], cols[3][1], cols[3][2]};
        // -R^T * t: component i is R's column i dotted with t.
        const vec3 inv_t{
            -(c0.x * t.x + c0.y * t.y + c0.z * t.z),
            -(c1.x * t.x + c1.y * t.y + c1.z * t.z),
            -(c2.x * t.x + c2.y * t.y + c2.z * t.z)
        };
        // Column-major ctor: column j of the result is R's row j.
        return mat4(
            float4{r0.x, r0.y, r0.z, 0},
            float4{r1.x, r1.y, r1.z, 0},
            float4{r2.x, r2.y, r2.z, 0},
            float4{inv_t.x, inv_t.y, inv_t.z, 1}
        );
    }

    // Store as column-major float[16] — direct memcpy (already column-major!)
    MM_FORCE_INLINE void store_column_major(f32* dst) const noexcept {
        std::memcpy(dst, cols, 64);
    }

    MM_FORCE_INLINE f32* data() noexcept { return reinterpret_cast<f32*>(cols); }
    MM_FORCE_INLINE const f32* data() const noexcept { return reinterpret_cast<const f32*>(cols); }
};

} // namespace mm_math

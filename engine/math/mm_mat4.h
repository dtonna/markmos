// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include "mm_vec3.h"
#include "mm_vec4.h"

namespace mm_math {

struct mat4 {
    float4 rows[4];

    MM_FORCE_INLINE constexpr mat4() noexcept : rows{float4{1, 0, 0, 0}, float4{0, 1, 0, 0}, float4{0, 0, 1, 0}, float4{0, 0, 0, 1}} {}
    MM_FORCE_INLINE constexpr mat4(f32 diagonal) noexcept
        : rows{float4{diagonal, 0, 0, 0}, float4{0, diagonal, 0, 0}, float4{0, 0, diagonal, 0}, float4{0, 0, 0, diagonal}} {}
    MM_FORCE_INLINE constexpr mat4(float4 r0, float4 r1, float4 r2, float4 r3) noexcept : rows{r0, r1, r2, r3} {}

    MM_FORCE_INLINE static constexpr mat4 identity() noexcept { return mat4(1.0F); }

    // =============================================================================
    // Projection matrices — API-specific variants
    // =============================================================================
    MM_FORCE_INLINE static mat4 ortho_gl(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        f32 rcp_width  = 1.0F / (right - left);
        f32 rcp_height = 1.0F / (top - bottom);
        f32 rcp_depth  = 1.0F / (far - near);
        return mat4(
            float4{2.0F * rcp_width, 0, 0, -(right + left) * rcp_width},
            float4{0, 2.0F * rcp_height, 0, -(top + bottom) * rcp_height},
            float4{0, 0, -2.0F * rcp_depth, -(far + near) * rcp_depth},
            float4{0, 0, 0, 1.0F}
        );
    }
    MM_FORCE_INLINE static mat4 ortho_vk(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        f32 rcp_width  = 1.0F / (right - left);
        f32 rcp_height = 1.0F / (top - bottom);
        f32 rcp_depth  = 1.0F / (far - near);
        return mat4(
            float4{2.0F * rcp_width, 0, 0, -(right + left) * rcp_width},
            float4{0, -2.0F * rcp_height, 0, (top + bottom) * rcp_height},
            float4{0, 0, rcp_depth, -near * rcp_depth},
            float4{0, 0, 0, 1.0F}
        );
    }
    MM_FORCE_INLINE static mat4 ortho_mt(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) noexcept {
        f32 rcp_width  = 1.0F / (right - left);
        f32 rcp_height = 1.0F / (top - bottom);
        f32 rcp_depth  = 1.0F / (far - near);
        return mat4(
            float4{2.0F * rcp_width, 0, 0, -(right + left) * rcp_width},
            float4{0, 2.0F * rcp_height, 0, -(top + bottom) * rcp_height},
            float4{0, 0, -2.0F * rcp_depth, -(far + near) * rcp_depth},
            float4{0, 0, 0, 1.0F}
        );
    }

    MM_FORCE_INLINE static mat4 perspective_gl(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept {
        f32 tan_half_fov = __builtin_tanf(fov_y * 0.5F);
        f32 rcp_range    = 1.0F / (near - far);
        return mat4(float4{1.0F / (aspect * tan_half_fov), 0, 0, 0}, float4{0, 1.0F / tan_half_fov, 0, 0},
                    float4{0, 0, (far + near) * rcp_range, 2.0F * far * near * rcp_range}, float4{0, 0, -1.0F, 0});
    }
    MM_FORCE_INLINE static mat4 perspective_vk(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept {
        f32 tan_half_fov = __builtin_tanf(fov_y * 0.5F);
        f32 rcp_range    = 1.0F / (near - far);
        return mat4(float4{1.0F / (aspect * tan_half_fov), 0, 0, 0}, float4{0, -1.0F / tan_half_fov, 0, 0},
                    float4{0, 0, far * rcp_range, far * near * rcp_range}, float4{0, 0, -1.0F, 0});
    }
    MM_FORCE_INLINE static mat4 perspective_mt(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept {
        f32 tan_half_fov = __builtin_tanf(fov_y * 0.5F);
        f32 rcp_range    = 1.0F / (near - far);
        return mat4(float4{1.0F / (aspect * tan_half_fov), 0, 0, 0}, float4{0, 1.0F / tan_half_fov, 0, 0},
                    float4{0, 0, far * rcp_range, far * near * rcp_range}, float4{0, 0, -1.0F, 0});
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
    // Translation, rotation, scaling
    // =============================================================================
    MM_FORCE_INLINE static mat4 translation(f32 tx, f32 ty, f32 tz) noexcept {
        return mat4(float4{1, 0, 0, 0}, float4{0, 1, 0, 0}, float4{0, 0, 1, 0}, float4{tx, ty, tz, 1.0F});
    }
    MM_FORCE_INLINE static mat4 translation(const vec3 &t) noexcept { return translation(t.x, t.y, t.z); }

    MM_FORCE_INLINE static mat4 rotation_x(f32 angle_rad) noexcept {
        f32 c = __builtin_cosf(angle_rad);
        f32 s = __builtin_sinf(angle_rad);
        return mat4(float4{1, 0, 0, 0}, float4{0, c, s, 0}, float4{0, -s, c, 0}, float4{0, 0, 0, 1.0F});
    }
    MM_FORCE_INLINE static mat4 rotation_y(f32 angle_rad) noexcept {
        f32 c = __builtin_cosf(angle_rad);
        f32 s = __builtin_sinf(angle_rad);
        return mat4(float4{c, 0, -s, 0}, float4{0, 1, 0, 0}, float4{s, 0, c, 0}, float4{0, 0, 0, 1.0F});
    }
    MM_FORCE_INLINE static mat4 rotation_z(f32 angle_rad) noexcept {
        f32 c = __builtin_cosf(angle_rad);
        f32 s = __builtin_sinf(angle_rad);
        return mat4(float4{c, s, 0, 0}, float4{-s, c, 0, 0}, float4{0, 0, 1, 0}, float4{0, 0, 0, 1.0F});
    }

    MM_FORCE_INLINE static mat4 scaling(f32 sx, f32 sy, f32 sz) noexcept {
        return mat4(float4{sx, 0, 0, 0}, float4{0, sy, 0, 0}, float4{0, 0, sz, 0}, float4{0, 0, 0, 1.0F});
    }
    MM_FORCE_INLINE static mat4 scaling(const vec3 &s) noexcept { return scaling(s.x, s.y, s.z); }

    // =============================================================================
    // Look-at matrix
    // =============================================================================
    MM_FORCE_INLINE static mat4 look_at(const vec3 &eye, const vec3 &target, const vec3 &up) noexcept {
        vec3 f = (target - eye).normalized();
        vec3 s = f.cross(up).normalized();
        vec3 u = s.cross(f);

        return mat4(float4{s.x, u.x, -f.x, 0}, float4{s.y, u.y, -f.y, 0}, float4{s.z, u.z, -f.z, 0}, float4{-s.dot(eye), -u.dot(eye), f.dot(eye), 1.0F});
    }

    // =============================================================================
    // Operators
    // =============================================================================
    MM_FORCE_INLINE constexpr mat4 operator+(const mat4 &other) const noexcept {
        return mat4(rows[0] + other.rows[0], rows[1] + other.rows[1], rows[2] + other.rows[2], rows[3] + other.rows[3]);
    }

    MM_FORCE_INLINE constexpr mat4 operator-(const mat4 &other) const noexcept {
        return mat4(rows[0] - other.rows[0], rows[1] - other.rows[1], rows[2] - other.rows[2], rows[3] - other.rows[3]);
    }

    MM_FORCE_INLINE mat4 operator*(const mat4 &other) const noexcept {
        mat4 result;
        for (int i = 0; i < 4; ++i) {
            float4 r       = rows[i];
            result.rows[i] = float4{r[0] * other.rows[0][0] + r[1] * other.rows[1][0] + r[2] * other.rows[2][0] + r[3] * other.rows[3][0],
                                    r[0] * other.rows[0][1] + r[1] * other.rows[1][1] + r[2] * other.rows[2][1] + r[3] * other.rows[3][1],
                                    r[0] * other.rows[0][2] + r[1] * other.rows[1][2] + r[2] * other.rows[2][2] + r[3] * other.rows[3][2],
                                    r[0] * other.rows[0][3] + r[1] * other.rows[1][3] + r[2] * other.rows[2][3] + r[3] * other.rows[3][3]};
        }
        return result;
    }

    MM_FORCE_INLINE vec4 operator*(const vec4 &vec) const noexcept {
        return vec4(float4{rows[0][0] * vec[0] + rows[0][1] * vec[1] + rows[0][2] * vec[2] + rows[0][3] * vec[3],
                           rows[1][0] * vec[0] + rows[1][1] * vec[1] + rows[1][2] * vec[2] + rows[1][3] * vec[3],
                           rows[2][0] * vec[0] + rows[2][1] * vec[1] + rows[2][2] * vec[2] + rows[2][3] * vec[3],
                           rows[3][0] * vec[0] + rows[3][1] * vec[1] + rows[3][2] * vec[2] + rows[3][3] * vec[3]});
    }

    MM_FORCE_INLINE float4 operator*(const float4 &v) const noexcept {
        return float4{rows[0][0] * v[0] + rows[0][1] * v[1] + rows[0][2] * v[2] + rows[0][3] * v[3],
                      rows[1][0] * v[0] + rows[1][1] * v[1] + rows[1][2] * v[2] + rows[1][3] * v[3],
                      rows[2][0] * v[0] + rows[2][1] * v[1] + rows[2][2] * v[2] + rows[2][3] * v[3],
                      rows[3][0] * v[0] + rows[3][1] * v[1] + rows[3][2] * v[2] + rows[3][3] * v[3]};
    }

    MM_FORCE_INLINE constexpr mat4 operator*(f32 scalar) const noexcept { return mat4(rows[0] * scalar, rows[1] * scalar, rows[2] * scalar, rows[3] * scalar); }

    MM_FORCE_INLINE constexpr friend mat4 operator*(f32 scalar, const mat4 &m) noexcept { return m * scalar; }

    // =============================================================================
    // Transpose
    // =============================================================================
    MM_FORCE_INLINE mat4                  transpose() const noexcept {
        return mat4(float4{rows[0][0], rows[1][0], rows[2][0], rows[3][0]}, float4{rows[0][1], rows[1][1], rows[2][1], rows[3][1]},
                                     float4{rows[0][2], rows[1][2], rows[2][2], rows[3][2]}, float4{rows[0][3], rows[1][3], rows[2][3], rows[3][3]});
    }

    // =============================================================================
    // Inverse (for orthogonal matrices, i.e. rotation + translation)
    // =============================================================================
    MM_FORCE_INLINE mat4 inverse_ortho() const noexcept {
        auto r0    = float4{rows[0][0], rows[1][0], rows[2][0], 0};
        auto r1    = float4{rows[0][1], rows[1][1], rows[2][1], 0};
        auto r2    = float4{rows[0][2], rows[1][2], rows[2][2], 0};
        auto t     = rows[3];
        auto inv_t = float4{-(t[0] * r0[0] + t[1] * r0[1] + t[2] * r0[2]), -(t[0] * r1[0] + t[1] * r1[1] + t[2] * r1[2]),
                            -(t[0] * r2[0] + t[1] * r2[1] + t[2] * r2[2]), 1.0F};
        return mat4(r0, r1, r2, inv_t);
    }

    // Store as column-major float[16] (OpenGL convention, for GPU upload)
    MM_FORCE_INLINE void store_column_major(f32* dst) const noexcept {
        dst[0]  = rows[0][0]; dst[1]  = rows[1][0]; dst[2]  = rows[2][0]; dst[3]  = rows[3][0];
        dst[4]  = rows[0][1]; dst[5]  = rows[1][1]; dst[6]  = rows[2][1]; dst[7]  = rows[3][1];
        dst[8]  = rows[0][2]; dst[9]  = rows[1][2]; dst[10] = rows[2][2]; dst[11] = rows[3][2];
        dst[12] = rows[0][3]; dst[13] = rows[1][3]; dst[14] = rows[2][3]; dst[15] = rows[3][3];
    }
};

} // namespace mm_math

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include "mm_vec4.h"
#include <math.h>

namespace mm_math {

struct color {
    union {
        float4 v;
        struct alignas(16) {
            f32 r, g, b, a;
        };
        f32 data[4];
    };

    MM_FORCE_INLINE constexpr color() : v{1.0F, 1.0F, 1.0F, 1.0F} {}
    MM_FORCE_INLINE constexpr color(f32 r_, f32 g_, f32 b_, f32 a_) noexcept : v{r_, g_, b_, a_} {}
    MM_FORCE_INLINE constexpr color(f32 r_, f32 g_, f32 b_) noexcept : v{r_, g_, b_, 1.0F} {}
    MM_FORCE_INLINE explicit constexpr color(float4 v_) noexcept : v(v_) {}
    MM_FORCE_INLINE explicit color(const vec4 &c) noexcept : v{c.v} {}

    MM_FORCE_INLINE constexpr color operator+(const color &c) const noexcept { return color(v + c.v); }
    MM_FORCE_INLINE constexpr color operator-(const color &c) const noexcept { return color(v - c.v); }
    MM_FORCE_INLINE constexpr color operator*(const color &c) const noexcept { return color(v * c.v); }
    MM_FORCE_INLINE constexpr color operator*(f32 s) const noexcept { return color(v * float4{s, s, s, s}); }
    MM_FORCE_INLINE constexpr color operator/(f32 s) const noexcept { return color(v / float4{s, s, s, s}); }
    MM_FORCE_INLINE constexpr friend color operator*(f32 s, const color &c) noexcept { return c * s; }
    MM_FORCE_INLINE constexpr bool    operator==(const color &c) const noexcept { return nearly_equal(c); }
    MM_FORCE_INLINE constexpr bool    operator!=(const color &c) const noexcept { return !nearly_equal(c); }

    MM_FORCE_INLINE constexpr color &operator+=(const color &c) noexcept {
        v += c.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr color &operator-=(const color &c) noexcept {
        v -= c.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr color &operator*=(const color &c) noexcept {
        v *= c.v;
        return *this;
    }
    MM_FORCE_INLINE constexpr color &operator*=(f32 s) noexcept {
        v *= float4{s, s, s, s};
        return *this;
    }

    MM_FORCE_INLINE constexpr f32  operator[](int i) const noexcept { return data[i]; }
    MM_FORCE_INLINE constexpr f32 &operator[](int i) noexcept { return data[i]; }

    MM_FORCE_INLINE bool nearly_equal(const color &c, f32 eps = 1e-6F) const noexcept {
        float4 d = v - c.v;
        return (d.x * d.x + d.y * d.y + d.z * d.z + d.w * d.w) < eps * eps;
    }

    MM_FORCE_INLINE constexpr color clamped() const noexcept {
        return color(MM_CLAMP(r, 0.0F, 1.0F), MM_CLAMP(g, 0.0F, 1.0F), MM_CLAMP(b, 0.0F, 1.0F), MM_CLAMP(a, 0.0F, 1.0F));
    }
    MM_FORCE_INLINE void clamp() noexcept {
        r = MM_CLAMP(r, 0.0F, 1.0F);
        g = MM_CLAMP(g, 0.0F, 1.0F);
        b = MM_CLAMP(b, 0.0F, 1.0F);
        a = MM_CLAMP(a, 0.0F, 1.0F);
    }

    MM_FORCE_INLINE constexpr color with_alpha(f32 a_) const noexcept { return color(r, g, b, a_); }
    MM_FORCE_INLINE constexpr color premultiplied() const noexcept { return color(r * a, g * a, b * a, a); }

    MM_FORCE_INLINE constexpr color lerp(const color &other, f32 t) const noexcept {
        return color(v + (other.v - v) * float4{t, t, t, t});
    }

    MM_FORCE_INLINE constexpr color blend_over(const color &dst) const noexcept {
        f32 out_a = a + dst.a * (1.0F - a);
        if (out_a < 1e-8F) {
            return color(0.0F, 0.0F, 0.0F, 0.0F);
        }
        return color((r * a + dst.r * dst.a * (1.0F - a)) / out_a,
                     (g * a + dst.g * dst.a * (1.0F - a)) / out_a,
                     (b * a + dst.b * dst.a * (1.0F - a)) / out_a,
                     out_a);
    }

    MM_FORCE_INLINE constexpr color grayscale() const noexcept {
        f32 gray = 0.299F * r + 0.587F * g + 0.114F * b;
        return color(gray, gray, gray, a);
    }

    MM_FORCE_INLINE constexpr color adjust_brightness(f32 factor) const noexcept {
        return color(r * factor, g * factor, b * factor, a);
    }

    MM_FORCE_INLINE constexpr color adjust_saturation(f32 factor) const noexcept {
        f32 gray = 0.299F * r + 0.587F * g + 0.114F * b;
        return color(gray + (r - gray) * factor,
                     gray + (g - gray) * factor,
                     gray + (b - gray) * factor,
                     a);
    }

    MM_FORCE_INLINE u32 to_u32_rgba() const noexcept {
        return ((u32)(r * 255.0F + 0.5F) << 24) |
               ((u32)(g * 255.0F + 0.5F) << 16) |
               ((u32)(b * 255.0F + 0.5F) << 8) |
               ((u32)(a * 255.0F + 0.5F));
    }
    MM_FORCE_INLINE u32 to_u32_argb() const noexcept {
        return ((u32)(a * 255.0F + 0.5F) << 24) |
               ((u32)(r * 255.0F + 0.5F) << 16) |
               ((u32)(g * 255.0F + 0.5F) << 8) |
               ((u32)(b * 255.0F + 0.5F));
    }
    // Engine pixel format: 0xAABBGGRR
    MM_FORCE_INLINE u32 to_u32_bgra() const noexcept {
        return ((u32)(a * 255.0F + 0.5F) << 24) |
               ((u32)(b * 255.0F + 0.5F) << 16) |
               ((u32)(g * 255.0F + 0.5F) << 8) |
               ((u32)(r * 255.0F + 0.5F));
    }

    MM_FORCE_INLINE static color from_u32_rgba(u32 rgba) noexcept {
        return color((f32)((rgba >> 24) & 0xFF) / 255.0F,
                     (f32)((rgba >> 16) & 0xFF) / 255.0F,
                     (f32)((rgba >> 8) & 0xFF) / 255.0F,
                     (f32)(rgba & 0xFF) / 255.0F);
    }
    MM_FORCE_INLINE static color from_u32_argb(u32 argb) noexcept {
        return color((f32)((argb >> 16) & 0xFF) / 255.0F,
                     (f32)((argb >> 8) & 0xFF) / 255.0F,
                     (f32)(argb & 0xFF) / 255.0F,
                     (f32)((argb >> 24) & 0xFF) / 255.0F);
    }

    MM_FORCE_INLINE static color from_hsv(f32 h, f32 s, f32 v, f32 a_ = 1.0F) noexcept {
        f32 c = v * s;
        f32 hp = h / 60.0F;
        f32 x = c * (1.0F - __builtin_fabsf(__builtin_fmodf(hp, 2.0F) - 1.0F));
        f32 m = v - c;

        f32 r_, g_, b_;
        if (hp < 1.0F) { r_ = c; g_ = x; b_ = 0.0F; }
        else if (hp < 2.0F) { r_ = x; g_ = c; b_ = 0.0F; }
        else if (hp < 3.0F) { r_ = 0.0F; g_ = c; b_ = x; }
        else if (hp < 4.0F) { r_ = 0.0F; g_ = x; b_ = c; }
        else if (hp < 5.0F) { r_ = x; g_ = 0.0F; b_ = c; }
        else { r_ = c; g_ = 0.0F; b_ = x; }

        return color(r_ + m, g_ + m, b_ + m, a_);
    }

    static const color WHITE;
    static const color BLACK;
    static const color RED;
    static const color GREEN;
    static const color BLUE;
    static const color YELLOW;
    static const color CYAN;
    static const color MAGENTA;
    static const color TRANSPARENT;
};

inline const color color::WHITE       = color(1.0F, 1.0F, 1.0F, 1.0F);
inline const color color::BLACK       = color(0.0F, 0.0F, 0.0F, 1.0F);
inline const color color::RED         = color(1.0F, 0.0F, 0.0F, 1.0F);
inline const color color::GREEN       = color(0.0F, 1.0F, 0.0F, 1.0F);
inline const color color::BLUE        = color(0.0F, 0.0F, 1.0F, 1.0F);
inline const color color::YELLOW      = color(1.0F, 1.0F, 0.0F, 1.0F);
inline const color color::CYAN        = color(0.0F, 1.0F, 1.0F, 1.0F);
inline const color color::MAGENTA     = color(1.0F, 0.0F, 1.0F, 1.0F);
inline const color color::TRANSPARENT = color(0.0F, 0.0F, 0.0F, 0.0F);

} // namespace mm_math

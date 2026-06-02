// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include "mm_vec2.h"
#include <math.h>

namespace mm_math {

struct rect {
    f32 x, y, w, h;

    MM_FORCE_INLINE constexpr rect() noexcept : x(0.0F), y(0.0F), w(0.0F), h(0.0F) {}
    MM_FORCE_INLINE constexpr rect(f32 x_, f32 y_, f32 w_, f32 h_) noexcept : x(x_), y(y_), w(w_), h(h_) {}
    MM_FORCE_INLINE constexpr rect(const vec2 &pos, const vec2 &size) noexcept : x(pos.x), y(pos.y), w(size.x), h(size.y) {}

    MM_FORCE_INLINE constexpr rect(const rect &) noexcept = default;
    MM_FORCE_INLINE constexpr rect(rect &&) noexcept = default;
    MM_FORCE_INLINE constexpr rect &operator=(const rect &) noexcept = default;

    MM_FORCE_INLINE constexpr bool operator==(const rect &r) const noexcept { return x == r.x && y == r.y && w == r.w && h == r.h; }
    MM_FORCE_INLINE constexpr bool operator!=(const rect &r) const noexcept { return !(*this == r); }

    // =============================================================================
    // Properties
    // =============================================================================
    MM_FORCE_INLINE constexpr f32  left()   const noexcept { return x; }
    MM_FORCE_INLINE constexpr f32  right()  const noexcept { return x + w; }
    MM_FORCE_INLINE constexpr f32  top()    const noexcept { return y; }
    MM_FORCE_INLINE constexpr f32  bottom() const noexcept { return y + h; }

    MM_FORCE_INLINE constexpr vec2 min() const noexcept { return vec2(x, y); }
    MM_FORCE_INLINE constexpr vec2 max() const noexcept { return vec2(x + w, y + h); }
    MM_FORCE_INLINE constexpr vec2 center() const noexcept { return vec2(x + w * 0.5F, y + h * 0.5F); }
    MM_FORCE_INLINE constexpr vec2 size() const noexcept { return vec2(w, h); }
    MM_FORCE_INLINE constexpr vec2 position() const noexcept { return vec2(x, y); }
    MM_FORCE_INLINE constexpr f32  area() const noexcept { return w * h; }
    MM_FORCE_INLINE constexpr bool empty() const noexcept { return w <= 0.0F || h <= 0.0F; }

    // =============================================================================
    // Mutators
    // =============================================================================
    MM_FORCE_INLINE constexpr void set_position(f32 x_, f32 y_) noexcept { x = x_; y = y_; }
    MM_FORCE_INLINE constexpr void set_position(const vec2 &pos) noexcept { x = pos.x; y = pos.y; }
    MM_FORCE_INLINE constexpr void set_size(f32 w_, f32 h_) noexcept { w = w_; h = h_; }
    MM_FORCE_INLINE constexpr void set_size(const vec2 &sz) noexcept { w = sz.x; h = sz.y; }
    MM_FORCE_INLINE constexpr void translate(f32 dx, f32 dy) noexcept { x += dx; y += dy; }
    MM_FORCE_INLINE constexpr void translate(const vec2 &d) noexcept { x += d.x; y += d.y; }
    MM_FORCE_INLINE constexpr void inflate(f32 dw, f32 dh) noexcept { x -= dw; y -= dh; w += dw * 2.0F; h += dh * 2.0F; }

    // =============================================================================
    // Containment & intersection
    // =============================================================================
    MM_FORCE_INLINE constexpr bool contains(f32 px, f32 py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    MM_FORCE_INLINE constexpr bool contains(const vec2 &p) const noexcept { return contains(p.x, p.y); }
    MM_FORCE_INLINE constexpr bool contains(const rect &r) const noexcept {
        return r.x >= x && r.right() <= right() && r.y >= y && r.bottom() <= bottom();
    }
    MM_FORCE_INLINE constexpr bool intersects(const rect &r) const noexcept {
        return x < r.right() && right() > r.x && y < r.bottom() && bottom() > r.y;
    }

    // =============================================================================
    // Set operations
    // =============================================================================
    MM_FORCE_INLINE static constexpr rect intersection(const rect &a, const rect &b) noexcept {
        f32 l = MM_MAX(a.x, b.x);
        f32 t = MM_MAX(a.y, b.y);
        f32 r = MM_MIN(a.right(), b.right());
        f32 btm = MM_MIN(a.bottom(), b.bottom());
        if (l >= r || t >= btm) {
            return rect(0, 0, 0, 0);
        }
        return rect(l, t, r - l, btm - t);
    }
    MM_FORCE_INLINE static constexpr rect unite(const rect &a, const rect &b) noexcept {
        f32 l = MM_MIN(a.x, b.x);
        f32 t = MM_MIN(a.y, b.y);
        f32 r = MM_MAX(a.right(), b.right());
        f32 btm = MM_MAX(a.bottom(), b.bottom());
        return rect(l, t, r - l, btm - t);
    }

    // =============================================================================
    // Factories
    // =============================================================================
    MM_FORCE_INLINE static constexpr rect from_min_max(f32 min_x, f32 min_y, f32 max_x, f32 max_y) noexcept {
        return rect(min_x, min_y, max_x - min_x, max_y - min_y);
    }
    MM_FORCE_INLINE static constexpr rect from_min_max(const vec2 &min, const vec2 &max) noexcept {
        return rect(min.x, min.y, max.x - min.x, max.y - min.y);
    }
    MM_FORCE_INLINE static constexpr rect from_center_size(f32 cx, f32 cy, f32 w_, f32 h_) noexcept {
        return rect(cx - w_ * 0.5F, cy - h_ * 0.5F, w_, h_);
    }
    MM_FORCE_INLINE static constexpr rect from_center_size(const vec2 &center, const vec2 &size) noexcept {
        return rect(center.x - size.x * 0.5F, center.y - size.y * 0.5F, size.x, size.y);
    }
};

} // namespace mm_math

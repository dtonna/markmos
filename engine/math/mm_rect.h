// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include "core/mm_types.h"
#include "mm_vec2.h"
#include <cstddef>
#include <math.h>
#include <cassert>

namespace mm_math {

enum class position_mode : uint8_t {
    ABSOLUTE, // x,y = absolute position in points
    RELATIVE, // x,y = relative offset from parent (0,0 = parent's top-left, 1,1 = parent's bottom-right)
    ANCHORED, // x,y = anchor point in parent (0,0 = parent's top-left, 1,1 = parent's bottom-right), w,h = offset from anchor
};

enum class anchor : uint8_t {
    TOP_LEFT,
    TOP_CENTER,
    TOP_RIGHT,
    CENTER_LEFT,
    CENTER,
    CENTER_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_CENTER,
    BOTTOM_RIGHT,
};

struct rect {
    union {
        float4 v;
        struct alignas(16) {
            f32 x, y, w, h;
        };
        struct alignas(16) {
            vec2 m_position, m_size;
        };
    };
    // f32 x, y, w, h;

    MM_FORCE_INLINE constexpr rect() noexcept : x(0.0F), y(0.0F), w(0.0F), h(0.0F) {}
    MM_FORCE_INLINE constexpr rect(f32 x_, f32 y_, f32 w_, f32 h_) noexcept : x(x_), y(y_), w(w_), h(h_) {}
    MM_FORCE_INLINE constexpr rect(const vec2 &pos, const vec2 &size) noexcept : m_position(pos), m_size(size) {}

    MM_FORCE_INLINE constexpr rect(const rect &) noexcept = default;
    MM_FORCE_INLINE constexpr rect(rect &&) noexcept      = default;
    // Manual assign: the union's variant struct holding vec2s has a
    // non-trivial copy-assign (vec2 defines operator=), which deletes the
    // union's implicit copy/move-assign. Assigning the float view directly
    // is identical (all views share the same 16 bytes).
    MM_FORCE_INLINE constexpr rect &operator=(const rect &r) noexcept {
        x = r.x;
        y = r.y;
        w = r.w;
        h = r.h;
        return *this;
    }
    MM_FORCE_INLINE constexpr rect &operator=(rect &&r) noexcept {
        x = r.x;
        y = r.y;
        w = r.w;
        h = r.h;
        return *this;
    }

    //  (fabsf(x - target) < 1e-5f)
    MM_FORCE_INLINE constexpr bool operator==(const rect &r) const noexcept {
        return (fabsf(x - r.x) < 1e-5f && fabsf(y - r.y) < 1e-5f && fabsf(w - r.w) < 1e-5f && fabsf(h - r.h) < 1e-5f);
    }
    MM_FORCE_INLINE constexpr bool operator!=(const rect &r) const noexcept { return !(*this == r); }

    // =============================================================================
    // Properties
    // =============================================================================
    MM_FORCE_INLINE constexpr f32  left() const noexcept { return x; }
    MM_FORCE_INLINE constexpr f32  right() const noexcept { return x + w; }
    MM_FORCE_INLINE constexpr f32  top() const noexcept { return y; }
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
    MM_FORCE_INLINE constexpr void set_position(f32 x_, f32 y_) noexcept {
        x = x_;
        y = y_;
    }
    MM_FORCE_INLINE constexpr void set_position(const vec2 &pos) noexcept { m_position = pos; }
    MM_FORCE_INLINE constexpr void set_size(f32 w_, f32 h_) noexcept {
        w = w_;
        h = h_;
    }
    MM_FORCE_INLINE constexpr void set_size(const vec2 &sz) noexcept { m_size = sz; }
    MM_FORCE_INLINE constexpr void translate(f32 dx, f32 dy) noexcept {
        x += dx;
        y += dy;
    }
    MM_FORCE_INLINE constexpr void translate(const vec2 &d) noexcept { m_position += d; }
    MM_FORCE_INLINE constexpr void inflate(f32 dw, f32 dh) noexcept {
        x -= dw;
        y -= dh;
        w += dw * 2.0F;
        h += dh * 2.0F;
    }

    // =============================================================================
    // Containment & intersection
    // =============================================================================
    MM_FORCE_INLINE constexpr bool contains(f32 px, f32 py) const noexcept { return px >= x && px <= x + w && py >= y && py <= y + h; }
    MM_FORCE_INLINE constexpr bool contains(const vec2 &p) const noexcept { return contains(p.x, p.y); }
    MM_FORCE_INLINE constexpr bool contains(const rect &r) const noexcept { return r.x >= x && r.right() <= right() && r.y >= y && r.bottom() <= bottom(); }
    MM_FORCE_INLINE constexpr bool intersects(const rect &r) const noexcept { return x < r.right() && right() > r.x && y < r.bottom() && bottom() > r.y; }

    // =============================================================================
    // Set operations
    // =============================================================================
    MM_FORCE_INLINE static constexpr rect intersection(const rect &a, const rect &b) noexcept {
        f32 l   = MM_MAX(a.x, b.x);
        f32 t   = MM_MAX(a.y, b.y);
        f32 r   = MM_MIN(a.right(), b.right());
        f32 btm = MM_MIN(a.bottom(), b.bottom());
        if (l >= r || t >= btm) {
            return rect(0, 0, 0, 0);
        }
        return rect(l, t, r - l, btm - t);
    }
    MM_FORCE_INLINE static constexpr rect unite(const rect &a, const rect &b) noexcept {
        f32 l   = MM_MIN(a.x, b.x);
        f32 t   = MM_MIN(a.y, b.y);
        f32 r   = MM_MAX(a.right(), b.right());
        f32 btm = MM_MAX(a.bottom(), b.bottom());
        return rect(l, t, r - l, btm - t);
    }

MM_FORCE_INLINE static constexpr rect apply_positioning(const rect &child, const rect &parent, position_mode mode, anchor anchor_point,
                                                             const vec2 &offset = {0, 0}) noexcept {
        if (mode == position_mode::ABSOLUTE) {
            return child;
        } else if (mode == position_mode::RELATIVE) {
            f32 new_x = parent.x + child.x;
            f32 new_y = parent.y + child.y;
            return rect(new_x, new_y, child.w, child.h);
        } else if (mode == position_mode::ANCHORED) {
            vec2 anchor_pos = compute_anchor_offset(parent, anchor_point);
            return rect(anchor_pos + offset - vec2(child.w * 0.5f, child.h * 0.5f), child.size());
        }
        MM_ASSERT(false && "unrecognized position_mode");
        return child; // unreachable
    }

    MM_FORCE_INLINE static constexpr vec2 compute_anchor_offset(const rect &parent, anchor anchor_point) noexcept {
        switch (anchor_point) {
        case anchor::TOP_LEFT:
            return vec2(parent.x, parent.y);
        case anchor::TOP_CENTER:
            return vec2(parent.x + parent.w * 0.5f, parent.y);
        case anchor::TOP_RIGHT:
            return vec2(parent.x + parent.w, parent.y);
        case anchor::CENTER_LEFT:
            return vec2(parent.x, parent.y + parent.h * 0.5f);
        case anchor::CENTER:
            return parent.center();
        case anchor::CENTER_RIGHT:
            return vec2(parent.x + parent.w, parent.y + parent.h * 0.5f);
        case anchor::BOTTOM_LEFT:
            return vec2(parent.x, parent.y + parent.h);
        case anchor::BOTTOM_CENTER:
            return vec2(parent.x + parent.w * 0.5f, parent.y + parent.h);
        case anchor::BOTTOM_RIGHT:
            return vec2(parent.x + parent.w, parent.y + parent.h);
        }
        MM_ASSERT(false && "unrecognized anchor");
        return parent.center(); // unreachable
    }

    MM_FORCE_INLINE constexpr rect        with_padding(f32 p) const noexcept { return rect(x + p, y + p, w - p * 2.0F, h - p * 2.0F); }

    // =============================================================================
    // Factories
    // =============================================================================
    MM_FORCE_INLINE static constexpr rect from_min_max(f32 min_x, f32 min_y, f32 max_x, f32 max_y) noexcept {
        return rect(min_x, min_y, max_x - min_x, max_y - min_y);
    }
    MM_FORCE_INLINE static constexpr rect from_min_max(const vec2 &min, const vec2 &max) noexcept { return rect(min.x, min.y, max.x - min.x, max.y - min.y); }
    MM_FORCE_INLINE static constexpr rect from_center_size(f32 cx, f32 cy, f32 w_, f32 h_) noexcept { return rect(cx - w_ * 0.5F, cy - h_ * 0.5F, w_, h_); }
    MM_FORCE_INLINE static constexpr rect from_center_size(const vec2 &center, const vec2 &size) noexcept {
        return rect(center.x - size.x * 0.5F, center.y - size.y * 0.5F, size.x, size.y);
    }
};

} // namespace mm_math

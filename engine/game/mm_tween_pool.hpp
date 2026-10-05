// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_handle.hpp"
#include "../math/mm_math.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

static constexpr u16 MAX_TWEENS = 512;
static constexpr f32 TWEEN_DELAY_EPSILON = 0.000001f;

using EaseFunc = f32 (*)(f32);

enum class e_ease_type : u8 {
    LINEAR = 0,
    QUAD_IN,
    QUAD_OUT,
    QUAD_IN_OUT,
    CUBIC_OUT,
    CUBIC_IN,
    ELASTIC_OUT,
    BOUNCE_OUT,
    SINE_IN_OUT,
    SINE_IN,
    SINE_OUT,
    BACK_OUT,
    BACK_IN_OUT,
    EXPO_OUT,
    CIRC_OUT,
    QUINT_OUT,
    COUNT
};

namespace ease {
inline f32 linear(f32 t) noexcept { return t; }
inline f32 quad_in(f32 t) noexcept { return t * t; }
inline f32 quad_out(f32 t) noexcept { return 1.0f - (1.0f - t) * (1.0f - t); }
inline f32 quad_in_out(f32 t) noexcept { return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * 0.5f; }
inline f32 cubic_out(f32 t) noexcept { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
inline f32 cubic_in(f32 t) noexcept { return t * t * t; }
inline f32 elastic_out(f32 t) noexcept {
    if (t == 0.0f || t == 1.0f) {
        return t;
    }
    f32 p = 0.3f;
    return __builtin_powf(2.0f, -10.0f * t) * __builtin_sinf((t - p / 4.0f) * (2.0f * mm_math::MM_PI) / p) + 1.0f;
}
inline f32 bounce_out(f32 t) noexcept {
    if (t < 1.0f / 2.75f) {
        return 7.5625f * t * t;
    }
    if (t < 2.0f / 2.75f) {
        t -= 1.5f / 2.75f;
        return 7.5625f * t * t + 0.75f;
    }
    if (t < 2.5f / 2.75f) {
        t -= 2.25f / 2.75f;
        return 7.5625f * t * t + 0.9375f;
    }
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + 0.984375f;
}
inline f32 sine_in_out(f32 t) noexcept { return -(__builtin_cosf(mm_math::MM_PI * t) - 1.0f) * 0.5f; }
inline f32 back_out(f32 t) noexcept {
    f32 c1 = 1.70158f;
    f32 c3 = c1 + 1.0f;
    return 1.0f + c3 * __builtin_powf(t - 1.0f, 3.0f) + c1 * __builtin_powf(t - 1.0f, 2.0f);
}
inline f32 back_in_out(f32 t) noexcept {
    f32 c1 = 1.70158f;
    f32 c2 = c1 * 1.525f;
    return t < 0.5f ? (__builtin_powf(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
                    : (__builtin_powf(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
}
inline f32 expo_out(f32 t) noexcept { return t == 1.0f ? 1.0f : 1.0f - __builtin_powf(2.0f, -10.0f * t); }
inline f32 circ_out(f32 t) noexcept { return __builtin_sqrtf(1.0f - (t - 1.0f) * (t - 1.0f)); }
inline f32 sine_in(f32 t) noexcept { return 1.0f - __builtin_cosf(t * mm_math::MM_PI * 0.5f); }
inline f32 sine_out(f32 t) noexcept { return __builtin_sinf(t * mm_math::MM_PI * 0.5f); }
inline f32 quint_out(f32 t) noexcept {
    f32 t2 = t - 1.0f;
    return t2 * t2 * t2 * t2 * t2 + 1.0f;
}

inline f32 apply(u8 type, f32 nt, f32 s, f32 e) noexcept {
    static constexpr EaseFunc TABLE[] = {linear,      quad_in, quad_out, quad_in_out, cubic_out,   cubic_in, elastic_out, bounce_out,
                                         sine_in_out, sine_in, sine_out, back_out,    back_in_out, expo_out, circ_out,    quint_out};
    if (type < static_cast<u8>(e_ease_type::COUNT)) {
        return s + (e - s) * TABLE[type](nt);
    }
    return s + (e - s) * nt;
}
} // namespace ease

enum class e_anim_property : u8 { X = 0, Y, SCALE_X, SCALE_Y, ROTATION, ALPHA, CUSTOM };

enum class e_anim_state : u8 { STOPPED, PLAYING, PAUSED, FINISHED };

enum class e_loop_mode : u8 {
    NONE      = 0,
    REPEAT    = 1,
    PING_PONG = 2,
};

struct alignas(64) TweenPool {
    alignas(64) f32 start[MAX_TWEENS];
    alignas(64) f32 end[MAX_TWEENS];
    alignas(64) f32 t[MAX_TWEENS];
    alignas(64) f32 duration[MAX_TWEENS];
    alignas(64) f32 *target[MAX_TWEENS];
    alignas(64) u32 target_id[MAX_TWEENS];
    alignas(64) u8 property[MAX_TWEENS];
    alignas(64) u8 use_id[MAX_TWEENS];
    alignas(64) u8 ease[MAX_TWEENS];
    alignas(64) u8 active[MAX_TWEENS];
    alignas(64) u16 gen[MAX_TWEENS];
    alignas(64) u8 loop_mode[MAX_TWEENS];
    alignas(64) u8 repeat_max[MAX_TWEENS];
    alignas(64) u8 repeat_cur[MAX_TWEENS];

    i16  next[MAX_TWEENS];
    i16  free_next[MAX_TWEENS];
    i16  free_head;
    f32    delay[MAX_TWEENS];
    u16 count;

    TweenPool() { reset(); }

    void reset() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
        free_head = 0;
        for (u16 i = 0; i < MAX_TWEENS; ++i) {
            free_next[i] = static_cast<i16>((i + 1 < MAX_TWEENS) ? i + 1 : -1);
        }
    }

    using OnComplete = void (*)(u32 target_id, u8 prop, void *userdata);
    OnComplete  on_complete[MAX_TWEENS];
    void       *userdata[MAX_TWEENS];

    i16 alloc_slot() noexcept {
        if (free_head < 0) {
            return -1;
        }
        u16 idx = static_cast<u16>(free_head);
        free_head = free_next[idx];
        free_next[idx] = -1;
        active[idx] = 1;
        ++gen[idx];
        ++count;
        return static_cast<i16>(idx);
    }

    void release_slot(u16 idx) noexcept {
        if (idx >= MAX_TWEENS || !active[idx]) {
            return;
        }
        active[idx] = 0;
        free_next[idx] = free_head;
        free_head = static_cast<i16>(idx);
        --count;
    }

    TweenHandle spawn(f32 *target_ptr, f32 start_val, f32 end_val, f32 dur, e_ease_type e, u8 lm = 0, u8 repeat = 0) noexcept {
        i16 slot = alloc_slot();
        if (slot < 0) {
            return TweenHandle::invalid();
        }
        u16 idx = static_cast<u16>(slot);
        start[idx]    = start_val;
        end[idx]      = end_val;
        t[idx]        = 0.0f;
        duration[idx] = dur;
        target[idx]   = target_ptr;
        target_id[idx] = 0;
        property[idx] = static_cast<u8>(e_anim_property::X);
        use_id[idx]   = 0;
        ease[idx]     = static_cast<u8>(e);
        loop_mode[idx] = lm;
        repeat_max[idx] = repeat;
        repeat_cur[idx] = 0;
        next[idx]     = -1;
        delay[idx]    = 0.0f;
        on_complete[idx] = nullptr;
        userdata[idx] = nullptr;
        return TweenHandle{SlotHandle{idx, gen[idx], {0}}};
    }

    TweenHandle spawn_id(u32 id, e_anim_property prop, f32 start_val, f32 end_val, f32 dur, e_ease_type e, OnComplete cb = nullptr,
                         void *ud = nullptr, e_loop_mode lm = e_loop_mode::NONE, u8 repeat = 0, f32 delay_seconds = 0.0f) noexcept {
        i16 slot = alloc_slot();
        if (slot < 0) {
            return TweenHandle::invalid();
        }
        u16 idx     = static_cast<u16>(slot);
        start[idx]       = start_val;
        end[idx]         = end_val;
        t[idx]           = 0.0f;
        duration[idx]    = dur;
        target[idx]      = nullptr;
        target_id[idx]   = id;
        property[idx]    = static_cast<u8>(prop);
        use_id[idx]      = 1;
        ease[idx]        = static_cast<u8>(e);
        loop_mode[idx]   = static_cast<u8>(lm);
        repeat_max[idx]  = repeat;
        repeat_cur[idx]  = 0;
        next[idx]        = -1;
        delay[idx]       = delay_seconds;
        on_complete[idx] = cb;
        userdata[idx]    = ud;
        return TweenHandle{SlotHandle{idx, gen[idx], {0}}};
    }

    void chain(TweenHandle from, TweenHandle to) noexcept {
        if (from.handle.id < MAX_TWEENS && to.handle.id < MAX_TWEENS && active[from.handle.id] && from.handle.gen == gen[from.handle.id] &&
            to.handle.gen == gen[to.handle.id]) {
            next[from.handle.id] = static_cast<i16>(to.handle.id);
        }
    }

    void update(f32 dt) noexcept {
        for (u16 i = 0; i < MAX_TWEENS; ++i) {
            if (!active[i]) {
                ++i;
                continue;
            }
            f32 remaining_dt = dt;
            if (delay[i] > TWEEN_DELAY_EPSILON) {
                if (delay[i] > remaining_dt + TWEEN_DELAY_EPSILON) {
                    delay[i] -= remaining_dt;
                    ++i;
                    continue;
                }
                delay[i] = 0.0f;
            }

            t[i] += remaining_dt;
            bool  done = (duration[i] <= 0.0f || t[i] >= duration[i]);
            f32 nt   = done ? 1.0f : t[i] / duration[i];

            if (target[i]) {
                *target[i] = ease::apply(ease[i], nt, start[i], end[i]);
            }

            if (!done) {
                ++i;
                continue;
            }

            auto lm       = static_cast<e_loop_mode>(loop_mode[i]);
            i16 next_i = next[i];

            if (lm == e_loop_mode::NONE) {
                if (next_i >= 0 && next_i < MAX_TWEENS && !active[next_i]) {
                    active[next_i] = 1;
                    ++count;
                }
                release_slot(i);
            } else if (lm == e_loop_mode::REPEAT) {
                t[i] = 0.0f;
                ++repeat_cur[i];
                if (repeat_max[i] > 0 && repeat_cur[i] >= repeat_max[i]) {
                    release_slot(i);
                } else {
                    ++i;
                }
            } else if (lm == e_loop_mode::PING_PONG) {
                t[i] = 0.0f;
                std::swap(start[i], end[i]);
                ++repeat_cur[i];
                if (repeat_max[i] > 0 && repeat_cur[i] >= repeat_max[i]) {
                    release_slot(i);
                } else {
                    ++i;
                }
            }
        }
    }

    void update(f32 dt, auto &&apply_fn) noexcept {
        for (u16 i = 0; i < MAX_TWEENS;) {
            if (!active[i]) {
                ++i;
                continue;
            }
            f32 remaining_dt = dt;
            if (delay[i] > TWEEN_DELAY_EPSILON) {
                if (delay[i] > remaining_dt + TWEEN_DELAY_EPSILON) {
                    delay[i] -= remaining_dt;
                    ++i;
                    continue;
                }
                delay[i] = 0.0f;
            }

            t[i]       += remaining_dt;
            bool  done  = (duration[i] <= 0.0f || t[i] >= duration[i]);
            f32 nt    = done ? 1.0f : t[i] / duration[i];
            f32 val   = ease::apply(ease[i], nt, start[i], end[i]);

            if (use_id[i]) {
                apply_fn(target_id[i], static_cast<e_anim_property>(property[i]), val);
            } else if (target[i]) {
                *target[i] = val;
            }

            if (!done) {
                ++i;
                continue;
            }

            auto lm       = static_cast<e_loop_mode>(loop_mode[i]);
            i16 next_i = next[i];
            OnComplete cb = on_complete[i];
            void *ud      = userdata[i];
            u32 tid  = target_id[i];
            u8 prop  = property[i];

            if (lm == e_loop_mode::NONE) {
                if (next_i >= 0 && next_i < MAX_TWEENS && !active[next_i]) {
                    active[next_i] = 1;
                    ++count;
                }
                release_slot(i);
                if (cb) {
                    cb(tid, prop, ud);
                }
            } else if (lm == e_loop_mode::REPEAT) {
                t[i] = 0.0f;
                ++repeat_cur[i];
                if (repeat_max[i] > 0 && repeat_cur[i] >= repeat_max[i]) {
                    if (next_i >= 0 && next_i < MAX_TWEENS && !active[next_i]) {
                        active[next_i] = 1;
                        ++count;
                    }
                    release_slot(i);
                    if (cb) {
                        cb(tid, prop, ud);
                    }
                } else {
                    ++i;
                }
            } else if (lm == e_loop_mode::PING_PONG) {
                t[i] = 0.0f;
                std::swap(start[i], end[i]);
                ++repeat_cur[i];
                if (repeat_max[i] > 0 && repeat_cur[i] >= repeat_max[i]) {
                    if (next_i >= 0 && next_i < MAX_TWEENS && !active[next_i]) {
                        active[next_i] = 1;
                        ++count;
                    }
                    release_slot(i);
                    if (cb) {
                        cb(tid, prop, ud);
                    }
                } else {
                    ++i;
                }
            }
        }
    }

    void cancel(TweenHandle h) noexcept {
        if (h.handle.id < MAX_TWEENS && h.handle.gen == gen[h.handle.id] && active[h.handle.id]) {
            release_slot(h.handle.id);
        }
    }

    void cancel_by_id(u32 id) noexcept {
        for (u16 i = 0; i < MAX_TWEENS;) {
            if (active[i] && use_id[i] && target_id[i] == id) {
                release_slot(i);
            } else {
                ++i;
            }
        }
    }
};

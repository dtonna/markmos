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

static constexpr uint16_t MAX_TWEENS = 512;
static constexpr float TWEEN_DELAY_EPSILON = 0.000001f;

using EaseFunc = float (*)(float);

enum class e_ease_type : uint8_t {
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
inline float linear(float t) noexcept { return t; }
inline float quad_in(float t) noexcept { return t * t; }
inline float quad_out(float t) noexcept { return 1.0f - (1.0f - t) * (1.0f - t); }
inline float quad_in_out(float t) noexcept { return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * 0.5f; }
inline float cubic_out(float t) noexcept { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
inline float cubic_in(float t) noexcept { return t * t * t; }
inline float elastic_out(float t) noexcept {
    if (t == 0.0f || t == 1.0f) {
        return t;
    }
    float p = 0.3f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t - p / 4.0f) * (2.0f * mm_math::MM_PI) / p) + 1.0f;
}
inline float bounce_out(float t) noexcept {
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
inline float sine_in_out(float t) noexcept { return -(std::cos(mm_math::MM_PI * t) - 1.0f) * 0.5f; }
inline float back_out(float t) noexcept {
    float c1 = 1.70158f;
    float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}
inline float back_in_out(float t) noexcept {
    float c1 = 1.70158f;
    float c2 = c1 * 1.525f;
    return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
                    : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
}
inline float expo_out(float t) noexcept { return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
inline float circ_out(float t) noexcept { return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f)); }
inline float sine_in(float t) noexcept { return 1.0f - std::cos(t * mm_math::MM_PI * 0.5f); }
inline float sine_out(float t) noexcept { return std::sin(t * mm_math::MM_PI * 0.5f); }
inline float quint_out(float t) noexcept {
    float t2 = t - 1.0f;
    return t2 * t2 * t2 * t2 * t2 + 1.0f;
}

inline float apply(uint8_t type, float nt, float s, float e) noexcept {
    static constexpr EaseFunc TABLE[] = {linear,      quad_in, quad_out, quad_in_out, cubic_out,   cubic_in, elastic_out, bounce_out,
                                         sine_in_out, sine_in, sine_out, back_out,    back_in_out, expo_out, circ_out,    quint_out};
    if (type < static_cast<uint8_t>(e_ease_type::COUNT)) {
        return s + (e - s) * TABLE[type](nt);
    }
    return s + (e - s) * nt;
}
} // namespace ease

enum class e_anim_property : uint8_t { X = 0, Y, SCALE_X, SCALE_Y, ROTATION, ALPHA, CUSTOM };

enum class e_anim_state : uint8_t { STOPPED, PLAYING, PAUSED, FINISHED };

enum class e_loop_mode : uint8_t {
    NONE      = 0,
    REPEAT    = 1,
    PING_PONG = 2,
};

struct alignas(64) TweenPool {
    alignas(64) float start[MAX_TWEENS];
    alignas(64) float end[MAX_TWEENS];
    alignas(64) float t[MAX_TWEENS];
    alignas(64) float duration[MAX_TWEENS];
    alignas(64) float *target[MAX_TWEENS];
    alignas(64) uint32_t target_id[MAX_TWEENS];
    alignas(64) uint8_t property[MAX_TWEENS];
    alignas(64) uint8_t use_id[MAX_TWEENS];
    alignas(64) uint8_t ease[MAX_TWEENS];
    alignas(64) uint8_t active[MAX_TWEENS];
    alignas(64) uint16_t gen[MAX_TWEENS];
    alignas(64) uint8_t loop_mode[MAX_TWEENS];
    alignas(64) uint8_t repeat_max[MAX_TWEENS];
    alignas(64) uint8_t repeat_cur[MAX_TWEENS];

    int16_t  next[MAX_TWEENS];
    int16_t  free_next[MAX_TWEENS];
    int16_t  free_head;
    float    delay[MAX_TWEENS];
    uint16_t count;

    TweenPool() { reset(); }

    void reset() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
        free_head = 0;
        for (uint16_t i = 0; i < MAX_TWEENS; ++i) {
            free_next[i] = static_cast<int16_t>((i + 1 < MAX_TWEENS) ? i + 1 : -1);
        }
    }

    using OnComplete = void (*)(uint32_t target_id, uint8_t prop, void *userdata);
    OnComplete  on_complete[MAX_TWEENS];
    void       *userdata[MAX_TWEENS];

    int16_t alloc_slot() noexcept {
        if (free_head < 0) {
            return -1;
        }
        uint16_t idx = static_cast<uint16_t>(free_head);
        free_head = free_next[idx];
        free_next[idx] = -1;
        active[idx] = 1;
        ++gen[idx];
        ++count;
        return static_cast<int16_t>(idx);
    }

    void release_slot(uint16_t idx) noexcept {
        if (idx >= MAX_TWEENS || !active[idx]) {
            return;
        }
        active[idx] = 0;
        free_next[idx] = free_head;
        free_head = static_cast<int16_t>(idx);
        --count;
    }

    TweenHandle spawn(float *target_ptr, float start_val, float end_val, float dur, e_ease_type e, uint8_t lm = 0, uint8_t repeat = 0) noexcept {
        int16_t slot = alloc_slot();
        if (slot < 0) {
            return TweenHandle::invalid();
        }
        uint16_t idx = static_cast<uint16_t>(slot);
        start[idx]    = start_val;
        end[idx]      = end_val;
        t[idx]        = 0.0f;
        duration[idx] = dur;
        target[idx]   = target_ptr;
        target_id[idx] = 0;
        property[idx] = static_cast<uint8_t>(e_anim_property::X);
        use_id[idx]   = 0;
        ease[idx]     = static_cast<uint8_t>(e);
        loop_mode[idx] = lm;
        repeat_max[idx] = repeat;
        repeat_cur[idx] = 0;
        next[idx]     = -1;
        delay[idx]    = 0.0f;
        on_complete[idx] = nullptr;
        userdata[idx] = nullptr;
        return TweenHandle{SlotHandle{idx, gen[idx], {0}}};
    }

    TweenHandle spawn_id(uint32_t id, e_anim_property prop, float start_val, float end_val, float dur, e_ease_type e, OnComplete cb = nullptr,
                         void *ud = nullptr, e_loop_mode lm = e_loop_mode::NONE, uint8_t repeat = 0, float delay_seconds = 0.0f) noexcept {
        int16_t slot = alloc_slot();
        if (slot < 0) {
            return TweenHandle::invalid();
        }
        uint16_t idx     = static_cast<uint16_t>(slot);
        start[idx]       = start_val;
        end[idx]         = end_val;
        t[idx]           = 0.0f;
        duration[idx]    = dur;
        target[idx]      = nullptr;
        target_id[idx]   = id;
        property[idx]    = static_cast<uint8_t>(prop);
        use_id[idx]      = 1;
        ease[idx]        = static_cast<uint8_t>(e);
        loop_mode[idx]   = static_cast<uint8_t>(lm);
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
            next[from.handle.id] = static_cast<int16_t>(to.handle.id);
        }
    }

    void update(float dt) noexcept {
        for (uint16_t i = 0; i < MAX_TWEENS; ++i) {
            if (!active[i]) {
                ++i;
                continue;
            }
            float remaining_dt = dt;
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
            float nt   = done ? 1.0f : t[i] / duration[i];

            if (target[i]) {
                *target[i] = ease::apply(ease[i], nt, start[i], end[i]);
            }

            if (!done) {
                ++i;
                continue;
            }

            auto lm       = static_cast<e_loop_mode>(loop_mode[i]);
            int16_t next_i = next[i];

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

    void update(float dt, auto &&apply_fn) noexcept {
        for (uint16_t i = 0; i < MAX_TWEENS;) {
            if (!active[i]) {
                ++i;
                continue;
            }
            float remaining_dt = dt;
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
            float nt    = done ? 1.0f : t[i] / duration[i];
            float val   = ease::apply(ease[i], nt, start[i], end[i]);

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
            int16_t next_i = next[i];
            OnComplete cb = on_complete[i];
            void *ud      = userdata[i];
            uint32_t tid  = target_id[i];
            uint8_t prop  = property[i];

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

    void cancel_by_id(uint32_t id) noexcept {
        for (uint16_t i = 0; i < MAX_TWEENS;) {
            if (active[i] && use_id[i] && target_id[i] == id) {
                release_slot(i);
            } else {
                ++i;
            }
        }
    }
};

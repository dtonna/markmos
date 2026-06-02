// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_handle.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include "../math/mm_math.h"

static constexpr uint16_t MAX_TWEENS = 512;

using EaseFunc = float(*)(float);

enum class EaseType : uint8_t {
    Linear = 0, QuadIn, QuadOut, QuadInOut,
    CubicOut, CubicIn,
    ElasticOut, BounceOut, SineInOut, SineIn, SineOut,
    BackOut, BackInOut,
    ExpoOut, CircOut, QuintOut,
    COUNT
};

namespace ease {
    inline float linear(float t) noexcept { return t; }
    inline float quad_in(float t) noexcept { return t * t; }
    inline float quad_out(float t) noexcept { return 1.0f - (1.0f - t) * (1.0f - t); }
    inline float quad_in_out(float t) noexcept {
        return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * 0.5f;
    }
    inline float cubic_out(float t) noexcept { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
    inline float cubic_in(float t) noexcept { return t * t * t; }
    inline float elastic_out(float t) noexcept {
        if (t == 0.0f || t == 1.0f) return t;
        float p = 0.3f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t - p / 4.0f) * (2.0f * mm_math::MM_PI) / p) + 1.0f;
    }
    inline float bounce_out(float t) noexcept {
        if (t < 1.0f / 2.75f) return 7.5625f * t * t;
        if (t < 2.0f / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
        if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
        t -= 2.625f / 2.75f; return 7.5625f * t * t + 0.984375f;
    }
    inline float sine_in_out(float t) noexcept {
        return -(std::cos(mm_math::MM_PI * t) - 1.0f) * 0.5f;
    }
    inline float back_out(float t) noexcept {
        float c1 = 1.70158f;
        float c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
    }
    inline float back_in_out(float t) noexcept {
        float c1 = 1.70158f;
        float c2 = c1 * 1.525f;
        return t < 0.5f
            ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
            : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
    }
    inline float expo_out(float t) noexcept {
        return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    }
    inline float circ_out(float t) noexcept {
        return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
    }
    inline float sine_in(float t) noexcept {
        return 1.0f - std::cos(t * mm_math::MM_PI * 0.5f);
    }
    inline float sine_out(float t) noexcept {
        return std::sin(t * mm_math::MM_PI * 0.5f);
    }
    inline float quint_out(float t) noexcept {
        float t2 = t - 1.0f;
        return t2 * t2 * t2 * t2 * t2 + 1.0f;
    }

    inline float apply(uint8_t type, float nt, float s, float e) noexcept {
        static constexpr EaseFunc TABLE[] = {
            linear, quad_in, quad_out, quad_in_out, cubic_out, cubic_in,
            elastic_out, bounce_out, sine_in_out, sine_in, sine_out,
            back_out, back_in_out,
            expo_out, circ_out, quint_out
        };
        if (type < static_cast<uint8_t>(EaseType::COUNT)) {
            return s + (e - s) * TABLE[type](nt);
        }
        return s + (e - s) * nt;
    }
}

struct alignas(64) TweenPool {
    alignas(64) float    start    [MAX_TWEENS];
    alignas(64) float    end      [MAX_TWEENS];
    alignas(64) float    t        [MAX_TWEENS];
    alignas(64) float    duration [MAX_TWEENS];
    alignas(64) float*   target   [MAX_TWEENS];
    alignas(64) uint8_t  ease     [MAX_TWEENS];
    alignas(64) uint8_t  active   [MAX_TWEENS];
    uint8_t  loop     [MAX_TWEENS];
    int16_t  next     [MAX_TWEENS];
    uint8_t  delay    [MAX_TWEENS];
    uint16_t count;

    TweenPool() { reset(); }

    void reset() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
    }

    TweenHandle spawn(float* target_ptr, float start_val, float end_val,
                      float dur, EaseType e, uint8_t loop_mode = 0) noexcept {
        if (count >= MAX_TWEENS) return TweenHandle::invalid();
        uint16_t idx = count++;
        start[idx]    = start_val;
        end[idx]      = end_val;
        t[idx]        = 0.0f;
        duration[idx] = dur;
        target[idx]   = target_ptr;
        ease[idx]     = static_cast<uint8_t>(e);
        active[idx]   = 1;
        loop[idx]     = loop_mode;
        next[idx]     = -1;
        delay[idx]    = 0;
        return TweenHandle{SlotHandle{idx, 0, 0}};
    }

    void chain(TweenHandle from, TweenHandle to) noexcept {
        if (from.handle.id < MAX_TWEENS && to.handle.id < MAX_TWEENS) {
            next[from.handle.id] = static_cast<int16_t>(to.handle.id);
        }
    }

    void update(float dt) noexcept {
        for (uint16_t i = 0; i < count; ++i) {
            if (!active[i]) continue;
            if (delay[i] > 0) { --delay[i]; continue; }

            t[i] += dt;
            if (t[i] >= duration[i]) {
                if (loop[i] == 1) {
                    t[i] = 0.0f;
                } else if (loop[i] == 2) {
                    t[i] = 0.0f;
                    std::swap(start[i], end[i]);
                } else {
                    if (target[i]) *target[i] = ease::apply(ease[i], 1.0f, start[i], end[i]);
                    active[i] = 0;
                    if (next[i] >= 0 && next[i] < MAX_TWEENS) {
                        active[next[i]] = 1;
                    }
                    continue;
                }
            }
            if (target[i]) {
                *target[i] = ease::apply(ease[i], t[i] / duration[i], start[i], end[i]);
            }
        }
    }

    void cancel(TweenHandle h) noexcept {
        if (h.handle.id < count && active[h.handle.id]) active[h.handle.id] = 0;
    }

    void deswap(uint16_t idx) noexcept {
        if (idx >= count) return;
        uint16_t last = count - 1;
        if (idx != last) {
            start[idx]    = start[last];    end[idx]      = end[last];
            t[idx]        = t[last];        duration[idx] = duration[last];
            target[idx]   = target[last];   ease[idx]     = ease[last];
            active[idx]   = active[last];   loop[idx]     = loop[last];
            next[idx]     = next[last];     delay[idx]    = delay[last];
        }
        --count;
    }
};

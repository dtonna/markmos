#pragma once

#include <concepts>
#include <cstdint>
#include <cstdlib>

#include "../core/mm_handle.hpp"
#include "../game/mm_tween_pool.hpp"

namespace mm {

// C++20 Concept สำหรับตรวจสอบว่า Object นั้นๆ รองรับการปรับค่า Property ของ Engine หรือไม่
// วิธีนี้ทำให้เราไม่ต้องใช้ Virtual Function (No vtable overhead)
template <typename T>
concept Animatable = requires(T &obj, size_t id, e_anim_property prop, float value) {
    { obj.update_property(id, prop, value) } -> std::same_as<void>;
};

// ─── Timeline — no heap, fixed size ──────────────────────────
struct Timeline {
    static constexpr uint8_t MAX_TWEENS = 16;

    struct Entry {
        uint32_t        target_id;
        e_anim_property property;
        float           from_value;
        float           to_value;
        float           duration;
        float           delay;
        e_ease_type     ease;
        e_loop_mode     loop;
        uint8_t         repeat;
    };

    Entry   entries[MAX_TWEENS]{};
    uint8_t count = 0;

    void    add_tween(uint32_t id, e_anim_property prop, float from, float to, float dur, e_ease_type e = e_ease_type::CUBIC_OUT, float delay = 0.0f,
                      e_loop_mode loop = e_loop_mode::NONE, uint8_t repeat = 0) noexcept {
        if (count >= MAX_TWEENS) {
            return;
        }
        entries[count++] = {id, prop, from, to, dur, delay, e, loop, repeat};
    }
};

// ─── AnimationPlayer — wraps TweenPool ───────────────────────
struct AnimationPlayer {
    TweenPool *pool = nullptr; // inject — ไม่ own

    explicit AnimationPlayer(TweenPool &p) noexcept : pool(&p) {}

    // submit Timeline entries เข้า TweenPool
    void play(const Timeline &timeline, TweenPool::OnComplete on_done = nullptr, void *userdata = nullptr) noexcept {
        if (!pool) {
            return;
        }

        for (uint8_t i = 0; i < timeline.count; ++i) {
            auto &e       = timeline.entries[i];
            bool  is_last = (i == timeline.count - 1);
            pool->spawn_id(e.target_id, e.property, e.from_value, e.to_value, e.duration, e.ease, is_last ? on_done : nullptr, is_last ? userdata : nullptr,
                           e.loop, e.repeat, e.delay);
        }
    }

    using PropertyUpdater = void (*)(uint32_t, e_anim_property, float, void *);

    void tick(float dt, PropertyUpdater updater, void *userdata) noexcept {
        if (!pool) {
            return;
        }
        pool->update(dt, [&](uint32_t id, e_anim_property prop, float val) { updater(id, prop, val, userdata); });
    }
    // tick — delegate ไปที่ TweenPool โดยตรง
    // template <Animatable GameRegistry> void tick(float dt, GameRegistry &registry) noexcept {
    //     if (!pool) {
    //         return;
    //     }
    //     pool->update(dt, [&](uint32_t id, AnimProperty prop, float val) { registry.update_property(id, prop, val); });
    // }

    void cancel(uint32_t target_id) noexcept {
        if (pool) {
            pool->cancel_by_id(target_id);
        }
    }
};

} // namespace mm

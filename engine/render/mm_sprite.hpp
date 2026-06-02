// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../rhi/mm_rhi_concept.hpp"
#include "../core/mm_handle.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>

// ─── Constants ───────────────────────────────────────────────────
static constexpr uint16_t SPRITE_MAX_NAME   = 32;
static constexpr uint16_t SPRITE_MAX_FRAMES = 1024;
static constexpr uint16_t SPRITE_MAX_ANIMS  = 128;
static constexpr uint16_t SPRITE_MAX_ANIM_FRAMES = 256;

// ─── SpriteFrame ─────────────────────────────────────────────────
// A single sub-image within a texture atlas, or a full texture.
struct SpriteFrame {
    TextureHandle texture;
    uint16_t      x, y;         // pixel offset in texture
    uint16_t      w, h;         // pixel size
    uint16_t      tex_w, tex_h; // texture dimensions (for UV calc)
    float         pivot_x, pivot_y; // 0-1 normalized pivot
    uint8_t       sfx_id;       // registered sound ID, SFX_NO_SOUND = none
    uint8_t       _pad[3];

    bool is_valid() const noexcept { return texture.is_valid() && w > 0 && h > 0; }
};

// ─── AnimFrame ───────────────────────────────────────────────────
struct AnimFrame {
    uint16_t frame_index;
    float    duration;  // seconds
};

// ─── SpriteAnim ──────────────────────────────────────────────────
struct SpriteAnim {
    char       name[SPRITE_MAX_NAME];
    AnimFrame  frames[SPRITE_MAX_ANIM_FRAMES];
    uint16_t   frame_count;
    bool       loop;
};

// ─── SpriteAtlas ─────────────────────────────────────────────────
// Collection of sprite frames from one texture, with optional animations.
struct SpriteAtlas {
    TextureHandle texture;
    uint16_t      tex_w, tex_h;

    SpriteFrame frames[SPRITE_MAX_FRAMES];
    char        frame_names[SPRITE_MAX_FRAMES][SPRITE_MAX_NAME];
    uint16_t    frame_count;

    SpriteAnim  anims[SPRITE_MAX_ANIMS];
    uint16_t    anim_count;

    void init() noexcept {
        frame_count = 0;
        anim_count  = 0;
    }

    // Find sprite frame by name (linear scan — fast for typical atlas sizes)
    const SpriteFrame* find(const char* name) const noexcept {
        for (uint16_t i = 0; i < frame_count; ++i) {
            if (std::strcmp(frame_names[i], name) == 0) {
                return &frames[i];
            }
        }
        return nullptr;
    }

    // Find animation by name
    const SpriteAnim* find_anim(const char* name) const noexcept {
        for (uint16_t i = 0; i < anim_count; ++i) {
            if (std::strcmp(anims[i].name, name) == 0) {
                return &anims[i];
            }
        }
        return nullptr;
    }

    // Get current frame of an animation based on time
    const SpriteFrame* get_anim_frame(const SpriteAnim& anim, float time) const noexcept {
        if (anim.frame_count == 0) return nullptr;

        float t = time;
        float total = 0.0f;
        for (uint16_t i = 0; i < anim.frame_count; ++i) {
            total += anim.frames[i].duration;
        }

        if (total <= 0.0f) return &frames[anim.frames[0].frame_index];

        if (anim.loop) {
            t = std::fmod(time, total);
        } else if (time >= total) {
            return &frames[anim.frames[anim.frame_count - 1].frame_index];
        }

        float accum = 0.0f;
        for (uint16_t i = 0; i < anim.frame_count; ++i) {
            accum += anim.frames[i].duration;
            if (t < accum) {
                return &frames[anim.frames[i].frame_index];
            }
        }

        return &frames[anim.frames[anim.frame_count - 1].frame_index];
    }
};

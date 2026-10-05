// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include "core/mm_types.h"
#include <cstddef>
#include <cstring>
#include <cmath>

// Text Popup Pool — floating score/combo text, no alloc in game loop
// Cache reason:
//   - SoA layout: px[], py[], alpha[], scale[], text_buf[][] — sequential
//   - Update loop reads all arrays linearly, prefetch-friendly
//   - Fixed text_buf[16] per popup avoids string alloc
// Design:
//   - MAX_POPUP = 64 (enough for score floats + combo messages)
//   - Text buffer: 15 UTF-8 bytes + null (short strings only)
//   - Swap-with-last despawn

static constexpr u16 MAX_POPUP = 64;

struct TextPopup {
    f32    px, py;
    f32    vy;           // f32-up velocity
    f32    alpha;        // 1.0 → 0.0 fade
    f32    scale;        // animate scale pop
    u8  text[16];     // max 15 char + null
    u8  text_len;
    u32 color;        // RGBA tint
    u8  active;
};

struct alignas(64) TextPopupPool {
    // SoA layout
    alignas(64) f32    px     [MAX_POPUP];
    alignas(64) f32    py     [MAX_POPUP];
    alignas(64) f32    vy     [MAX_POPUP];
    alignas(64) f32    alpha  [MAX_POPUP];
    alignas(64) f32    scale  [MAX_POPUP];
    alignas(64) u8  text   [MAX_POPUP][16];
    alignas(64) u8  text_len[MAX_POPUP];
    alignas(64) u32 color  [MAX_POPUP];
    alignas(64) u8  active [MAX_POPUP];

    u16 count;

    TextPopupPool() { reset(); }

    void reset() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
    }

    // Spawn a score popup — O(1), no alloc
    void spawn(f32 x, f32 y, const char* str, u8 len,
               u32 col, f32 sc = 1.0f) noexcept {
        if (count >= MAX_POPUP || len > 15) return;

        px[count]        = x;
        py[count]        = y;
        vy[count]        = -80.0f;  // f32 up
        alpha[count]     = 1.0f;
        scale[count]     = sc;
        color[count]     = col;
        text_len[count]  = len;
        active[count]    = 1;

        memcpy(text[count], str, len);
        text[count][len] = 0;

        ++count;
    }

    // Spawn with formatted score
    void spawn_score(f32 x, f32 y, u32 score, u32 col) noexcept {
        // Convert score to ASCII — small buffer on stack
        char buf[16];
        u8 len = 0;
        u32 s = score;
        do {
            buf[len++] = '0' + static_cast<char>(s % 10);
            s /= 10;
        } while (s > 0);
        // Reverse
        for (u8 i = 0; i < len / 2; ++i) {
            char tmp = buf[i];
            buf[i] = buf[len - 1 - i];
            buf[len - 1 - i] = tmp;
        }
        spawn(x, y, buf, len, col);
    }

    // Spawn combo text
    void spawn_combo(f32 x, f32 y, u8 combo, u32 col) noexcept {
        // Format: "COMBO x3"
        char buf[16] = "COMBO x";
        u8 len = 7;
        if (combo >= 10) {
            buf[len++] = '0' + (combo / 10);
        }
        buf[len++] = '0' + (combo % 10);
        spawn(x, y, buf, len, col, 1.5f);
    }

    // Update all popups
    void update(f32 dt) noexcept {
        for (u16 i = 0; i < count; ++i) {
            if (!active[i]) continue;

            // Decelerate upward: fast start, slow fade out
            py[i] += vy[i] * dt * (0.3f + 0.7f * alpha[i]);
            alpha[i] -= dt * 1.25f;
            scale[i] *= 0.98f;

            if (alpha[i] <= 0.0f) {
                deswap(i);
                --i;
            }
        }
    }

    void deswap(u16 idx) noexcept {
        if (idx >= count) return;
        u16 last = count - 1;
        if (idx != last) {
            px[idx]        = px[last];
            py[idx]        = py[last];
            vy[idx]        = vy[last];
            alpha[idx]     = alpha[last];
            scale[idx]     = scale[last];
            text_len[idx]  = text_len[last];
            color[idx]     = color[last];
            active[idx]    = active[last];
            memcpy(text[idx], text[last], 16);
        }
        --count;
    }
};

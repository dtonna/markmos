#pragma once
#include <cstdint>
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

static constexpr uint16_t MAX_POPUP = 64;

struct TextPopup {
    float    px, py;
    float    vy;           // float-up velocity
    float    alpha;        // 1.0 → 0.0 fade
    float    scale;        // animate scale pop
    uint8_t  text[16];     // max 15 char + null
    uint8_t  text_len;
    uint32_t color;        // RGBA tint
    uint8_t  active;
};

struct alignas(64) TextPopupPool {
    // SoA layout
    alignas(64) float    px     [MAX_POPUP];
    alignas(64) float    py     [MAX_POPUP];
    alignas(64) float    vy     [MAX_POPUP];
    alignas(64) float    alpha  [MAX_POPUP];
    alignas(64) float    scale  [MAX_POPUP];
    alignas(64) uint8_t  text   [MAX_POPUP][16];
    alignas(64) uint8_t  text_len[MAX_POPUP];
    alignas(64) uint32_t color  [MAX_POPUP];
    alignas(64) uint8_t  active [MAX_POPUP];

    uint16_t count;

    TextPopupPool() { reset(); }

    void reset() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
    }

    // Spawn a score popup — O(1), no alloc
    void spawn(float x, float y, const char* str, uint8_t len,
               uint32_t col, float sc = 1.0f) noexcept {
        if (count >= MAX_POPUP || len > 15) return;

        px[count]        = x;
        py[count]        = y;
        vy[count]        = -80.0f;  // float up
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
    void spawn_score(float x, float y, uint32_t score, uint32_t col) noexcept {
        // Convert score to ASCII — small buffer on stack
        char buf[16];
        uint8_t len = 0;
        uint32_t s = score;
        do {
            buf[len++] = '0' + static_cast<char>(s % 10);
            s /= 10;
        } while (s > 0);
        // Reverse
        for (uint8_t i = 0; i < len / 2; ++i) {
            char tmp = buf[i];
            buf[i] = buf[len - 1 - i];
            buf[len - 1 - i] = tmp;
        }
        spawn(x, y, buf, len, col);
    }

    // Spawn combo text
    void spawn_combo(float x, float y, uint8_t combo, uint32_t col) noexcept {
        // Format: "COMBO x3"
        char buf[16] = "COMBO x";
        uint8_t len = 7;
        if (combo >= 10) {
            buf[len++] = '0' + (combo / 10);
        }
        buf[len++] = '0' + (combo % 10);
        spawn(x, y, buf, len, col, 1.5f);
    }

    // Update all popups
    void update(float dt) noexcept {
        for (uint16_t i = 0; i < count; ++i) {
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

    void deswap(uint16_t idx) noexcept {
        if (idx >= count) return;
        uint16_t last = count - 1;
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

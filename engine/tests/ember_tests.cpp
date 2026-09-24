// Ember pool tests — lifecycle, respawn, color ramp, glow texture.
// Plain main() + assert(), no framework (matches layout_tests).
#include "../game/mm_ember_pool.hpp"
#include <cassert>
#include <cmath>

static bool Near(float a, float b, float eps = 1e-4f) noexcept {
    return std::fabs(a - b) <= eps;
}

int main() {
    // ─── init scatters + staggers ───
    {
        EmberPool p;
        p.init(100.0f, 200.0f, 24, 999u);
        assert(p.count == 24);
        assert(Near(p.anchor_x, 100.0f) && Near(p.anchor_y, 200.0f));
        for (uint16_t i = 0; i < p.count; ++i) {
            const EmberParticle &e = p.embers[i];
            assert(e.max_life >= 1.0f && e.max_life <= 2.5f);
            assert(e.life >= 0.0f && e.life <= e.max_life); // staggered
            assert(e.size >= 10.0f && e.size <= 28.0f);
            assert(e.vy <= -40.0f); // rises
        }
    }

    // ─── count clamps at MAX ───
    {
        EmberPool p;
        p.init(0.0f, 0.0f, 1000, 1u);
        assert(p.count == EMBER_POOL_MAX);
    }

    // ─── update advances + respawns the dead (count stable) ───
    {
        EmberPool p;
        p.init(0.0f, 0.0f, 8, 7u);
        for (int i = 0; i < 600; ++i) {
            p.update(1.0f / 60.0f, static_cast<float>(i) / 60.0f);
        }
        assert(p.count == 8);
        for (uint16_t i = 0; i < p.count; ++i) {
            assert(p.embers[i].life > 0.0f && p.embers[i].life <= p.embers[i].max_life);
        }
    }

    // ─── color ramp: yellow newborn, red elder, alpha tracks life ───
    {
        uint32_t young = EmberColor(0.9f);
        uint32_t old   = EmberColor(0.2f);
        assert((young & 0x00FFFFFFu) == 0x00FFAA33u);
        assert((old & 0x00FFFFFFu) == 0x00FF4400u);
        assert(((young >> 24) & 0xFF) > ((old >> 24) & 0xFF));
        assert(EmberColor(0.0f) == 0x00FF4400u); // fully transparent elder
        assert(EmberColor(1.0f) == 0xFFFFAA33u); // fully opaque newborn
    }

    // ─── size shrinks with age ───
    {
        EmberParticle e{};
        e.size     = 20.0f;
        e.max_life = 2.0f;
        e.life     = 2.0f;
        assert(Near(EmberSize(e), 20.0f));
        e.life = 0.0f;
        assert(Near(EmberSize(e), 10.0f));
    }

    // ─── glow texture: opaque white core, transparent corners ───
    {
        static uint32_t px[EMBER_GLOW_SIZE * EMBER_GLOW_SIZE];
        MakeEmberGlowTexture(px);
        constexpr uint32_t N = EMBER_GLOW_SIZE;
        uint32_t center      = px[(N / 2) * N + (N / 2)];
        assert(((center >> 24) & 0xFF) > 200);   // near-opaque core
        assert((center & 0x00FFFFFFu) == 0x00FFFFFFu); // white
        assert(px[0] == 0x00FFFFFFu);             // transparent corner
        assert(px[N - 1] == 0x00FFFFFFu);
        assert(px[(N - 1) * N] == 0x00FFFFFFu);
        assert(px[N * N - 1] == 0x00FFFFFFu);
    }

    return 0;
}

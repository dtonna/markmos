// Progressbar tests — factory defaults, clamp, target/displayed split,
// smoothing convergence via update(), update(0) noop.
// Plain main() + assert(), no framework. Headless: update() is pure CPU
// (render path needs Renderer and is covered by the mm_07 screenshot).
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

static bool Near(f32 a, f32 b) noexcept {
    return __builtin_fabsf(a - b) < 0.005f;
}

int main() {
    // ─── Factory defaults + clamp ───
    {
        ui::Manager m;
        m.init();
        u16 lo = m.progressbar(10.0f, 10.0f, 200.0f, 24.0f, -0.5f);
        u16 hi = m.progressbar(10.0f, 40.0f, 200.0f, 24.0f, 1.5f);
        assert(m.get_progress(lo) == 0.0f);
        assert(m.get_progress(hi) == 1.0f);
        assert(m.get_progress(UINT16_MAX) == 0.0f); // OOB guard
        m.set_progress(9999, 0.5f);                 // OOB guard: no crash
    }

    // ─── set target, displayed chases via update ───
    {
        ui::Manager m;
        m.init();
        u16 b = m.progressbar(10.0f, 10.0f, 200.0f, 24.0f, 0.0f);
        m.set_progress(b, 1.0f);
        assert(m.get_progress(b) == 0.0f); // target stored, displayed lags
        m.update(1.0f);                    // t_lerp ≈ 1: lands (snap)
        assert(Near(m.get_progress(b), 1.0f));
        // Partial convergence mid-flight.
        m.set_progress(b, 0.0f);
        m.update(0.05f); // t_lerp = 1 - e^-0.5 ≈ 0.39
        f32 mid = m.get_progress(b);
        assert(mid < 1.0f && mid > 0.0f);
        m.update(5.0f); // plenty of time: lands exactly
        assert(m.get_progress(b) == 0.0f);
    }

    // ─── update(0) is a bit-identical no-op ───
    {
        ui::Manager m;
        m.init();
        u16 b = m.progressbar(10.0f, 10.0f, 200.0f, 24.0f, 0.2f);
        m.set_progress(b, 0.9f);
        m.update(0.03f);
        f32 v = m.get_progress(b);
        assert(v > 0.2f);
        m.update(0.0f);
        assert(m.get_progress(b) == v);
    }

    // ─── Circle shape flag survives (render path asserts type only) ───
    {
        ui::Manager m;
        m.init();
        u16 c = m.progressbar(10.0f, 10.0f, 120.0f, 120.0f, 0.33f, UINT16_MAX, 1.0f, 0, (u8)ui::shape_type::CIRCLE);
        assert(c != UINT16_MAX);
        assert(m.get_progress(c) == 0.33f);
        m.set_progress(c, 0.75f);
        m.update(2.0f);
        assert(Near(m.get_progress(c), 0.75f));
    }

    // ─── Completion burst: arrival at full pops one sparkle (circle only) ───
    {
        ui::Manager m;
        m.init();
        u16 c = m.progressbar(10.0f, 10.0f, 120.0f, 120.0f, 0.0f, UINT16_MAX, 1.0f, 0, (u8)ui::shape_type::CIRCLE);
        u16 l = m.progressbar(10.0f, 140.0f, 200.0f, 24.0f, 0.0f); // line: never bursts
        assert(m.progressbar_burst[c] < 0.0f && m.progressbar_burst[l] < 0.0f);
        m.set_progress(c, 1.0f);
        m.set_progress(l, 1.0f);
        m.update(2.0f); // both land at full
        assert(m.get_progress(c) == 1.0f && m.get_progress(l) == 1.0f);
        assert(m.progressbar_burst[c] >= 0.0f); // circle pops
        assert(m.progressbar_burst[l] < 0.0f);  // line stays quiet
        // Burst runs its course then goes inactive (dt=0 freezes it).
        f32 t0 = m.progressbar_burst[c];
        m.update(0.0f);
        assert(m.progressbar_burst[c] == t0);
        m.update(0.5f);
        assert(m.progressbar_burst[c] > t0);
        m.update(1.0f);
        assert(m.progressbar_burst[c] < 0.0f); // past K_BURST_DUR
        // Same fill again: no second pop (latched) …
        m.update(1.0f);
        assert(m.progressbar_burst[c] < 0.0f);
        // … until the target drops (re-arm) and refills.
        m.set_progress(c, 0.2f);
        m.update(2.0f);
        assert(Near(m.get_progress(c), 0.2f));
        m.set_progress(c, 1.0f);
        m.update(2.0f);
        assert(m.progressbar_burst[c] >= 0.0f);
    }

    // ─── Born full: no pop on the first update ───
    {
        ui::Manager m;
        m.init();
        u16 c = m.progressbar(10.0f, 10.0f, 120.0f, 120.0f, 1.0f, UINT16_MAX, 1.0f, 0, (u8)ui::shape_type::CIRCLE);
        m.update(1.0f);
        assert(m.progressbar_burst[c] < 0.0f);
    }

    return 0;
}

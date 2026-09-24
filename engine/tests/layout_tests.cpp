// Layout engine tests — alignment, percent size, margin, nesting.
// Plain main() + assert(), no framework (matches backend_concept_tests).
// Headless: widgets use absolute sizes (no AutoW/H), so measure() is a
// no-op over an empty default font; layout() is pure CPU.
#include "../ui/mm_ui.hpp"
#include <cassert>
#include <cmath>

static bool Near(float a, float b, float eps = 0.01f) noexcept {
    return std::fabs(a - b) <= eps;
}

static void NoopBtn(uint16_t) noexcept {}

int main() {
    // ─── HBox legacy (align Start, absolute, no margin) ───
    {
        ui::Manager m;
        m.init();
        uint16_t box = m.panel(0.0f, 0.0f, 500.0f, 60.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        uint16_t b0 = m.button(0.0f, 0.0f, 100.0f, 44.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        uint16_t b1 = m.button(0.0f, 0.0f, 100.0f, 44.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        uint16_t b2 = m.button(0.0f, 0.0f, 100.0f, 44.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        Renderer r;
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        assert(Near(m.pool[b0].x, 8.0f) && Near(m.pool[b0].y, 8.0f));
        assert(Near(m.pool[b1].x, 116.0f) && Near(m.pool[b1].y, 8.0f));
        assert(Near(m.pool[b2].x, 224.0f) && Near(m.pool[b2].y, 8.0f));
    }

    // ─── HBox Center/Center ───
    {
        ui::Manager m;
        m.init();
        uint16_t box = m.panel(0.0f, 0.0f, 500.0f, 60.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        m.set_layout_align(box, 1, 1);
        uint16_t b0 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        uint16_t b1 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        uint16_t b2 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        Renderer r;
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // total = 300 + 16 = 316; inner = 484; start = 8 + 84 = 92
        assert(Near(m.pool[b0].x, 92.0f) && Near(m.pool[b2].x, 308.0f));
        assert(Near(m.pool[b1].x, 200.0f));
        // cross: inner_h = 44; (44 - 32) / 2 = 6 -> y = 8 + 6 = 14
        assert(Near(m.pool[b0].y, 14.0f) && Near(m.pool[b1].y, 14.0f) && Near(m.pool[b2].y, 14.0f));
    }

    // ─── HBox End/End ───
    {
        ui::Manager m;
        m.init();
        uint16_t box = m.panel(0.0f, 0.0f, 500.0f, 60.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        m.set_layout_align(box, 2, 2);
        uint16_t b0 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        uint16_t b2 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        (void)m.button(0.0f, 0.0f, 100.0f, 32.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        Renderer r;
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // start = 8 + (484 - 316) = 176; cross end: 8 + (44 - 32) = 20
        // (creation order b0, b2, anon -> positions 176, 284, 392)
        assert(Near(m.pool[b0].x, 176.0f) && Near(m.pool[b0].y, 20.0f));
        assert(Near(m.pool[b2].x, 284.0f) && Near(m.pool[b2].y, 20.0f));
    }

    // ─── VBox End(main)/Center(cross) — exercises the Axis=1 kernel ───
    {
        ui::Manager m;
        m.init();
        uint16_t box = m.panel(0.0f, 0.0f, 200.0f, 200.0f, 0xFF222233);
        m.set_layout(box, 2, 8, 8);
        m.set_layout_align(box, 2, 1);
        uint16_t b0 = m.button(0.0f, 0.0f, 180.0f, 40.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        uint16_t b1 = m.button(0.0f, 0.0f, 180.0f, 40.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        Renderer r;
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // total = 80 + 8 = 88; inner = 184; start = 8 + 96 = 104
        assert(Near(m.pool[b0].y, 104.0f) && Near(m.pool[b1].y, 152.0f));
        // cross center: 8 + (184 - 180) / 2 = 10
        assert(Near(m.pool[b0].x, 10.0f) && Near(m.pool[b1].x, 10.0f));
    }

    // ─── Percent widths + margins ───
    {
        ui::Manager m;
        m.init();
        uint16_t box = m.panel(0.0f, 0.0f, 800.0f, 56.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        uint16_t p0 = m.button(0.0f, 0.0f, 10.0f, 40.0f, "A", 0xFF3366CC, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        uint16_t p1 = m.button(0.0f, 0.0f, 10.0f, 40.0f, "B", 0xFF3366CC, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        m.set_size_pct(p0, 30, 0);
        m.set_size_pct(p1, 40, 0);
        m.set_margin(p0, 0, 6, 0, 6);
        m.set_margin(p1, 0, 6, 0, 6);
        Renderer r;
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // inner = 784; w0 = 235.2 @ x = 8 + 6 = 14 (w written back)
        assert(Near(m.pool[p0].x, 14.0f) && Near(m.pool[p0].w, 235.2f));
        // x1 = 14 + 235.2 + 6 + 8 + 6 = 269.2, w = 313.6
        assert(Near(m.pool[p1].x, 269.2f) && Near(m.pool[p1].w, 313.6f));
    }

    // ─── Nested container under a plain panel (previously never laid out) ───
    {
        ui::Manager m;
        m.init();
        uint16_t bg  = m.panel(0.0f, 0.0f, 800.0f, 600.0f, 0xFF2D2D2D);
        uint16_t box = m.panel(20.0f, 20.0f, 400.0f, 60.0f, 0xFF222233, bg);
        m.set_layout(box, 1, 8, 8);
        uint16_t b0 = m.button(0.0f, 0.0f, 100.0f, 40.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        uint16_t b1 = m.button(0.0f, 0.0f, 100.0f, 40.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        Renderer r;
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // Local positions are what layout owns; abs resolves via parents.
        assert(Near(m.pool[b0].x, 8.0f) && Near(m.pool[b0].y, 8.0f));
        assert(Near(m.pool[b1].x, 116.0f) && Near(m.pool[b1].y, 8.0f));
        assert(Near(m.pool[box].x, 20.0f) && Near(m.pool[box].y, 20.0f));
    }

    return 0;
}

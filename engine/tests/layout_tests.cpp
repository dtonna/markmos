// Layout engine tests — alignment, percent size, margin, nesting.
// Plain main() + assert(), no framework (matches backend_concept_tests).
// Headless: widgets use absolute sizes (no AutoW/H), so measure() is a
// no-op over an empty default font; layout() is pure CPU.
#include "../ui/mm_ui.hpp"
#include <cassert>
#include <cmath>

static bool Near(f32 a, f32 b, f32 eps = 0.01f) noexcept {
    return __builtin_fabsf(a - b) <= eps;
}

static void NoopBtn(u16, void *) noexcept {}


int main() {
    // ONE Manager for the whole binary. sizeof(ui::Manager) is ~65 KB, and a
    // per-group `ui::Manager m;` in 19 scoped blocks gave main() an 8.1 MB stack
    // frame - larger than macOS's 8 MB main-thread stack, so the binary
    // segfaulted in the prologue before main's first statement. It only ever
    // ran because ctest's child happens to get a bigger stack. `= Manager{}`
    // value-initialises (pool and count included), which is what a fresh object
    // gave, so each group still starts from a clean Manager.
    ui::Manager m;
    // ...and ONE Renderer, which matters far more: sizeof(Renderer) is 1.03 MB
    // (the render graph uses fixed-size arrays, not the heap), so seven
    // per-group locals gave main() a 7.4 MB frame on top of the Managers.
    Renderer r;
    // ─── HBox legacy (align Start, absolute, no margin) ───
    {
        m = ui::Manager{};
        m.init();
        u16 box = m.panel(0.0f, 0.0f, 500.0f, 60.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        u16 b0 = m.button(0.0f, 0.0f, 100.0f, 44.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 b1 = m.button(0.0f, 0.0f, 100.0f, 44.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 b2 = m.button(0.0f, 0.0f, 100.0f, 44.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        assert(Near(m.pool[b0].frame.x, 8.0f) && Near(m.pool[b0].frame.y, 8.0f));
        assert(Near(m.pool[b1].frame.x, 116.0f) && Near(m.pool[b1].frame.y, 8.0f));
        assert(Near(m.pool[b2].frame.x, 224.0f) && Near(m.pool[b2].frame.y, 8.0f));
    }

    // ─── HBox Center/Center ───
    {
        m = ui::Manager{};
        m.init();
        u16 box = m.panel(0.0f, 0.0f, 500.0f, 60.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        m.set_layout_align(box, 1, 1);
        u16 b0 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 b1 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 b2 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // total = 300 + 16 = 316; inner = 484; start = 8 + 84 = 92
        assert(Near(m.pool[b0].frame.x, 92.0f) && Near(m.pool[b2].frame.x, 308.0f));
        assert(Near(m.pool[b1].frame.x, 200.0f));
        // cross: inner_h = 44; (44 - 32) / 2 = 6 -> y = 8 + 6 = 14
        assert(Near(m.pool[b0].frame.y, 14.0f) && Near(m.pool[b1].frame.y, 14.0f) && Near(m.pool[b2].frame.y, 14.0f));
    }

    // ─── HBox End/End ───
    {
        m = ui::Manager{};
        m.init();
        u16 box = m.panel(0.0f, 0.0f, 500.0f, 60.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        m.set_layout_align(box, 2, 2);
        u16 b0 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 b2 = m.button(0.0f, 0.0f, 100.0f, 32.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        (void)m.button(0.0f, 0.0f, 100.0f, 32.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // start = 8 + (484 - 316) = 176; cross end: 8 + (44 - 32) = 20
        // (creation order b0, b2, anon -> positions 176, 284, 392)
        assert(Near(m.pool[b0].frame.x, 176.0f) && Near(m.pool[b0].frame.y, 20.0f));
        assert(Near(m.pool[b2].frame.x, 284.0f) && Near(m.pool[b2].frame.y, 20.0f));
    }

    // ─── VBox End(main)/Center(cross) — exercises the Axis=1 kernel ───
    {
        m = ui::Manager{};
        m.init();
        u16 box = m.panel(0.0f, 0.0f, 200.0f, 200.0f, 0xFF222233);
        m.set_layout(box, 2, 8, 8);
        m.set_layout_align(box, 2, 1);
        u16 b0 = m.button(0.0f, 0.0f, 180.0f, 40.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 b1 = m.button(0.0f, 0.0f, 180.0f, 40.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // total = 80 + 8 = 88; inner = 184; start = 8 + 96 = 104
        assert(Near(m.pool[b0].frame.y, 104.0f) && Near(m.pool[b1].frame.y, 152.0f));
        // cross center: 8 + (184 - 180) / 2 = 10
        assert(Near(m.pool[b0].frame.x, 10.0f) && Near(m.pool[b1].frame.x, 10.0f));
    }

    // ─── Percent widths + margins ───
    {
        m = ui::Manager{};
        m.init();
        u16 box = m.panel(0.0f, 0.0f, 800.0f, 56.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        u16 p0 = m.button(0.0f, 0.0f, 10.0f, 40.0f, "A", 0xFF3366CC, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 p1 = m.button(0.0f, 0.0f, 10.0f, 40.0f, "B", 0xFF3366CC, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        m.set_size_pct(p0, 30, 0);
        m.set_size_pct(p1, 40, 0);
        m.set_margin(p0, 0, 6, 0, 6);
        m.set_margin(p1, 0, 6, 0, 6);
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // inner = 784; w0 = 235.2 @ x = 8 + 6 = 14 (w written back)
        assert(Near(m.pool[p0].frame.x, 14.0f) && Near(m.pool[p0].frame.w, 235.2f));
        // x1 = 14 + 235.2 + 6 + 8 + 6 = 269.2, w = 313.6
        assert(Near(m.pool[p1].frame.x, 269.2f) && Near(m.pool[p1].frame.w, 313.6f));
    }

    // ─── Nested container under a plain panel (previously never laid out) ───
    {
        m = ui::Manager{};
        m.init();
        u16 bg  = m.panel(0.0f, 0.0f, 800.0f, 600.0f, 0xFF2D2D2D);
        u16 box = m.panel(20.0f, 20.0f, 400.0f, 60.0f, 0xFF222233, bg);
        m.set_layout(box, 1, 8, 8);
        u16 b0 = m.button(0.0f, 0.0f, 100.0f, 40.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 b1 = m.button(0.0f, 0.0f, 100.0f, 40.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // Local positions are what layout owns; abs resolves via parents.
        assert(Near(m.pool[b0].frame.x, 8.0f) && Near(m.pool[b0].frame.y, 8.0f));
        assert(Near(m.pool[b1].frame.x, 116.0f) && Near(m.pool[b1].frame.y, 8.0f));
        assert(Near(m.pool[box].frame.x, 20.0f) && Near(m.pool[box].frame.y, 20.0f));
    }

    // ─── position_mode: ABSOLUTE ignores the parent chain ───
    {
        m = ui::Manager{};
        m.init();
        u16 bg = m.panel(100.0f, 100.0f, 400.0f, 300.0f, 0xFF2D2D2D);
        u16 c = m.button(10.0f, 10.0f, 60.0f, 30.0f, "X", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, bg, 0.4f);
        m.set_position_mode(c, mm_math::position_mode::ABSOLUTE);
        InputState in;
        in.init();
        m.handle(in); // rebuilds the abs cache headless
        assert(Near(m.abs_x(c), 10.0f) && Near(m.abs_y(c), 10.0f)); // not 110/110
    }

    // ─── position_mode: RELATIVE default folds nested offsets ───
    {
        m = ui::Manager{};
        m.init();
        u16 bg = m.panel(100.0f, 100.0f, 400.0f, 300.0f, 0xFF2D2D2D);
        u16 mid = m.panel(20.0f, 30.0f, 200.0f, 200.0f, 0xFF222233, bg);
        u16 leaf = m.button(5.0f, 5.0f, 60.0f, 30.0f, "X", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, mid, 0.4f);
        InputState in;
        in.init();
        m.handle(in);
        assert(Near(m.abs_x(leaf), 125.0f) && Near(m.abs_y(leaf), 135.0f));
    }

    // ─── position_mode: ANCHORED pins center + offset, BOTTOM_RIGHT corner ───
    {
        m = ui::Manager{};
        m.init();
        u16 bg = m.panel(100.0f, 100.0f, 200.0f, 200.0f, 0xFF2D2D2D);
        u16 c0 = m.button(0.0f, 0.0f, 40.0f, 20.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, bg, 0.4f);
        m.set_position_mode(c0, mm_math::position_mode::ANCHORED);
        m.set_anchor(c0, mm_math::anchor::CENTER);
        u16 c1 = m.button(10.0f, 5.0f, 40.0f, 20.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, bg, 0.4f);
        m.set_position_mode(c1, mm_math::position_mode::ANCHORED);
        m.set_anchor(c1, mm_math::anchor::CENTER);
        u16 c2 = m.button(0.0f, 0.0f, 40.0f, 20.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, bg, 0.4f);
        m.set_position_mode(c2, mm_math::position_mode::ANCHORED);
        m.set_anchor(c2, mm_math::anchor::BOTTOM_RIGHT);
        InputState in;
        in.init();
        m.handle(in);
        // center of (100,100,200,200) = (200,200), minus half-size (20,10)
        assert(Near(m.abs_x(c0), 180.0f) && Near(m.abs_y(c0), 190.0f));
        assert(Near(m.abs_x(c1), 190.0f) && Near(m.abs_y(c1), 195.0f)); // + frame offset
        assert(Near(m.abs_x(c2), 280.0f) && Near(m.abs_y(c2), 290.0f)); // (300,300) - (20,10)
    }

    // ─── position_mode: root ANCHORED pins to the view, falls back without it ───
    {
        m = ui::Manager{};
        m.init();
        u16 d = m.panel(0.0f, 0.0f, 100.0f, 50.0f, 0xFF2D2D2D);
        m.set_position_mode(d, mm_math::position_mode::ANCHORED);
        m.set_anchor(d, mm_math::anchor::CENTER);
        InputState in;
        in.init();
        m.handle(in); // view unknown (0) → ABSOLUTE fallback
        assert(Near(m.abs_x(d), 0.0f) && Near(m.abs_y(d), 0.0f));
        m.view_w = 800.0f;
        m.view_h = 600.0f;
        m.set_anchor(d, mm_math::anchor::CENTER); // re-dirty the cache
        m.handle(in);
        assert(Near(m.abs_x(d), 350.0f) && Near(m.abs_y(d), 275.0f)); // (400,300) - (50,25)
    }

    // ─── layout flow skips out-of-flow (non-RELATIVE) children ───
    {
        m = ui::Manager{};
        m.init();
        u16 box = m.panel(0.0f, 0.0f, 500.0f, 60.0f, 0xFF222233);
        m.set_layout(box, 1, 8, 8);
        u16 b0 = m.button(0.0f, 0.0f, 100.0f, 44.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 b1 = m.button(0.0f, 0.0f, 100.0f, 44.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        u16 ov = m.button(0.0f, 0.0f, 100.0f, 44.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, box, 0.4f);
        m.set_position_mode(ov, mm_math::position_mode::ANCHORED);
        m.set_anchor(ov, mm_math::anchor::CENTER);
        m.measure_dirty = false; // skip font measure: headless, sizes are absolute
        m.layout(r);
        // Flow sees 2 buttons only (8, 116 — not shifted by the overlay).
        assert(Near(m.pool[b0].frame.x, 8.0f) && Near(m.pool[b1].frame.x, 116.0f));
        InputState in;
        in.init();
        m.handle(in);
        // Overlay centers on the box: (250,30) - (50,22).
        assert(Near(m.abs_x(ov), 200.0f) && Near(m.abs_y(ov), 8.0f));
    }

    // ─── resolve_border_sides: legacy uniform vs per-side ───
    {
        ui::WidgetStyle s{};
        s.border_color = 0xFF112233;
        s.border_width = 0.08f;
        f32 w[4];
        u32 c[4];
        ui::resolve_border_sides(s, w, c); // all-zero sides → legacy everywhere
        for (int k = 0; k < 4; ++k) {
            assert(Near(w[k], 0.08f) && c[k] == 0xFF112233);
        }
        s.border_w[ui::BORDER_L] = 0.10f;
        s.border_c[ui::BORDER_L] = 0xFFFF0000;
        s.border_w[ui::BORDER_T] = 0.05f; // no color → legacy color
        ui::resolve_border_sides(s, w, c);
        assert(Near(w[ui::BORDER_L], 0.10f) && c[ui::BORDER_L] == 0xFFFF0000);
        assert(Near(w[ui::BORDER_T], 0.05f) && c[ui::BORDER_T] == 0xFF112233);
        assert(Near(w[ui::BORDER_R], 0.0f) && c[ui::BORDER_R] == 0xFF112233);
        assert(Near(w[ui::BORDER_B], 0.0f) && c[ui::BORDER_B] == 0xFF112233);
    }

    // ─── resolve_ring: ring fields win, else legacy (never per-side) ───
    {
        ui::WidgetStyle s{};
        s.border_color = 0xFF112233;
        s.border_width = 0.08f;
        s.border_w[ui::BORDER_L] = 0.20f; // must NOT leak into the ring
        f32 rw;
        u32 rc;
        ui::resolve_ring(s, rw, rc);
        assert(Near(rw, 0.08f) && rc == 0xFF112233);
        s.ring_width = 0.12f;
        s.ring_color = 0xFF00FF00;
        ui::resolve_ring(s, rw, rc);
        assert(Near(rw, 0.12f) && rc == 0xFF00FF00);
    }

    // ─── content_box: content stays inside the L/R caps ───
    {
        ui::WidgetStyle s{};
        s.border_color = 0xFF112233;
        s.border_width = 0.04f;
        f32 x0 = 0.0f, x1 = 0.0f;
        ui::content_box(s, 100.0f, 400.0f, x0, x1); // uniform 0.04 × 400pt caps
        assert(Near(x0, 116.0f) && Near(x1, 484.0f));
        ui::WidgetStyle z{}; // borderless → identity (slider/progressbar/listbox unchanged)
        ui::content_box(z, 100.0f, 400.0f, x0, x1);
        assert(Near(x0, 100.0f) && Near(x1, 500.0f));
        // Per-side widths inset asymmetrically (listbox scrollbar pins to x1).
        s.border_w[ui::BORDER_L] = 0.10f;
        s.border_w[ui::BORDER_R] = 0.02f;
        ui::content_box(s, 0.0f, 200.0f, x0, x1);
        assert(Near(x0, 20.0f) && Near(x1, 196.0f));
    }

    // ─── content_insets / content_rect: 4-side inner band ───
    {
        ui::WidgetStyle s{};
        s.border_color = 0xFF112233;
        s.border_width = 0.04f;
        f32 il = 0.0f, it = 0.0f, ir = 0.0f, ib = 0.0f;
        ui::content_insets(s, 400.0f, 280.0f, il, it, ir, ib);
        assert(Near(il, 16.0f) && Near(ir, 16.0f)); // 0.04 × w
        assert(Near(it, 11.2f) && Near(ib, 11.2f)); // 0.04 × h (SDF: per-axis)
        f32 x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        ui::content_rect(s, 100.0f, 50.0f, 400.0f, 280.0f, x0, y0, x1, y1);
        assert(Near(x0, 116.0f) && Near(x1, 484.0f));
        assert(Near(y0, 61.2f) && Near(y1, 318.8f));
        ui::WidgetStyle z{}; // borderless → frame rect
        ui::content_rect(z, 100.0f, 50.0f, 400.0f, 280.0f, x0, y0, x1, y1);
        assert(Near(x0, 100.0f) && Near(y0, 50.0f) && Near(x1, 500.0f) && Near(y1, 330.0f));
    }

    // ─── container helpers: tree queries, no base class ───
    {
        m = ui::Manager{};
        m.init();
        u16 root = m.panel(10.0f, 10.0f, 400.0f, 300.0f, 0xFF2D2D2D, UINT16_MAX);
        u16 a = m.button(10.0f, 10.0f, 100.0f, 30.0f, "A", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, root);
        u16 b = m.button(10.0f, 50.0f, 100.0f, 30.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, root);
        u16 inner = m.panel(10.0f, 90.0f, 200.0f, 100.0f, 0xFF333333, root);
        u16 c = m.button(10.0f, 10.0f, 100.0f, 30.0f, "C", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, inner);

        // Direct children vs whole subtree.
        assert(m.child_count(root) == 3); // A, B, inner (not C)
        u16 direct = 0, deep = 0;
        m.for_each_child(root, [&direct](u16) noexcept { ++direct; });
        m.for_each_descendant(root, [&deep](u16) noexcept { ++deep; });
        assert(direct == 3 && deep == 4);

        // A plain button is not a container; a panel is (children are created
        // with parent=), as is anything that lays out or clips.
        assert(!m.is_container(a));
        assert(m.is_container(root));
        m.layout_type[inner] = 2; // VBox
        assert(m.is_container(inner));
        assert(!m.is_container(a));
    }

    // ─── effective enabled/visible: derived from ancestors ───
    {
        m = ui::Manager{};
        m.init();
        u16 root = m.panel(10.0f, 10.0f, 400.0f, 300.0f, 0xFF2D2D2D, UINT16_MAX);
        u16 kid = m.button(10.0f, 10.0f, 100.0f, 30.0f, "K", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, root);

        assert(m.is_enabled(kid) && m.is_visible(kid));

        // Disabling the CONTAINER disables the child...
        m.set_enabled(root, false);
        assert(!m.is_enabled(root));
        assert(!m.is_enabled(kid));

        // ...and re-enabling the child alone does NOT bring it back, which is
        // the whole point of deriving instead of propagating: a stored copy
        // cannot represent "one live child of a dead panel".
        m.set_enabled(kid, true);
        assert(!m.is_enabled(kid));
        m.set_enabled(root, true);
        assert(m.is_enabled(kid));

        // Same for visibility, and setters touch only their own widget.
        m.set_visible(root, false);
        assert(!m.is_visible(kid));
        m.set_visible(root, true);
        assert(m.is_visible(kid));
        assert((m.pool[root].flags & ui::WF_VISIBLE) != 0);
    }

    // ─── align_text_x + set_text_align (pure geometry, no Renderer) ───
    {
        // LEFT is the identity; CENTER and RIGHT place the text's own width.
        assert(Near(ui::align_text_x(ui::text_align::LEFT, 10.0f, 200.0f, 60.0f), 10.0f));
        assert(Near(ui::align_text_x(ui::text_align::CENTER, 10.0f, 200.0f, 60.0f), 80.0f));
        assert(Near(ui::align_text_x(ui::text_align::RIGHT, 10.0f, 200.0f, 60.0f), 150.0f));
        // Wider than the box: both modes push left of it rather than clamping
        // (a clipped overflowing label is still anchored to its edge).
        assert(Near(ui::align_text_x(ui::text_align::CENTER, 0.0f, 100.0f, 160.0f), -30.0f));
        assert(Near(ui::align_text_x(ui::text_align::RIGHT, 0.0f, 100.0f, 160.0f), -60.0f));
    }
    {
        m = ui::Manager{};
        m.init();
        // Default is LEFT, and a label is AUTO_W so its box hugs the text -
        // which is why set_text_align can pin a width.
        u16 lb = m.label(10.0f, 10.0f, "hello", 0xFFFFFFFF, 1.0f, UINT16_MAX);
        assert(m.get_text_align(lb) == ui::text_align::LEFT);
        assert(m.pool[lb].flags & ui::WF_AUTO_W);
        // Alignment alone does not pin the width (box_w = 0 = keep auto).
        m.set_text_align(lb, ui::text_align::CENTER);
        assert(m.get_text_align(lb) == ui::text_align::CENTER);
        assert(m.pool[lb].flags & ui::WF_AUTO_W);
        // With a width: pins the box (AUTO_W off) so centring has something to
        // centre inside.
        m.set_text_align(lb, ui::text_align::CENTER, 300.0f);
        assert(Near(m.pool[lb].frame.w, 300.0f));
        assert(!(m.pool[lb].flags & ui::WF_AUTO_W));
        // Invalid id is a no-op.
        m.set_text_align(9999, ui::text_align::RIGHT, 10.0f);
        assert(m.get_text_align(9999) == ui::text_align::LEFT);
        // Theme default seeds the factory.
        m.theme.label_align = ui::text_align::RIGHT;
        u16 lb2 = m.label(0.0f, 0.0f, "x", 0xFFFFFFFF, 1.0f, UINT16_MAX);
        assert(m.get_text_align(lb2) == ui::text_align::RIGHT);
    }

    // ─── separator + tabbar ───────────────────────────────────────
    {
        // A separator's frame IS the rule: fixed size, no auto, no text.
        m = ui::Manager{};
        m.init();
        u16 sp = m.separator(10.0f, 20.0f, 300.0f, UINT16_MAX);
        assert(m.pool[sp].type == (u8)ui::widget_type::SEPARATOR);
        assert(Near(m.pool[sp].frame.w, 300.0f));
        assert(Near(m.pool[sp].frame.h, m.theme.separator_thickness));
        assert(!(m.pool[sp].flags & ui::WF_AUTO_W) && !(m.pool[sp].flags & ui::WF_AUTO_H));
        assert(!(m.pool[sp].flags & ui::WF_FOCUSABLE));
        assert(m.pool[sp].bg_color == m.theme.separator_color);
        // Explicit colour/thickness win over the theme.
        u16 sp2 = m.separator(0.0f, 0.0f, 100.0f, UINT16_MAX, 0xFFFF0000, 5.0f);
        assert(m.pool[sp2].bg_color == 0xFFFF0000 && Near(m.pool[sp2].frame.h, 5.0f));
        // Vertical: the LENGTH is h, the width is the thickness.
        u16 sp3 = m.separator_v(0.0f, 0.0f, 200.0f, UINT16_MAX);
        assert(Near(m.pool[sp3].frame.h, 200.0f));
        assert(Near(m.pool[sp3].frame.w, m.theme.separator_thickness));
        // Not a hit target (a rule you can tap is a bug).
        assert(m.hit_test(150.0f, 20.0f + m.theme.separator_thickness * 0.5f) != sp);
    }
    {
        // cell_at is the shared geometry: render never inverts it separately.
        assert(ui::Manager::tabbar_cell_at(300.0f, 3, 0.0f) == 0);
        assert(ui::Manager::tabbar_cell_at(300.0f, 3, 99.9f) == 0);
        assert(ui::Manager::tabbar_cell_at(300.0f, 3, 100.0f) == 1);
        assert(ui::Manager::tabbar_cell_at(300.0f, 3, 299.9f) == 2);
        // Exactly on the right edge belongs to the LAST cell (not out of range).
        assert(ui::Manager::tabbar_cell_at(300.0f, 3, 300.0f) == 2);
        // Degenerate: no tabs, no width, outside.
        assert(ui::Manager::tabbar_cell_at(300.0f, 0, 10.0f) == -1);
        assert(ui::Manager::tabbar_cell_at(0.0f, 3, 0.0f) == -1);
        assert(ui::Manager::tabbar_cell_at(300.0f, 3, -1.0f) == -1);
        assert(ui::Manager::tabbar_cell_at(300.0f, 3, 301.0f) == -1);
    }
    {
        m = ui::Manager{};
        m.init();
        static const char *tabs[] = {"Deals", "Stats", "Help"};
        u16          tb      = m.tabbar(20.0f, 20.0f, 300.0f, 40.0f, tabs, 3, UINT16_MAX);
        assert(m.pool[tb].type == (u8)ui::widget_type::TABBAR);
        assert(m.pool[tb].flags & ui::WF_FOCUSABLE);
        assert(m.get_tab(tb) == 0);
        m.set_tab(tb, 2);
        assert(m.get_tab(tb) == 2);
        // Out-of-range is refused, not clamped (a silent clamp would hide a
        // bad index from the app).
        m.set_tab(tb, 3);
        m.set_tab(tb, -1);
        assert(m.get_tab(tb) == 2);
        assert(m.get_tab(9999) == -1);
        // `active` is clamped at construction so a stale index cannot render
        // an empty highlight.
        u16 tb2 = m.tabbar(0.0f, 0.0f, 100.0f, 30.0f, tabs, 3, UINT16_MAX, 9);
        assert(m.get_tab(tb2) == 2);
        // The app's array is stored by pointer, like listbox_items.
        assert(m.tabbar_count_[tb] == 3 && m.tabbar_items[tb] == tabs);
    }

    // ─── rows::baseline_from (pure vertical placement) ───────────
    {
        // Centred on the GLYPH box, not the line box: the baseline sits one
        // line_h*scale below the box top when the row is exactly one line
        // tall, and a taller row adds half the slack on each side.
        assert(Near(ui::rows::baseline_from(40.0f, 100.0f, 40.0f, 1.0f), 140.0f));
        assert(Near(ui::rows::baseline_from(40.0f, 100.0f, 80.0f, 1.0f), 160.0f));
        // Scale shrinks the glyph box, so the text sits higher in the row:
        // 20pt of glyph in a 40pt row -> 10pt of slack above it.
        assert(Near(ui::rows::baseline_from(40.0f, 100.0f, 40.0f, 0.5f), 130.0f));
        // Row origin moves the baseline with it, 1:1.
        assert(Near(ui::rows::baseline_from(40.0f, 0.0f, 40.0f, 1.0f), 40.0f));
        // A zero-height line box would divide by nothing - the helper must not
        // produce NaN/inf that then poisons a vertex.
        const f32 z = ui::rows::baseline_from(0.0f, 50.0f, 20.0f, 1.0f);
        assert(z == z && z > 0.0f && z < 1e9f);
    }

    // ─── A negative frame degrades to "draws nothing", not "draws everything" ───
    // Layouts are absolute (`view_h - 230`), so a small window produces a
    // NEGATIVE rect, and the scissor args are uint16 - where a negative f32
    // wraps to ~65000, i.e. a scissor covering the whole window. The clamp lives
    // in render(), which needs a Renderer, so what is pinned here is the
    // property it depends on: content_insets() on a degenerate rect must not
    // report a huge box, and a zero-size widget must not be a tap target.
    {
        f32 il, it, ir, ib;
        ui::content_insets(ui::WidgetStyle{}, 0.0f, 0.0f, il, it, ir, ib);
        assert(ir - il >= 0.0f && ib - it >= 0.0f);
        ui::content_insets(ui::WidgetStyle{}, 200.0f, -30.0f, il, it, ir, ib);
        assert(ib - it >= 0.0f);

        m = ui::Manager{};
        m.init();
        u16 z = m.button(0.0f, 0.0f, 0.0f, 0.0f, "z", 0xFF3A3A3A, 0xFFFFFFFF, nullptr);
        (void)z;
        assert(m.hit_test(1.0f, 1.0f) != z);
    }

    // ─── ScrollView: pure geometry, no Manager needed ───
    {
        using ui::Manager;
        // No scrollbar when it all fits - the classic rule.
        assert(Manager::scroll_max(100.0f, 200.0f) == 0.0f);
        assert(Manager::scroll_max(200.0f, 200.0f) == 0.0f);
        assert(Near(Manager::scroll_max(500.0f, 200.0f), 300.0f));
        assert(Manager::scroll_max(0.0f, 0.0f) == 0.0f);

        // Thumb is proportional to how much is visible, and never smaller than a
        // grabbable stub (or bigger than the track).
        const f32 th_half = Manager::scroll_thumb_h(200.0f, 200.0f); // half visible
        assert(Near(th_half, 100.0f));
        const f32 th_small = Manager::scroll_thumb_h(200.0f, 5000.0f);
        assert(th_small >= 24.0f && th_small < 200.0f);
        const f32 th_none = Manager::scroll_thumb_h(200.0f, 0.0f);
        assert(Near(th_none, 200.0f)); // nothing to scroll: thumb fills the track
        // The bar is a constant width, not a ratio of the view: deriving it from
        // the ListBox's row-height ratio and feeding it the VIEW height gave a
        // 94pt bar on a 260pt view.
        assert(Manager::K_SCROLL_BAR_W == 10.0f);
    }

    // ─── ScrollView: the offset moves the abs cache, so draw AND hit agree ───
    {
        ui::Manager mm;
        mm.init();
        u16 sv = mm.scrollview(100.0f, 100.0f, 200.0f, 150.0f, UINT16_MAX);
        (void)sv;
        // Content coordinates: the child sits at y=300 inside a 600px tall area,
        // so it starts BELOW the 150px window and has to be scrolled to.
        u16 kid = mm.button(0.0f, 300.0f, 100.0f, 30.0f, "far", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, sv);
        (void)kid;
        assert(mm.scroll_max(sv) == 0.0f); // no content height declared yet
        mm.set_scroll_content_h(sv, 600.0f);
        assert(Near(mm.scroll_max(sv), 450.0f));

        mm.hit_test(0.0f, 0.0f); // rebuild the abs cache
        assert(Near(mm.abs_y(sv), 100.0f)); // the view itself never moves
        assert(Near(mm.abs_y(kid), 400.0f)); // 100 + 300 content offset

        // Scrolling moves the CHILD, not the view.
        // hit_test() rebuilds the abs cache, which is what render()/handle() do
        // each frame: set_scroll only MARKS it dirty, like every other setter
        // here (set_position_mode, set_anchor, alloc). Reading abs_y straight
        // after a setter without a rebuild reads the previous frame, by design.
        mm.set_scroll(sv, 200.0f);
        mm.hit_test(0.0f, 0.0f);
        assert(Near(mm.get_scroll(sv), 200.0f));
        assert(Near(mm.abs_y(sv), 100.0f));
        assert(Near(mm.abs_y(kid), 200.0f));

        // And hit-testing follows, because it reads the same cache. This is the
        // reason the offset lives here and not in the draw pass: a tap has to
        // land where the widget is DRAWN.
        assert(mm.hit_test(150.0f, 215.0f) == kid);
        assert(mm.hit_test(150.0f, 415.0f) != kid); // its old position is dead

        // Clamped both ends.
        mm.set_scroll(sv, -50.0f);
        assert(mm.get_scroll(sv) == 0.0f);
        mm.set_scroll(sv, 99999.0f);
        assert(Near(mm.get_scroll(sv), 450.0f));

        // set_scroll clamps against the CURRENT content height: 400 is legal
        // while the content is 600 tall (max 450).
        mm.set_scroll(sv, 400.0f);
        assert(Near(mm.get_scroll(sv), 400.0f));
        // Shrinking the content pulls a now-illegal offset back in: the window is
        // 150 tall, so 200 of content leaves a max of 50.
        mm.set_scroll_content_h(sv, 200.0f);
        assert(Near(mm.get_scroll(sv), 50.0f));
        // ...and all the way to 0 when the content stops overflowing.
        mm.set_scroll_content_h(sv, 100.0f);
        assert(mm.get_scroll(sv) == 0.0f);
    }

    // ─── ScrollView: nested views compose, and clip to their own window ───
    {
        ui::Manager mm;
        mm.init();
        u16 outer = mm.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        u16 inner = mm.scrollview(0.0f, 40.0f, 200.0f, 60.0f, outer);
        u16 leaf  = mm.button(0.0f, 200.0f, 50.0f, 20.0f, "leaf", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, inner);
        (void)inner;
        (void)leaf;
        mm.set_scroll_content_h(outer, 400.0f);
        mm.set_scroll_content_h(inner, 400.0f);
        mm.hit_test(0.0f, 0.0f);
        assert(Near(mm.abs_y(inner), 40.0f));

        // Only the inner one scrolled: the leaf moves, the inner view does not.
        mm.set_scroll(inner, 100.0f);
        mm.hit_test(0.0f, 0.0f);
        assert(Near(mm.abs_y(inner), 40.0f));
        assert(Near(mm.abs_y(leaf), 140.0f)); // 40 + 200 - 100

        // Both scrolled: offsets ADD. scroll_offset_for sums the ancestor chain,
        // so a leaf inside two viewports is displaced by both.
        mm.set_scroll(outer, 30.0f);
        mm.hit_test(0.0f, 0.0f);
        assert(Near(mm.abs_y(inner), 10.0f)); // 40 - 30
        assert(Near(mm.abs_y(leaf), 110.0f)); // 40 + 200 - 100 - 30
        assert(Near(mm.scroll_ancestor_of(leaf), inner));
    }

    // ─── ScrollView content is out of the layout flow ───
    // Children are positioned in CONTENT coordinates, which may be far taller
    // than the window. A layout pass that stacked them from the padding would
    // destroy the arrangement, so both layout passes skip them.
    {
        ui::Manager mm;
        mm.init();
        u16 sv = mm.scrollview(0.0f, 0.0f, 200.0f, 100.0f, UINT16_MAX);
        u16 a  = mm.button(0.0f, 0.0f, 40.0f, 20.0f, "a", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, sv);
        u16 b  = mm.button(0.0f, 500.0f, 40.0f, 20.0f, "b", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn, sv);
        (void)a;
        (void)b;
        mm.set_layout(sv, 1 /* HBox */, 4, 4);
        mm.layout(r); // layout_subtree is private; layout() is the public entry
        // Both keep exactly the coordinates they were given: an HBox would have
        // stacked them side by side from the padding, and `b` asked for y=500 -
        // far outside a 100px window, which is the whole point of a viewport.
        assert(Near(mm.pool[b].frame.y, 500.0f));
        assert(Near(mm.pool[a].frame.y, 0.0f));
        assert(Near(mm.pool[a].frame.x, 0.0f));
    }

    // ─── A recycled slot does not inherit a LAYOUT ───────────────────
    // is_container() asks "is a layout attached?", which a stale layout_type
    // answers YES to - and every layout array is written by a SETTER, never by
    // a factory, so nothing else resets them. mm_07's D7 page puts layouts on
    // slots 3/7/11/15 and clear() hands the same slots straight back, so the
    // first accordion header button on the next page lands on slot 3 and
    // becomes a parent whose children get re-flowed from a dead layout.
    {
        m = ui::Manager{};
        m.init();
        u16 box = m.panel(0.0f, 0.0f, 500.0f, 60.0f, 0xFF222233);
        m.set_layout(box, 1 /* HBox */, 8, 6);
        m.set_layout_align(box, 2 /* End */, 1 /* Center */);
        m.set_size_pct(box, 60, 40);
        m.set_margin(box, 3, 4, 5, 6);
        m.clear();

        const u16 btn = m.button(0.0f, 0.0f, 120.0f, 30.0f, "plain", 0xFF3A3A3A, 0xFFFFFFFF, NoopBtn);
        assert(btn == box);          // the same slot came back
        assert(m.layout_type[btn] == 0);
        assert(!m.is_container(btn)); // the stale layout made it a parent
        assert(m.layout_pad[btn] == 0);
        assert(m.layout_spacing[btn] == 0);
        assert(m.layout_align[btn] == 0);
        assert(m.size_pct_w[btn] == 0);
        assert(m.size_pct_h[btn] == 0);
        assert(m.margin[btn][0] == 0 && m.margin[btn][1] == 0);
        assert(m.margin[btn][2] == 0 && m.margin[btn][3] == 0);

        // The observable half: give the recycled slot a child and lay out. With
        // a stale HBox the child is repositioned into a row from the dead
        // padding; with the reset it keeps the coordinates it asked for.
        u16 kid = m.button(0.0f, 0.0f, 40.0f, 20.0f, "kid", 0xFF444444, 0xFFFFFFFF, NoopBtn, btn);
        m.measure_dirty = false;
        m.layout(r);
        assert(Near(m.pool[kid].frame.x, 0.0f) && Near(m.pool[kid].frame.y, 0.0f));
    }

    return 0;
}

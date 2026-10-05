// Accordion tests: a header BUTTON plus a content panel, an animated drawn
// height, and a clip that has to reach everything nested inside.
//
// get_clip() is a pure function of the pool + the abs cache, so the whole clip
// story is testable headlessly - no Renderer, no frame. That matters here
// because the bug this file exists for was invisible in every screenshot until
// someone zoomed in: an OPEN section whose content was inset by the HEADER's
// proportional border width, so the body was 27pt narrower than the header and
// the first glyphs of anything near the left edge were cut. "Looks clipped" is
// exactly what a wrong clip looks like.
#include <cassert>
#include "core/mm_types.h"
#include <cmath>
#include <cstdio>

#include "../ui/mm_ui.hpp"

static bool Near(f32 a, f32 b, f32 eps = 0.01f) noexcept {
    return __builtin_fabsf(a - b) <= eps;
}

// A bordered style, which is what a header button has: the clip has to be
// resolved in the style's units (bw * frame.w), not in raw pixels.
static u8 bordered_style(ui::Manager &m, f32 border) {
    ui::WidgetStyle s{};
    s.border_color = 0xFF88AAFF;
    s.border_width = border;
    s.corner_r     = 0.12f;
    s.shape        = ui::shape_type::ROUNDED_RECT;
    return m.register_style(s);
}

int main() {
    // ─── The clip band is the header's FRAME, not its border band ───
    // The header's border exists to stop children painting over the header's own
    // border. The section body is NOT such a child: it hangs BELOW the header,
    // where those side borders are nowhere near it. Inheriting the inset made
    // the body 2 * bw * header_w narrower than the header it belongs to.
    {
        ui::Manager m;
        m.init();
        const u8 st = bordered_style(m, 0.04f);
        const u16 head = m.button(20.0f, 56.0f, 340.0f, 30.0f, "section", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX, 0.36f, st);
        const u16 body = m.panel(0.0f, 30.0f, 340.0f, 110.0f, 0xFF1D1D1D, head, m.register_style(ui::WidgetStyle{}));
        const u16 kid  = m.label(10.0f, 8.0f, "content lives here", 0xFFAAAAAA, 0.30f, body);
        m.accordion_attach(head, body, 110.0f, true);
        m.hit_test(0.0f, 0.0f); // rebuild the abs cache (the clip reads abs_x/y)

        i16  cx = 0, cy = 0;
        u16 cw = 0, ch = 0;
        assert(m.get_clip(kid, cx, cy, cw, ch));

        const f32 hx = m.abs_x(head);
        const f32 hy = m.abs_y(head);
        // Horizontal: the header's FRAME, not its border band. 0.04 * 340 = 13.6pt,
        // so the old behaviour started the band 13.6pt late and ended it 13.6pt
        // early - the body was 27pt narrower than the header it belongs to.
        // (The band's width is the intersection of the header's frame and the
        // body's, and the body is authored at x = 0 relative to the header.)
        assert(cx == static_cast<i16>(hx));
        assert(cw == 340);
        // Vertical: the top is the BODY's top (the body is a clipping container
        // too, so the intersection starts there) and the bottom is the header's
        // bottom plus the drawn height, which is the content height because
        // attach lands settled.
        assert(cy == static_cast<i16>(hy + 30.0f));
        assert(ch == 110);

        // The regression itself, stated as a fact about the child's own box: a
        // label 10pt from the body's left edge is fully inside the clip. At the
        // old inset its first 3.6pt were outside.
        const f32 kid_x0 = m.abs_x(kid);
        assert(kid_x0 >= cx && kid_x0 < cx + cw);
        assert(static_cast<f32>(cx) + static_cast<f32>(cw) >= m.abs_x(kid) + m.pool[kid].frame.w);
    }

    // ─── A child at x = 0 is not shaved by the header's border ───
    {
        ui::Manager m;
        m.init();
        const u8 st = bordered_style(m, 0.08f); // a fat border: 0.08 * 340 = 27pt
        const u16 head = m.button(20.0f, 56.0f, 340.0f, 30.0f, "section", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX, 0.36f, st);
        const u16 body = m.panel(0.0f, 30.0f, 340.0f, 110.0f, 0xFF1D1D1D, head, 0);
        const u16 kid  = m.label(0.0f, 0.0f, "flush", 0xFFAAAAAA, 0.30f, body);
        m.accordion_attach(head, body, 110.0f, true);
        m.hit_test(0.0f, 0.0f);

        i16  cx = 0, cy = 0;
        u16 cw = 0, ch = 0;
        assert(m.get_clip(kid, cx, cy, cw, ch));
        assert(cx == static_cast<i16>(m.abs_x(head)));
        assert(cw == 340);
        assert(m.is_visible(kid));
    }

    // ─── The band's height IS the drawn height, and it animates ───
    {
        ui::Manager m;
        m.init();
        const u16 head = m.button(20.0f, 0.0f, 300.0f, 30.0f, "section", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX);
        const u16 body = m.panel(0.0f, 30.0f, 300.0f, 120.0f, 0xFF1D1D1D, head, 0);
        const u16 kid  = m.label(8.0f, 8.0f, "inside", 0xFFAAAAAA, 0.30f, body);
        m.accordion_attach(head, body, 120.0f, true);
        m.hit_test(0.0f, 0.0f);

        i16  cx = 0, cy = 0;
        u16 cw = 0, ch = 0;
        assert(m.get_clip(kid, cx, cy, cw, ch));
        // Fully open, the band is the BODY's own rect (it is a clipping container
        // in its own right): the content height, starting at the body's top.
        assert(ch == 120);
        assert(cy == static_cast<i16>(m.abs_y(body)));
        assert(static_cast<f32>(cy) + static_cast<f32>(ch) == m.abs_y(head) + 30.0f + 120.0f);

        // Half-way: the clip follows the DRAWN height, so a collapsing section
        // reveals less of itself every frame instead of vanishing at the end.
        m.set_scroll_content_h(0, 0.0f); // no-op; keeps the optimiser honest
        m.accordion_set_open(head, false);
        for (int i = 0; i < 40 && !m.accordion_is_settled(head); ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.accordion_is_settled(head));
        assert(Near(m.accordion_drawn_h(head), 0.0f));
        assert(m.get_clip(kid, cx, cy, cw, ch));
        assert(ch == 0); // an EMPTY clip, which is NOT "no clip"
        assert(!ui::clip_draws(true, cw, ch));
        // ...and the content is hidden once there is nothing left to see.
        assert(!m.is_visible(kid));
    }

    // ─── Reopening brings it back (the flag only update() clears) ───
    {
        ui::Manager m;
        m.init();
        const u16 head = m.button(20.0f, 0.0f, 300.0f, 30.0f, "section", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX);
        const u16 body = m.panel(0.0f, 30.0f, 300.0f, 120.0f, 0xFF1D1D1D, head, 0);
        const u16 kid  = m.label(8.0f, 8.0f, "inside", 0xFFAAAAAA, 0.30f, body);
        m.accordion_attach(head, body, 120.0f, true);
        m.accordion_set_open(head, false);
        for (int i = 0; i < 60 && !m.accordion_is_settled(head); ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(!m.is_visible(kid));
        m.accordion_set_open(head, true);
        assert(m.is_visible(kid)); // set_visible is the reopen path's job
        for (int i = 0; i < 60 && !m.accordion_is_settled(head); ++i) {
            m.update(1.0f / 60.0f);
        }
        i16  cx = 0, cy = 0;
        u16 cw = 0, ch = 0;
        m.hit_test(0.0f, 0.0f);
        assert(m.get_clip(kid, cx, cy, cw, ch));
        assert(ch == 120);
        assert(cy == static_cast<i16>(m.abs_y(body)));
    }

    // ─── A section that starts CLOSED has an empty clip from frame one ───
    // attach lands settled, so drawn_h == 0 immediately and there is no frame on
    // which the subtree is briefly visible.
    {
        ui::Manager m;
        m.init();
        const u16 head = m.button(20.0f, 0.0f, 300.0f, 30.0f, "section", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX);
        const u16 body = m.panel(0.0f, 30.0f, 300.0f, 120.0f, 0xFF1D1D1D, head, 0);
        const u16 kid  = m.label(8.0f, 8.0f, "inside", 0xFFAAAAAA, 0.30f, body);
        m.accordion_attach(head, body, 120.0f, false);
        m.hit_test(0.0f, 0.0f);
        i16  cx = 0, cy = 0;
        u16 cw = 0, ch = 0;
        assert(m.get_clip(kid, cx, cy, cw, ch));
        assert(ch == 0);
        assert(!ui::clip_draws(true, cw, ch));
        m.update(1.0f / 60.0f); // one frame: still settled, still empty
        assert(m.accordion_is_settled(head));
        assert(!ui::clip_draws(true, cw, ch));
    }

    // ─── accordion_step is pure and dt == 0 is a no-op ───
    {
        assert(Near(ui::accordion_step(50.0f, 100.0f, 0.0f), 50.0f));
        assert(ui::accordion_step(50.0f, 0.0f, 0.0f) == 50.0f);
        // dt > 0 moves toward the target and never past it.
        f32 h = 0.0f;
        for (int i = 0; i < 200; ++i) {
            h = ui::accordion_step(h, 100.0f, 1.0f / 60.0f);
            assert(h <= 100.0f && h >= 0.0f);
        }
        assert(Near(h, 100.0f));
    }

    // ─── The header needs no callback: a tap toggles it ───
    {
        ui::Manager m;
        m.init();
        int fires = 0;
        const u16 head = m.button(20.0f, 0.0f, 300.0f, 30.0f, "section", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX);
        const u16 body = m.panel(0.0f, 30.0f, 300.0f, 120.0f, 0xFF1D1D1D, head, 0);
        m.accordion_attach(head, body, 120.0f, true);
        (void)fires;
        assert(m.is_accordion(head));
        assert(m.accordion_is_open(head));
        assert(!m.is_accordion(body)); // the content is not itself an accordion
        assert(m.accordion_parent_of(body) == head);
        assert(m.accordion_parent_of(head) == UINT16_MAX);
        assert(Near(m.accordion_content_h(head), 120.0f));
        m.accordion_toggle(head);
        assert(!m.accordion_is_open(head));
        m.accordion_toggle(head);
        assert(m.accordion_is_open(head));
    }

    printf("[accordion] all tests passed\n");
    return 0;
}
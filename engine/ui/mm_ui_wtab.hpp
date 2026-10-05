// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_ui_wtab.hpp — separator + tab strip (header-only).
// Included by mm_ui.hpp tail (Manager complete); direct inclusion also works
// via the mm_ui.hpp include below (pragma-once guarded, no cycle).
//
// A separator needs NO render code: Pass 1 already draws any visible widget
// with a bg_color, so the factory's frame IS the rule. What it does need is to
// stay OUT of the interaction path (pick() skips it by type) and out of the
// container test.
//
// The tab strip draws its own two quads (bar + active cell) and its labels,
// and skips the generic paths. Colours come from the Theme, not a
// WidgetStyle — see TabbarMetrics for why.
#pragma once
#include "mm_ui.hpp"
#include "core/mm_types.h"

namespace ui {
namespace tabbar {

// Metrics from the frame alone, refreshed every render like the listbox's.
inline TabbarMetrics compute(const Manager &m, u16 i, f32 line_h) noexcept {
    TabbarMetrics tm{0.0f, 0.0f, 0};
    if (i >= Manager::MAX || m.pool[i].type != (u8)widget_type::TABBAR) {
        return tm;
    }
    tm.count = m.tabbar_count_[i];
    if (tm.count > 0 && m.pool[i].frame.w > 0.0f) {
        tm.cell_w = m.pool[i].frame.w / static_cast<f32>(tm.count);
    }
    // Label scale from the CELL height, exactly like the listbox derives its
    // row text scale: a strip is sized by its height, so a fixed scale either
    // overflows a short strip or looks tiny in a tall one.
    if (line_h > 0.0f && m.pool[i].frame.h > 0.0f) {
        const f32 fit = (m.pool[i].frame.h * 0.62f) / line_h;
        tm.text_scale     = m.theme.font_scale * (fit < 1.0f ? fit : 1.0f);
    } else {
        tm.text_scale = m.theme.font_scale;
    }
    return tm;
}

// Pass 1.9: bar background + the active cell's fill, one plain flush.
inline void render_bg(Manager &m, u16 i, Renderer &r, SpriteBatch &batch) noexcept {
    if (i >= Manager::MAX || m.pool[i].type != (u8)widget_type::TABBAR) {
        return;
    }
    const auto  &w   = m.pool[i];
    const TabbarMetrics tm = m.tabbar_metrics[i];
    const f32  ax  = m.abs_x(i);
    const f32  ay  = m.abs_y(i);

    // Clip like every other per-widget pass. The strip draws its own primitive,
    // so nothing else clips it for us - inside a ScrollView or a collapsed
    // section it would paint straight over the container's border. An empty clip
    // means draw nothing (a zero-size scissor is invalid at the backend).
    i16  cx, cy;
    u16 cw, ch;
    if (m.get_clip(i, cx, cy, cw, ch)) {
        if (!clip_draws(true, cw, ch)) {
            return;
        }
        r.set_scissor(cx, cy, cw, ch);
    }
    // Label colour rides the cell state, so the fg lives on the widget.
    // SpriteBatch::add takes the quad CENTRE, not its top-left (every Pass 1
    // call site adds w * 0.5) - a left-edge origin silently shifts the quad
    // up-left by half its size.
    const f32 ccx = ax + w.frame.w * 0.5f;
    const f32 ccy = ay + w.frame.h * 0.5f;
    // Drain first: this pass owns a flush, so anything still queued belongs to an
    // EARLIER pass and must be submitted before we reset.
    batch.reset();
    batch.add(ccx, ccy, w.frame.w, w.frame.h, 0.0f, w.bg_color, 0);
    const int act = m.tabbar_active[i];
    if (tm.count > 0 && act >= 0 && act < tm.count) {
        batch.add(ax + (static_cast<f32>(act) + 0.5f) * tm.cell_w, ccy, tm.cell_w, w.frame.h, 0.0f, m.theme.tab_active_bg, 0);
    }
    r.flush_sprites(batch, r.white_tex, r.default_sampler);
    // ...and reset AFTER it. flush_sprites_impl early-returns on count == 0 and
    // never resets, so leaving these two quads in the batch let the NEXT pass
    // submit them again - through its own pipeline, under its own scissor.
    batch.reset();
}

// Pass 3: one centred label per cell. Drawn from the module (not the shared
// chain) because a cell's text position depends on the cell grid.
inline void render_text(Manager &m, u16 i, Renderer &r) noexcept {
    if (i >= Manager::MAX || m.pool[i].type != (u8)widget_type::TABBAR) {
        return;
    }
    const auto  &w   = m.pool[i];
    const TabbarMetrics tm = m.tabbar_metrics[i];
    if (tm.count == 0 || tm.cell_w <= 0.0f || m.tabbar_items[i] == nullptr) {
        return;
    }
    const f32 ax   = m.abs_x(i);
    const f32  ay   = m.abs_y(i);
    for (int c = 0; c < tm.count; ++c) {
        const char *s = m.tabbar_items[i][c];
        if (s == nullptr || s[0] == '\0') {
            continue;
        }
        f32 tw = 0.0f, th = 0.0f;
        r.measure_text(r.default_font, s, tm.text_scale, tw, th);
        const f32 cell_x = ax + static_cast<f32>(c) * tm.cell_w;
        // measure_text's out_h is the LINE box (it reserves descender space),
        // so centring on it sits text high - centre on the ascent instead,
        // the same rule the button label uses.
        const f32 asc = th - r.default_font.line_height * tm.text_scale;
        const f32 tx  = align_text_x(text_align::CENTER, cell_x, tm.cell_w, tw);
        const f32 ty  = ay + w.frame.h * 0.5f - asc * 0.5f;
        const bool  act = (c == m.tabbar_active[i]);
        r.draw_text(r.default_font, s, tx, ty, act ? m.theme.tab_active_fg : w.text_color, tm.text_scale);
    }
}

} // namespace tabbar
} // namespace ui
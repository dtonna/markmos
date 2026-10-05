// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_ui_wtoggle.hpp — toggle / checkbox / radiobox family (header-only).
// Included by mm_ui.hpp tail (Manager complete).
#pragma once
#include "mm_ui.hpp"

namespace ui {
namespace togglebox {

inline void draw_line(SpriteBatch &batch, f32 x1, f32 y1, f32 x2, f32 y2, f32 thickness, u32 color) noexcept {
    f32 dx  = x2 - x1;
    f32 dy  = y2 - y1;
    f32 len = __builtin_sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) {
        return;
    }
    f32 angle = __builtin_atan2f(dy, dx);
    f32 cx    = (x1 + x2) * 0.5f;
    f32 cy    = (y1 + y2) * 0.5f;
    // Radius 0.5 = a capsule: a px-art checkmark with square-cut ends reads as
    // broken at 2x, and the SDF mask is free once the batch is on the rounded
    // pipeline anyway.
    batch.add(cx, cy, len, thickness, angle, color, 0, 0, 0.5f);
}

// ── Pass 1.75: checkbox rounded rects + radiobox circles (batched, single flush) ──
// Borders are style-driven (no hardcoded widths): checkbox uses the
// uniform border resolve + style corner_r; radio uses the ring resolve.
// A style with no border at all draws none (border width 0 = none).
inline void render_pass(Manager &m, SpriteBatch &batch, Renderer &r) noexcept {
bool has_rounded = false;
    // Effective visibility, never the own flag. A collapsed Accordion clears its
    // CONTENT's WF_VISIBLE and not its descendants', so this pass used to leave
    // the checkboxes and radios of a closed section floating on the page. Every
    // other pass already reads is_visible() - this one was the last holdout.
    //
    // Clip is part of the batch identity, for the same reason as Pass 1 / 1.8 /
    // 2: a run of boxes scissored to one rect and flushed at the end means only
    // the LAST rect applies to the whole batch. Leaving a clipped run has to
    // restore the full frame, because the previous scissor is still the active
    // one - the caller restores after this returns, too.
    bool    clipped = false;
    i16 ccx = 0, ccy = 0;
    u16 ccw = 0, cch = 0;
for (u16 i = 0; i < m.count; ++i) {
    auto &w = m.pool[i];
    if (!m.is_visible(i)) {
        continue;
    }
    if (w.type != (u8)widget_type::CHECKBOX && w.type != (u8)widget_type::RADIOBOX) {
        continue;
    }

    i16  ncx, ncy;
    u16 ncw, nch;
    const bool nclipped = m.get_clip(i, ncx, ncy, ncw, nch);
    // An empty clip means draw NOTHING. Skipping is the only correct answer: a
    // zero-size scissor is invalid at the backend and passing it through has an
    // undefined outcome (this pass's box vanished under one while Pass 2's
    // checkmark survived). Skipping BEFORE the flush decision also keeps the
    // batch bookkeeping honest - a widget that draws nothing leaves the previous
    // widget's scissor in force, which is exactly right.
    if (!clip_draws(nclipped, ncw, nch)) {
        continue;
    }
    if (nclipped != clipped || (nclipped && (ncx != ccx || ncy != ccy || ncw != ccw || nch != cch))) {
        if (has_rounded) {
            r.flush_rounded_sprites(batch);
            batch.reset();
            has_rounded = false;
        }
        if (nclipped) {
            r.set_scissor(ncx, ncy, ncw, nch);
        } else if (clipped) {
            r.set_scissor(0, 0, static_cast<u16>(r.width), static_cast<u16>(r.height));
        }
        clipped = nclipped;
        ccx = ncx;
        ccy = ncy;
        ccw = ncw;
        cch = nch;
    }

    f32              ax       = m.abs_x(i);
    f32              ay       = m.abs_y(i);
    f32              ps       = w.press_scale;
    f32              s        = w.frame.w * ps;
    f32              ccx2     = ax + w.frame.w * 0.5f;
    f32              ccy2     = ay + w.frame.h * 0.5f;
    const WidgetStyle &sty      = m.styles[w.style_id];
    // State cross-fade: the box colour travels off->on so a tap reads as a
    // fade rather than a pop. Lerping the packed colour (not alpha) keeps the
    // border-width logic below unchanged.
    f32             fade     = m.state_fade[i];
    u32          fill_col = ui_lerp_color(m.off_color[i], m.on_color[i], fade);
    // Effective enabled, like the button fill in Pass 1: a box inside a disabled
    // panel is unclickable, so it must not look clickable. This pass had no
    // WF_ENABLED read anywhere, so a disabled one drew at full brightness.
    const bool enabled = m.is_enabled(i);
    if (!enabled) {
        fill_col = ui_darken(fill_col, 80);
    }
    f32              norm_radius, norm_border;
    if (w.type == (u8)widget_type::CHECKBOX) {
        f32    sw[4];
        u32 sc[4];
        resolve_border_sides(sty, sw, sc);
        norm_border = sw[0];
        u32 bc = sc[0];
        for (int k = 1; k < 4; ++k) {
            if (sw[k] > norm_border) {
                norm_border = sw[k];
            }
            if (!bc && sc[k]) {
                bc = sc[k];
            }
        }
        norm_radius = sty.corner_r;
        if (norm_radius < 0.0f) {
            norm_radius = 0.0f;
        }
        if (norm_radius > 0.5f) {
            norm_radius = 0.5f;
        }
        // Border follows the fade too, or the outline pops to full white while
        // the fill is still travelling.
        u32 bc_on  = (bc != 0) ? ui_lighten(bc, 20) : ui_lighten(m.on_color[i], 20);
        u32 bc_off = (bc != 0) ? bc : m.theme.checkbox_border;
        if (norm_border > 0.0f) {
            bc = ui_lerp_color(bc_off, bc_on, fade);
        }
        if (!enabled) {
            bc = ui_darken(bc, 80);
        }
        batch.add(ccx2, ccy2, s, s, 0.0f, fill_col, 0, 0, norm_radius, norm_border, bc);
    } else {
        // Radio: outer circle (0.5 radius) + white inner dot when selected.
        f32    rw;
        u32 rc;
        resolve_ring(sty, rw, rc);
        u32 rc_on  = (rc != 0) ? ui_lighten(rc, 20) : ui_lighten(m.on_color[i], 20);
        u32 rc_off = (rc != 0) ? rc : m.theme.checkbox_border;
        if (rw > 0.0f) {
            rc = ui_lerp_color(rc_off, rc_on, fade);
        }
        if (!enabled) {
            rc = ui_darken(rc, 80);
        }
        batch.add(ccx2, ccy2, s, s, 0.0f, fill_col, 0, 0, 0.5f, rw, rc);
        // Dot fades in with the state (alpha, which the sprite pipeline already
        // honours) instead of appearing on the frame the tap lands.
        if (fade > 0.001f) {
            f32 ds = s * 0.45f;
            const u32 dot = enabled ? ui_alpha(0xFFFFFFFFu, fade) : ui_darken(ui_alpha(0xFFFFFFFFu, fade), 80);
            batch.add(ccx2, ccy2, ds, ds, 0.0f, dot, 0, 0, 0.5f, 0.0f, 0);
        }
    }
    has_rounded = true;
}
if (has_rounded) {
    r.flush_rounded_sprites(batch);
    batch.reset();
}
}

// Checkmark for a selected checkbox (Pass 2). Tuned px art for the 28px box;
// the center follows the frame.
inline void render_check(Manager &m, u16 i, f32 ax, f32 ay, SpriteBatch &batch) noexcept {
    auto &w = m.pool[i];
    // ── Checkmark (box already drawn in pass 1.75) ───────────
    // Check proportions are tuned px art for the 28px box; the center
    // follows the frame so a resized box keeps the mark centered.
    if (w.type == (u8)widget_type::CHECKBOX && m.state_fade[i] > 0.001f) {
        f32 ps  = w.press_scale;
        f32 ccx = ax + w.frame.w * 0.5f;
        f32 ccy = ay + w.frame.h * 0.5f;
        f32 x1 = ccx + (-8.0f) * ps, y1 = ccy + 2.0f * ps;
        f32 x2 = ccx + (-3.0f) * ps, y2 = ccy + 8.0f * ps;
        f32 x3 = ccx + 9.0f * ps, y3 = ccy + (-7.0f) * ps;
        u32 ink = ui_alpha(m.theme.checkbox_check, m.state_fade[i]);
        draw_line(batch, x1, y1, x2, y2, 2.5f * ps, ink);
        draw_line(batch, x2, y2, x3, y3, 2.5f * ps, ink);
    }
}

} // namespace togglebox

// ─── toggle / checkbox / radiobox factories (moved from mm_ui.hpp, byte-identical) ───

inline u16 Manager::toggle(f32 x, f32 y, f32 w, f32 h, const char *text, u32 bg_on, u32 bg_off, u32 fg, bool initial,
                                ClickCallback cb, u16 parent, u8 style_id, f32 scale) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg              = pool[id];
    wg.frame.x            = x;
    wg.frame.y            = y;
    wg.frame.w            = w;
    wg.frame.h            = h;
    wg.scale              = scale; // the LABEL scale (see the decl); the box is fixed 28pt
    wg.bg_color           = theme.toggle_track_off;
    wg.text_color         = fg;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent                    = parent;
    wg.type                      = (u8)widget_type::TOGGLE;
    wg.flags                     = WF_VISIBLE | WF_ENABLED | WF_FOCUSABLE;
    wg.state                     = initial ? 1 : 0;
    wg.style_id                  = style_id;
    wg.shape                     = 0;
    wg.pos_mode                  = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt                 = mm_math::anchor::TOP_LEFT;
    wg.on_click                  = cb;
    wg.on_draw                   = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    on_color[id]                 = bg_on;
    off_color[id]                = bg_off;
    std::memcpy(wg.pad, theme.toggle_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

inline u16 Manager::checkbox(f32 x, f32 y, const char *text, u32 on_c, u32 off_c, u32 fg, bool initial, ClickCallback cb,
                                  u16 parent, u8 style_id, f32 scale) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg              = pool[id];
    wg.frame.x            = x;
    wg.frame.y            = y;
    wg.frame.w            = 28.0f;
    wg.frame.h            = 28.0f;
    wg.scale              = scale; // the LABEL scale (see the decl); the box is fixed 28pt
    wg.bg_color           = initial ? on_c : off_c;
    wg.text_color         = fg;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent                    = parent;
    wg.type                      = (u8)widget_type::CHECKBOX;
    wg.flags                     = WF_VISIBLE | WF_ENABLED | WF_FOCUSABLE;
    wg.state                     = initial ? 1 : 0;
    wg.style_id                  = style_id;
    wg.shape                     = 0;
    wg.pos_mode                  = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt                 = mm_math::anchor::TOP_LEFT;
    wg.on_click                  = cb;
    wg.on_draw                   = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    on_color[id]                 = on_c;
    off_color[id]                = off_c;
    state_fade[id]               = wg.state ? 1.0f : 0.0f;
    std::memcpy(wg.pad, theme.checkbox_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

inline u16 Manager::radiobox(f32 x, f32 y, const char *text, u32 on_c, u32 off_c, u32 fg, u8 group, bool initial,
                                  ClickCallback cb, u16 parent, u8 style_id, f32 scale) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg              = pool[id];
    wg.frame.x            = x;
    wg.frame.y            = y;
    wg.frame.w            = 28.0f;
    wg.frame.h            = 28.0f;
    wg.scale              = scale; // the LABEL scale (see the decl); the box is fixed 28pt
    wg.bg_color           = initial ? on_c : off_c;
    wg.text_color         = fg;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent                    = parent;
    wg.type                      = (u8)widget_type::RADIOBOX;
    wg.flags                     = WF_VISIBLE | WF_ENABLED | WF_FOCUSABLE;
    wg.state                     = 0;
    wg.style_id                  = style_id;
    wg.shape                     = 0;
    wg.pos_mode                  = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt                 = mm_math::anchor::TOP_LEFT;
    wg.on_click                  = cb;
    wg.on_draw                   = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    radio_group[id]              = group;
    on_color[id]                 = on_c;
    off_color[id]                = off_c;
    // radiobox_select() below applies the initial selection; keep the fade
    // in sync so a freshly built group never starts mid-transition.
    state_fade[id]               = 0.0f;
    std::memcpy(wg.pad, theme.checkbox_pad, sizeof(wg.pad)); // no theme radio pad; reuse checkbox pad
    if (initial) {
        radiobox_select(id); // last-initial-wins within the group
        state_fade[id] = 1.0f; // snap, don't fade in on the first frame of a group
    }
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

// Select a radio: clear the rest of its group, set this one.
// Caller fires on_click, and only on actual change (re-tapping the
// selected radio is a silent no-op). Visible-guard: recycled freelist
// slots keep stale type/group/state until a factory overwrites them.
inline void Manager::radiobox_select(u16 id) noexcept {
    if (id >= MAX) {
        return;
    }
    u8 g = radio_group[id];
    for (u16 i = 0; i < count; ++i) {
        if (i != id && (pool[i].flags & WF_VISIBLE) && pool[i].type == (u8)widget_type::RADIOBOX && radio_group[i] == g) {
            pool[i].state = 0;
        }
    }
    pool[id].state = 1;
}

} // namespace ui

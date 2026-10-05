// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_ui_wslider.hpp — slider family (header-only).
// Included by mm_ui.hpp tail (Manager complete).
#pragma once
#include "mm_ui.hpp"
#include <cstdio> // snprintf for the percent label

namespace ui {
namespace slider {

// Slider drag update (cut from Manager::handle — member accesses via m).
inline void drag_update(Manager &m, u16 id, f32 px) noexcept {
    if (m.pool[id].type != (u8)widget_type::SLIDER) {
        return;
    }
    f32 x0 = 0.0f, x1 = 0.0f;
    content_box(m.styles[m.pool[id].style_id], m.abs_x(id), m.pool[id].frame.w, x0, x1);
    f32 rel = (x1 > x0) ? (px - x0) / (x1 - x0) : 0.0f;
    if (rel < 0.0f) {
        rel = 0.0f;
    }
    if (rel > 1.0f) {
        rel = 1.0f;
    }
    m.slider_value[id] = rel;
    m.emit_event(ui_event_type::CHANGE, id, rel);
    if (m.on_change[id]) {
        m.on_change[id](id, rel);
    }
    m.hot = id;
}

// Slider bar + thumb (cut from Manager::render Pass 2).
// Type guard lives here (not at the call site) so the helper is safe
// to call for any widget id — without it, slider_value garbage from
// non-slider widgets draws wild fills (this bit us once: yellow blocks).
inline void render_parts(Manager &m, u16 i, f32 ax, f32 ay, SpriteBatch &batch, Renderer &r) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::SLIDER) {
        return;
    }
    f32 val = m.slider_value[i];
    f32 x0 = 0.0f, x1 = 0.0f;
    content_box(m.styles[w.style_id], ax, w.frame.w, x0, x1);
    if (x1 <= x0) {
        return;
    }
    f32 span     = x1 - x0;
    f32 track_h  = w.frame.h * 0.4f;
    f32 track_cx = (x0 + x1) * 0.5f; // track lives inside the border caps
    f32 track_cy = ay + w.frame.h * 0.5f; // vertically centered in slider

    // Track, fill and thumb all take the STYLE's corner radius: the frame they
    // sit inside is rounded in Pass 1 with the same number, and a square track
    // inside a rounded frame is what "the radius does not round" looks like.
    const f32 cr = style_corner_radius(m.styles[w.style_id], w.shape);
    batch.add(track_cx, track_cy, span, track_h, 0.0f, m.theme.slider_track, 0, 0, cr);

    // Thumb geometry FIRST, because the fill runs UNDER it: ending the fill
    // exactly at the thumb's left edge left two rounded caps touching, which
    // reads as a beaded seam instead of one continuous line (the fill's right
    // cap + the thumb's left cap = a pinch between them). Running the fill to
    // the thumb's CENTRE hides that cap behind the thumb, so the bar reads as
    // one piece while both of the track's ends stay round.
    f32 thumb_w  = w.frame.h * 0.5f;
    f32 thumb_h  = w.frame.h * 0.7f;
    f32 thumb_lx = x0 + span * val;
    if (thumb_lx < x0) {
        thumb_lx = x0;
    }
    if (thumb_lx + thumb_w > x1) {
        thumb_lx = x1 - thumb_w;
    }
    if (val > 0.01f) {
        f32 fill_r  = thumb_lx + thumb_w * 0.5f; // under the thumb
        f32 fill_l  = x0;
        if (fill_r - fill_l > 0.5f) {
            batch.add((fill_l + fill_r) * 0.5f, track_cy, fill_r - fill_l, track_h, 0.0f, m.theme.slider_fill, 0, 0, cr);
        }
    }
    f32    thumb_cy    = ay + w.frame.h * 0.5f;
    u32 thumb_color = (i == m.focus_id || i == m.hot) ? m.theme.slider_thumb_hot : m.theme.slider_thumb;
    batch.add(thumb_lx + thumb_w * 0.5f, thumb_cy, thumb_w, thumb_h, 0.0f, thumb_color, 0, 0, cr);
    // Textured thumb (WidgetStyle::thumb_tex): own flush, same reasoning as
    // the toggle thumb - one primitive, so don't split the whole Pass 2.
    TextureHandle ttex = m.styles[w.style_id].thumb_tex;
    if (ttex.handle.id != 0) {
        // Rounded pipeline: the SDF mask is a no-op at radius 0, so the bitmap
        // thumb is untouched while the tracks already queued keep their corners.
        r.flush_rounded_sprites(batch, ttex, r.default_sampler);
        batch.reset();
    }
}

// Slider percent label (cut from Manager::render Pass 3; draws directly).
inline void render_text(Manager &m, u16 i, f32 ax, f32 ay, f32 pad_r, f32 pad_t, f32 pad_b, f32 a, f32 hc,
                        Renderer &r) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::SLIDER) {
        return;
    }
    char buf[16];
    int pct = static_cast<int>(m.slider_value[i] * 100.0f + 0.5f);
    int len = snprintf(buf, sizeof(buf), "%d%%", pct);
    if (len > 0) {
        // Position + scale come from the shared helpers so this label cannot
        // spill out of its container (see Manager::value_label_x).
        const f32 sc    = m.value_label_scale(r, w.frame.h, pad_t, pad_b);
        f32       tw    = 0.0f, th = 0.0f;
        r.measure_text(r.default_font, buf, sc, tw, th);
        const f32 txt_x = m.value_label_x(i, ax, w.frame.w, tw);
        f32       txt_y = ay + pad_t + (w.frame.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
        r.draw_text(r.default_font, buf, txt_x, txt_y, w.text_color ? w.text_color : m.theme.slider_text, sc);
    }
}

} // namespace slider

// ─── slider factory (moved from mm_ui.hpp, byte-identical) ───

inline u16 Manager::slider(f32 x, f32 y, f32 w, f32 h, f32 initial, ChangeCallback cb, u16 parent, u8 style_id) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg              = pool[id];
    wg.frame.x            = x;
    wg.frame.y            = y;
    wg.frame.w            = w;
    wg.frame.h            = h;
    wg.scale              = 1.0f;
    wg.bg_color           = theme.slider_bg;
    wg.text_color         = 0;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    wg.text[0]            = '\0';
    wg.parent             = parent;
    wg.type               = (u8)widget_type::SLIDER;
    wg.flags              = WF_VISIBLE | WF_ENABLED | WF_FOCUSABLE;
    wg.state              = 0;
    wg.style_id           = style_id;
    wg.shape              = 0;
    wg.pos_mode           = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt          = mm_math::anchor::TOP_LEFT;
    wg.on_click           = nullptr;
    wg.on_draw            = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    slider_value[id]      = initial;
    on_change[id]         = cb;
    std::memcpy(wg.pad, theme.slider_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

} // namespace ui

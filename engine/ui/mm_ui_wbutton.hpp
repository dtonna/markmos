// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_ui_wbutton.hpp — button family (header-only).
// Included by mm_ui.hpp tail (Manager complete).
#pragma once
#include "mm_ui.hpp"

namespace ui {
namespace button {

// Button label (cut from Manager::render Pass 3; draws directly).
inline void render_text(Manager &m, u16 i, f32 ax, f32 ay, f32 pad_l, f32 pad_r, f32 pad_t, f32 pad_b, f32 a, f32 hc,
                        Renderer &r) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::BUTTON) {
        return;
    }
    // The label lives in the SAME rect the fill is drawn in, and Pass 1 scales
    // that rect about the centre while pressed - so the label has to scale with
    // it or it stays full-size at the unscaled position and drifts out of the
    // shrinking pill (which is what "the row shrank and the text slid off it"
    // looked like on the ScrollView page). Padding scales with the box: it is a
    // fraction of the same visual element.
    const f32 ps      = w.press_scale;
    const f32 bx      = ax + w.frame.w * (1.0f - ps) * 0.5f;
    const f32 by      = ay + w.frame.h * (1.0f - ps) * 0.5f;
    const f32 bw      = w.frame.w * ps;
    const f32 bh      = w.frame.h * ps;
    const f32 pad_ls  = pad_l * ps;
    const f32 pad_rs  = pad_r * ps;
    const f32 pad_ts  = pad_t * ps;
    const f32 pad_bs  = pad_b * ps;
    // A button draws its label UNCLIPPED, so anything wider or taller than the
    // padded content box used to spill symmetrically out of the frame (freecell
    // menu at scale 1.0 was the loud case). Shrink to fit instead of spilling:
    // both axes are linear in scale, so one ratio each, measured once.
    const f32 inner_w = bw - pad_ls - pad_rs;
    const f32 inner_h = bh - pad_ts - pad_bs;
    if (inner_w <= 0.0f || inner_h <= 0.0f) {
        return; // frame smaller than its own padding: nothing sensible to draw
    }
    const f32 text_w = static_cast<f32>(m.content_w[i]);
    f32       fit    = 1.0f;
    if (text_w > inner_w && text_w > 0.0f) {
        fit = inner_w / text_w;
    }
    if (hc > inner_h && hc > 0.0f) {
        fit = fminf(fit, inner_h / hc);
    }
    // Centring stays exact: x and y must use the SAME clamped scale, otherwise
    // the fit clamp would itself shift the label off-centre.
    const f32 scale = w.scale * fit * ps;
    const f32 tw    = text_w * fit;
    const f32 ta    = a * fit;
    const f32 th    = hc * fit;
    const f32 txt_x = bx + pad_ls + (inner_w - tw) * 0.5f;
    const f32 txt_y = by + pad_ts + (inner_h + 2.0f * ta - th) * 0.5f;
    u32    tc    = w.text_color;
    if (w.state == (u8)btn_state::PRESSED) {
        tc = ui_darken(tc, 40);
    }
    // Button labels stay single-line by design (a two-line button is a sign the
    // label should be a label), but they still honour a long-text override.
    r.draw_text(r.default_font, m.text_of(i), txt_x, txt_y, tc, scale);
}

} // namespace button

// ─── button factory (moved from mm_ui.hpp, byte-identical) ───

inline u16 Manager::button(f32 x, f32 y, f32 w, f32 h, const char *text, u32 bg, u32 fg, ClickCallback cb, u16 parent,
                                f32 scale, u8 style_id, u8 shape, DrawCallback on_draw, void *user) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg              = pool[id];
    wg.frame.x            = x;
    wg.frame.y            = y;
    wg.frame.w            = w;
    wg.frame.h            = h;
    wg.scale              = scale;
    wg.bg_color           = bg;
    wg.text_color         = fg;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent                    = parent;
    wg.type                      = (u8)widget_type::BUTTON;
    // Focusable: Tab walks buttons like every other control, and Confirm (Enter
    // / Space) fires the focused one. It was NOT focusable before, which left
    // the toolkit with no keyboard path to ANY button - set_focusable(id, false)
    // opts a widget back out (a decorative hit target, or an app that drives
    // its own key map and does not want Tab order to change).
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
    // Optional custom visuals + owner (default: plain themed button, no owner).
    // Lets callers finish setup in one call instead of poking pool[] after.
    if (shape != 0) {
        wg.shape = shape;
    }
    if (on_draw != nullptr) {
        wg.on_draw    = on_draw;
        draw_user[id] = user;
    }
    if (cb != nullptr) {
        click_user[id] = user;
    }
    std::memcpy(wg.pad, theme.button_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

} // namespace ui

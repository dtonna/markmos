// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_ui_wtext.hpp — label / image / panel / textfield family (header-only).
// Included by mm_ui.hpp tail (Manager complete); direct inclusion also works
// via the mm_ui.hpp include below (pragma-once guarded, no cycle).
#pragma once
#include "mm_ui.hpp"

namespace ui {
namespace textfield {

// TextField cursor (cut from Manager::render Pass 2 — byte-identical logic).
inline void render_cursor(Manager &m, u16 i, f32 ax, f32 ay, Renderer &r, SpriteBatch &batch) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::TEXT_FIELD) {
        return;
    }
    f32       cursor_x = ax + static_cast<f32>(w.pad[3]) + 8.0f;
    u32    len      = static_cast<u32>(std::strlen(w.text));
    Utf8Decoder dec(w.text, len);
    u8     pos = 0;
    while (pos < m.cursor_pos[i]) {
        u32 cp = dec.next();
        if (cp == 0) {
            break;
        }
        const GlyphInfo *g = resolve_glyph(r.default_font, cp);
        if (g != nullptr) {
            cursor_x += static_cast<f32>(g->advance) * w.scale;
        }
        ++pos;
    }
    // Clamp into the field's inner box. The caret walks the text's advance, so
    // once the value is longer than the field it lands outside the box - and it
    // was drawn with no scissor, i.e. floating over the neighbouring widget.
    // (The TEXT still clips; only the caret needed its own bound.)
    const f32 inner_l = ax + static_cast<f32>(w.pad[3]);
    const f32 inner_r = ax + w.frame.w - static_cast<f32>(w.pad[1]);
    const f32 caret_w = 2.0f;
    if (cursor_x + caret_w > inner_r) {
        cursor_x = inner_r - caret_w;
    }
    if (cursor_x < inner_l) {
        cursor_x = inner_l;
    }
    f32 cursor_y = ay + static_cast<f32>(w.pad[0]) + 6.0f;
    f32 cursor_h = 20.0f * w.scale;
    batch.add(cursor_x + 1.0f, cursor_y + cursor_h * 0.5f, 2.0f, cursor_h, 0.0f, m.theme.cursor, 0);
}

} // namespace textfield

// ─── label / image / panel / textfield factories (moved from mm_ui.hpp, byte-identical) ───

inline u16 Manager::panel(f32 x, f32 y, f32 w, f32 h, u32 color, u16 parent, u8 style_id) noexcept {
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
    wg.bg_color           = color;
    wg.text_color         = 0;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    wg.text[0]            = '\0';
    wg.parent             = parent;
    wg.type               = (u8)widget_type::PANEL;
    // WF_ENABLED too: is_enabled() derives effective state from the whole
    // ancestor chain, so a container without it would read as permanently
    // disabled and gate every child.
    wg.flags              = WF_VISIBLE | WF_ENABLED;
    wg.state              = 0;
    wg.style_id           = style_id;
    wg.shape              = 0;
    wg.pos_mode           = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt          = mm_math::anchor::TOP_LEFT;
    wg.on_click           = nullptr;
    wg.on_draw            = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    std::memcpy(wg.pad, theme.panel_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

inline u16 Manager::label(f32 x, f32 y, const char *text, u32 color, f32 scale, u16 parent, u8 style_id) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg              = pool[id];
    wg.frame.x            = x;
    wg.frame.y            = y;
    wg.frame.w            = 0;
    wg.frame.h            = 0;
    wg.scale              = scale;
    wg.bg_color           = 0;
    wg.text_color         = color;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent                    = parent;
    wg.type                      = (u8)widget_type::LABEL;
    wg.flags                     = WF_VISIBLE | WF_ENABLED | WF_AUTO_W | WF_AUTO_H;
    wg.state                     = 0;
    wg.style_id                  = style_id;
    wg.shape                     = 0;
    wg.pos_mode                  = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt                 = mm_math::anchor::TOP_LEFT;
    wg.on_click                  = nullptr;
    wg.on_draw                   = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    std::memcpy(wg.pad, theme.label_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id]     = theme.label_align;
    text_box[id]  = 0;
    return id;
}

inline u16 Manager::textfield(f32 x, f32 y, f32 w, f32 h, const char *initial_text, u32 bg, u32 fg, u16 parent,
                                   u8 style_id, f32 scale) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg              = pool[id];
    wg.frame.x            = x;
    wg.frame.y            = y;
    wg.frame.w            = w;
    wg.frame.h            = h;
    wg.scale              = scale; // the VALUE scale (see the decl); the field is a fixed box
    wg.bg_color           = bg;
    wg.text_color         = fg;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    std::strncpy(wg.text, initial_text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent                    = parent;
    wg.type                      = (u8)widget_type::TEXT_FIELD;
    wg.flags                     = WF_VISIBLE | WF_ENABLED | WF_FOCUSABLE;
    wg.state                     = 0;
    wg.style_id                  = style_id;
    wg.shape                     = 0;
    wg.pos_mode                  = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt                 = mm_math::anchor::TOP_LEFT;
    wg.on_click                  = nullptr;
    wg.on_draw                   = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    cursor_pos[id]               = static_cast<u8>(std::strlen(wg.text));
    std::memcpy(wg.pad, theme.textfield_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

inline u16 Manager::image(f32 x, f32 y, f32 w, f32 h, u16 parent, u8 style_id) noexcept {
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
    wg.bg_color           = 0xFFFFFFFF; // white tint = show texture as-is
    wg.text_color         = 0;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    wg.text[0]            = '\0';
    wg.parent             = parent;
    wg.type               = (u8)widget_type::IMAGE;
    wg.flags              = WF_VISIBLE | WF_ENABLED;
    wg.state              = 0;
    wg.style_id           = style_id;
    wg.shape              = 0;
    wg.pos_mode           = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt          = mm_math::anchor::TOP_LEFT;
    wg.pad[0] = wg.pad[1] = wg.pad[2] = wg.pad[3] = 0; // image has no theme pad
    wg.on_click                                   = nullptr;
    wg.on_draw                                    = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    measure_dirty                                 = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

} // namespace ui

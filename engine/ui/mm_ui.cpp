// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_ui.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <simdjson.h>

// simdjson header-only implementation
#include "../../thirdparty/simdjson/singleheader/simdjson.cpp"

namespace ui {

void Manager::handle(const InputState &input) noexcept {
    clicked = UINT16_MAX;

    // ── TextField editing input ────────────────────────────────
    if (editing_id != UINT16_MAX) {
        auto &w = pool[editing_id];

        // Character input
        for (uint8_t i = 0; i < input.text_count; ++i) {
            char    c   = input.text_input[i];
            uint8_t len = static_cast<uint8_t>(std::strlen(w.text));
            if (len >= sizeof(w.text) - 1) {
                continue;
            }
            // Shift right from cursor
            for (uint8_t j = len + 1; j > cursor_pos[editing_id]; --j) {
                w.text[j] = w.text[j - 1];
            }
            w.text[cursor_pos[editing_id]] = c;
            cursor_pos[editing_id]++;
        }

        // Backspace
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Backspace)]) {
            if (cursor_pos[editing_id] > 0) {
                cursor_pos[editing_id]--;
                uint8_t len = static_cast<uint8_t>(std::strlen(w.text));
                for (uint8_t j = cursor_pos[editing_id]; j < len; ++j) {
                    w.text[j] = w.text[j + 1];
                }
            }
        }

        // Left arrow
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Left)]) {
            if (cursor_pos[editing_id] > 0) {
                cursor_pos[editing_id]--;
            }
        }
        // Right arrow
        if (input.keys_just_pressed[static_cast<size_t>(KeyCode::Right)]) {
            uint8_t len = static_cast<uint8_t>(std::strlen(w.text));
            if (cursor_pos[editing_id] < len) {
                cursor_pos[editing_id]++;
            }
        }

        // Enter → finalize editing, skip confirm action
        bool finalized = false;
        for (uint8_t i = 0; i < input.action_count; ++i) {
            auto a = input.actions[i];
            if (a == InputAction::Confirm || a == InputAction::MenuConfirm) {
                editing_id = UINT16_MAX;
                finalized  = true;
                break;
            }
        }

        if (finalized) {
            // Don't process remaining input; editing just ended
        } else {
            // Check for Select action: if it's not on this TextField,
            // dismiss editing and let the click-through be processed.
            for (uint8_t i = 0; i < input.action_count; ++i) {
                if (input.actions[i] == InputAction::Select) {
                    // Compute hot under action position to see if we tapped elsewhere
                    float sx = input.action_x;
                    float sy = input.action_y;
                    for (uint16_t j = count; j > 0; --j) {
                        auto &tw = pool[j - 1];
                        if (!(tw.flags & WF_Visible)) {
                            continue;
                        }
                        if (!(tw.flags & WF_Enabled)) {
                            continue;
                        }
                        if (tw.type == (uint8_t)WidgetType::Label) {
                            continue;
                        }
                        float tax = abs_x(j - 1);
                        float tay = abs_y(j - 1);
                        if (tw.w <= 0.0f || tw.h <= 0.0f) {
                            continue;
                        }
                        if (sx >= tax && sx <= tax + tw.w && sy >= tay && sy <= tay + tw.h) {
                            if (j - 1 != editing_id) {
                                editing_id = UINT16_MAX;
                                finalized  = true;
                            }
                            break;
                        }
                    }
                    break;
                }
            }
            if (!finalized) {
                return;
            }
        }
    }

    // Get pointer position — prefer touch, then action, then mouse
    float px = input.mouse_x;
    float py = input.mouse_y;
    for (uint8_t i = 0; i < input.touch.active_count; ++i) {
        auto &f = input.touch.fingers[i];
        if (f.phase == TouchPhase::Pressing || f.phase == TouchPhase::Moved) {
            px = f.curr_x;
            py = f.curr_y;
            break;
        }
    }

    bool has_select = false;
    for (uint8_t i = 0; i < input.action_count; ++i) {
        if (input.actions[i] == InputAction::Select) {
            px         = input.action_x;
            py         = input.action_y;
            has_select = true;
            break;
        }
    }

    hot              = pick(px, py);
    bool finger_down = is_finger_down(input);

    if (has_select) {
        // Click detection: prefer active (held) widget, but fall back to hot
        // under the release position when active was already reset (fast clicks).
        uint16_t target = (active != UINT16_MAX && active == hot) ? active : hot;
        if (target != UINT16_MAX) {
            clicked = target;
            auto &w = pool[clicked];
            if (w.type == (uint8_t)WidgetType::TextField) {
                editing_id = clicked;
                active     = UINT16_MAX;
                return;
            }
            if (w.type == (uint8_t)WidgetType::Toggle || w.type == (uint8_t)WidgetType::Checkbox) {
                w.state = w.state ? 0 : 1;
            }
            if (w.on_click) {
                w.on_click(clicked);
            }
        }
        active = UINT16_MAX;
    } else if (finger_down) {
        if (active == UINT16_MAX && hot != UINT16_MAX) {
            active = hot;
        }
        // Slider drag update
        if (active != UINT16_MAX && pool[active].type == (uint8_t)WidgetType::Slider) {
            float ax  = abs_x(active);
            float rel = (px - ax) / pool[active].w;
            if (rel < 0.0f) {
                rel = 0.0f;
            }
            if (rel > 1.0f) {
                rel = 1.0f;
            }
            slider_value[active] = rel;
            if (on_change[active]) {
                on_change[active](active, rel);
            }
            hot = active;
        }
    } else {
        // Click elsewhere while editing → stop editing
        if (editing_id != UINT16_MAX) {
            editing_id = UINT16_MAX;
        }
        active = UINT16_MAX;
    }

    // Keyboard/gamepad focus navigation
    for (uint8_t i = 0; i < input.action_count; ++i) {
        auto a = input.actions[i];
        if (a == InputAction::MenuDown || a == InputAction::SwapDown) {
            focus_id = find_next_focus(focus_id);
        } else if (a == InputAction::MenuUp || a == InputAction::SwapUp) {
            focus_id = find_prev_focus(focus_id);
        } else if (a == InputAction::Confirm || a == InputAction::MenuConfirm) {
            if (focus_id != UINT16_MAX) {
                clicked = focus_id;
                auto &w = pool[focus_id];

                // TextField: start editing on confirm
                if (w.type == (uint8_t)WidgetType::TextField) {
                    editing_id = clicked;
                    active     = UINT16_MAX;
                    return;
                }

                if (w.type == (uint8_t)WidgetType::Toggle || w.type == (uint8_t)WidgetType::Checkbox) {
                    w.state = w.state ? 0 : 1;
                }
                if (w.on_click) {
                    w.on_click(focus_id);
                }
            }
        }
    }

    // Update button/toggle/checkbox visual states
    for (uint16_t i = 0; i < count; ++i) {
        if (!(pool[i].flags & WF_Visible)) {
            continue;
        }
        uint8_t t = pool[i].type;
        if (t != (uint8_t)WidgetType::Button && t != (uint8_t)WidgetType::Toggle && t != (uint8_t)WidgetType::Checkbox) {
            continue;
        }
        if (t == (uint8_t)WidgetType::Toggle || t == (uint8_t)WidgetType::Checkbox) {
            if (!(pool[i].flags & WF_Enabled)) {
                pool[i].bg_color = ui_darken(off_color[i], 80);
            } else if (i == active && hot == active) {
                pool[i].bg_color = pool[i].state ? on_color[i] : off_color[i];
            } else if (i == hot || i == focus_id) {
                pool[i].bg_color = pool[i].state ? ui_lighten(on_color[i], 20) : ui_lighten(off_color[i], 20);
            } else {
                pool[i].bg_color = pool[i].state ? on_color[i] : off_color[i];
            }
        } else if (!(pool[i].flags & WF_Enabled)) {
            pool[i].state = (uint8_t)BtnState::Normal;
        } else if (i == active && hot == active) {
            pool[i].state = (uint8_t)BtnState::Pressed;
        } else if (i == hot || i == focus_id) {
            pool[i].state = (uint8_t)BtnState::Hover;
        } else {
            pool[i].state = (uint8_t)BtnState::Normal;
        }
    }
}

void Manager::layout_children(uint16_t parent_id) noexcept {
    uint8_t type = layout_type[parent_id];
    uint8_t lpad = layout_pad[parent_id];
    uint8_t gap  = layout_spacing[parent_id];

    float   cx   = static_cast<float>(lpad);
    float   cy   = static_cast<float>(lpad);

    for (uint16_t i = 0; i < count; ++i) {
        if (pool[i].parent != parent_id) {
            continue;
        }
        if (!(pool[i].flags & WF_Visible)) {
            continue;
        }

        auto &child = pool[i];

        if (type == 1) { // HBox
            child.x = cx;
            child.y = static_cast<float>(lpad);
            if (child.w > 0.0f) {
                cx += child.w + gap;
            }
        } else if (type == 2) { // VBox
            child.x = static_cast<float>(lpad);
            child.y = cy;
            if (child.h > 0.0f) {
                cy += child.h + gap;
            }
        }

        if (layout_type[i] != 0) {
            layout_children(i);
        }
    }
}

void Manager::layout(Renderer &r) noexcept {
    measure(r);
    for (uint16_t i = 0; i < count; ++i) {
        if (pool[i].parent != UINT16_MAX) {
            continue;
        }
        if (!(pool[i].flags & WF_Visible)) {
            continue;
        }
        if (layout_type[i] == 0) {
            continue;
        }
        layout_children(i);
    }
}

// ─── Line drawing helper ───────────────────────────────────────────
static void draw_line(SpriteBatch &batch, float x1, float y1, float x2, float y2, float thickness, uint32_t color) noexcept {
    float dx  = x2 - x1;
    float dy  = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) {
        return;
    }
    float angle = std::atan2(dy, dx);
    float cx    = (x1 + x2) * 0.5f;
    float cy    = (y1 + y2) * 0.5f;
    batch.add(cx, cy, len, thickness, angle, color, 0);
}

void Manager::render(Renderer &r, SpriteBatch &batch, float dt) noexcept {
    measure(r);
    batch.reset();

    uint16_t        fw          = static_cast<uint16_t>(r.width);
    uint16_t        fh          = static_cast<uint16_t>(r.height);

    // ── Pass 1: background rectangles ───────────────────────────
    // Flush per material-group for widgets with custom shader overrides
    bool            has_batch   = false;
    const Material *current_mat = nullptr; // nullptr = default (white_tex + sprite_pipeline)

    for (uint16_t i = 0; i < count; ++i) {
        auto &w = pool[i];
        if (!(w.flags & WF_Visible)) {
            continue;
        }
        if (w.bg_color == 0 && w.type != (uint8_t)WidgetType::Image) {
            continue;
        }
        if (w.type == (uint8_t)WidgetType::Label) {
            continue;
        }

        float           ax  = abs_x(i);
        float           ay  = abs_y(i);

        // Resolve per-widget material override
        const Material *mat = widget_material[i].pipeline.is_valid() ? &widget_material[i] : nullptr;

        // Flush previous batch if material changed
        if (mat != current_mat) {
            if (has_batch) {
                if (current_mat) {
                    r.flush_sprites(batch, *current_mat);
                } else {
                    r.flush_sprites(batch, r.white_tex, r.default_sampler);
                }
                batch.reset();
                has_batch = false;
            }
            current_mat = mat;
        }

        int16_t  cx, cy;
        uint16_t cw, ch;
        if (get_clip(i, cx, cy, cw, ch)) {
            r.set_scissor(cx, cy, cw, ch);
        }

        uint32_t color = w.bg_color;
        if (w.type == (uint8_t)WidgetType::Button) {
            if (!(w.flags & WF_Enabled)) {
                color = ui_darken(color, 80);
            } else if (w.state == (uint8_t)BtnState::Pressed) {
                color = ui_darken(color, 50);
            } else if (w.state == (uint8_t)BtnState::Hover) {
                color = ui_lighten(color, 30);
            }
        }

        // Toggle draws a track half the height, not a full rect
        if (w.type == (uint8_t)WidgetType::Toggle) {
            float track_h = w.h * 0.5f;
            float track_y = ay + (w.h - track_h) * 0.5f;
            batch.add(ax + w.w * 0.5f, track_y + track_h * 0.5f, w.w, track_h, 0.0f, color, 0);
        } else {
            batch.add(ax + w.w * 0.5f, ay + w.h * 0.5f, w.w, w.h, 0.0f, color, 0);
        }

        has_batch = true;
    }

    if (has_batch) {
        if (current_mat) {
            r.flush_sprites(batch, *current_mat);
        } else {
            r.flush_sprites(batch, r.white_tex, r.default_sampler);
        }
    }

    r.set_scissor(0, 0, fw, fh);
    batch.reset();

    // ── Pass 2: toggles, sliders, checkboxes, cursor, callbacks ──
    float cursor_blink  = std::fmod(cursor_timer, 0.5f) < 0.25f ? 1.0f : 0.0f;
    cursor_timer       += dt;

    for (uint16_t i = 0; i < count; ++i) {
        auto &w = pool[i];
        if (!(w.flags & WF_Visible)) {
            continue;
        }

        int16_t  cx, cy;
        uint16_t cw, ch;
        bool     clipped = get_clip(i, cx, cy, cw, ch);
        if (clipped) {
            r.set_scissor(cx, cy, cw, ch);
        }

        float ax = abs_x(i);
        float ay = abs_y(i);

        // ── Toggle switch thumb ──────────────────────────────────
        if (w.type == (uint8_t)WidgetType::Toggle) {
            float    track_h     = w.h * 0.5f;
            float    track_y     = ay + (w.h - track_h) * 0.5f;
            float    thumb_size  = track_h * 0.75f;
            float    margin      = (track_h - thumb_size) * 0.5f;
            float    thumb_lx    = w.state ? ax + w.w - thumb_size - margin : ax + margin;
            float    thumb_ly    = track_y + margin;
            uint32_t thumb_color = (i == focus_id || i == hot) ? theme.toggle_thumb_hot : theme.toggle_thumb;
            if (!(w.flags & WF_Enabled)) {
                thumb_color = ui_darken(thumb_color, 80);
            }
            batch.add(thumb_lx + thumb_size * 0.5f, thumb_ly + thumb_size * 0.5f, thumb_size, thumb_size, 0.0f, thumb_color, 0);
        }

        // ── Slider bar + thumb ───────────────────────────────────
        if (w.type == (uint8_t)WidgetType::Slider) {
            float val      = slider_value[i];
            float track_h  = w.h * 0.4f;
            float track_cx = ax + w.w * 0.5f; // track center x
            float track_cy = ay + w.h * 0.5f; // vertically centered in slider

            batch.add(track_cx, track_cy, w.w, track_h, 0.0f, theme.slider_track, 0);
            if (val > 0.01f) {
                float fill_cx = ax + (w.w * val) * 0.5f; // fill left-aligned at ax
                batch.add(fill_cx, track_cy, w.w * val, track_h, 0.0f, theme.slider_fill, 0);
            }
            float thumb_w  = w.h * 0.5f;
            float thumb_h  = w.h * 0.7f;
            float thumb_lx = ax + w.w * val; // right-aligned: left edge at fill edge
            if (thumb_lx + thumb_w > ax + w.w) {
                thumb_lx = ax + w.w - thumb_w;
            }
            float    thumb_cy    = ay + w.h * 0.5f;
            uint32_t thumb_color = (i == focus_id || i == hot) ? theme.slider_thumb_hot : theme.slider_thumb;
            batch.add(thumb_lx + thumb_w * 0.5f, thumb_cy, thumb_w, thumb_h, 0.0f, thumb_color, 0);
        }

        // ── Checkbox box + checkmark ─────────────────────────────
        if (w.type == (uint8_t)WidgetType::Checkbox) {
            float    s           = 28.0f;
            float    bx          = ax;
            float    by          = ay;
            uint32_t check_color = w.state ? on_color[i] : off_color[i];
            uint32_t border_col  = w.state ? ui_lighten(on_color[i], 20) : theme.checkbox_border;

            // Border (4 thin quads)
            float    bw          = 2.0f;
            batch.add(bx + s * 0.5f, by + bw * 0.5f, s, bw, 0.0f, border_col, 0);
            batch.add(bx + s * 0.5f, by + s - bw * 0.5f, s, bw, 0.0f, border_col, 0);
            batch.add(bx + bw * 0.5f, by + (s - bw * 2.0f) * 0.5f + bw, bw, s - bw * 2.0f, 0.0f, border_col, 0);
            batch.add(bx + s - bw * 0.5f, by + (s - bw * 2.0f) * 0.5f + bw, bw, s - bw * 2.0f, 0.0f, border_col, 0);

            if (w.state) {
                float inner_s = s - 4.0f;
                batch.add(bx + 2.0f + inner_s * 0.5f, by + 2.0f + inner_s * 0.5f, inner_s, inner_s, 0.0f, check_color, 0);
                // Check mark ✓
                float x1 = bx + 6.0f, y1 = by + 16.0f;
                float x2 = bx + 11.0f, y2 = by + 22.0f;
                float x3 = bx + 23.0f, y3 = by + 7.0f;
                draw_line(batch, x1, y1, x2, y2, 2.5f, theme.checkbox_check);
                draw_line(batch, x2, y2, x3, y3, 2.5f, theme.checkbox_check);
            }
        }

        // ── TextField cursor ─────────────────────────────────────
        if (w.type == (uint8_t)WidgetType::TextField && editing_id == i && cursor_blink > 0.0f) {
            float text_scale = w.scale;
            float char_width = 12.0f * text_scale;
            float cursor_x   = ax + static_cast<float>(pad[i][3]) + 8.0f + static_cast<float>(cursor_pos[i]) * char_width;
            float cursor_y   = ay + static_cast<float>(pad[i][0]) + 6.0f;
            float cursor_h   = 20.0f * text_scale;
            batch.add(cursor_x + 1.0f, cursor_y + cursor_h * 0.5f, 2.0f, cursor_h, 0.0f, theme.cursor, 0);
        }

        // ── Custom draw callbacks ────────────────────────────────
        if (w.on_draw) {
            w.on_draw(i, r, batch, ax, ay, dt);
        }

        if (clipped) {
            r.set_scissor(0, 0, fw, fh);
        }

        // ── Focus indicator ─────────────────────────────────────
        if (focus_id == i && (w.flags & WF_Focusable)) {
            float    focus_w = w.w > 0 ? w.w : 100.0f;
            float    focus_h = w.h > 0 ? w.h : 30.0f;
            uint32_t fc      = theme.focus_color;
            float    fw4     = focus_w + 4.0f;
            batch.add(ax - 2.0f + fw4 * 0.5f, ay - 1.0f, fw4, 2.0f, 0.0f, fc, 0);
            batch.add(ax - 2.0f + fw4 * 0.5f, ay + focus_h + 1.0f, fw4, 2.0f, 0.0f, fc, 0);
            batch.add(ax - 1.0f, ay + focus_h * 0.5f, 2.0f, focus_h, 0.0f, fc, 0);
            batch.add(ax + focus_w + 1.0f, ay + focus_h * 0.5f, 2.0f, focus_h, 0.0f, fc, 0);
        }
    }

    if (batch.count > 0) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler);
    }

    r.set_scissor(0, 0, fw, fh);
    batch.reset();

    // ── Pass 3: text ────────────────────────────────────────────
    for (uint16_t i = 0; i < count; ++i) {
        auto &w = pool[i];
        if (!(w.flags & WF_Visible)) {
            continue;
        }
        if (w.text[0] == '\0') {
            continue;
        }

        float    ax    = abs_x(i);
        float    ay    = abs_y(i);

        float    pad_l = static_cast<float>(pad[i][3]);
        float    pad_r = static_cast<float>(pad[i][1]);
        float    pad_t = static_cast<float>(pad[i][0]);
        float    pad_b = static_cast<float>(pad[i][2]);

        float    a     = static_cast<float>(content_ascent[i]);
        float    hc    = static_cast<float>(content_h[i]);

        int16_t  cx, cy;
        uint16_t cw, ch;
        if (get_clip(i, cx, cy, cw, ch)) {
            r.set_scissor(cx, cy, cw, ch);
        }

        float txt_x = 0, txt_y = 0;
        float txt_sc = 1.0f;
        char  buf[16];

        if (w.type == (uint8_t)WidgetType::Button) {
            float text_w  = static_cast<float>(content_w[i]);
            float inner_w = w.w - pad_l - pad_r;
            txt_x         = ax + pad_l + (inner_w - text_w) * 0.5f;
            txt_y         = ay + pad_t + (w.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc        = w.scale;
            uint32_t tc   = w.text_color;
            if (w.state == (uint8_t)BtnState::Pressed) {
                tc = ui_darken(tc, 40);
            }
            r.draw_text(r.default_font, w.text, txt_x, txt_y, tc, txt_sc);
        } else if (w.type == (uint8_t)WidgetType::Toggle) {
            txt_y  = ay + pad_t + (w.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc = w.scale;
            r.draw_text(r.default_font, w.text, txt_x, txt_y, w.text_color, txt_sc);
        } else if (w.type == (uint8_t)WidgetType::Checkbox) {
            txt_x  = ax + pad_l + 34.0f;
            txt_y  = ay + pad_t + (w.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc = 1.0f;
            r.draw_text(r.default_font, w.text, txt_x, txt_y, w.text_color, txt_sc);
        } else if (w.type == (uint8_t)WidgetType::TextField) {
            txt_x  = ax + pad_l + 8.0f;
            txt_y  = ay + pad_t + (w.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc = w.scale;
            r.draw_text(r.default_font, w.text, txt_x, txt_y, w.text_color, txt_sc);
        } else if (w.type == (uint8_t)WidgetType::Slider) {
            int pct = static_cast<int>(slider_value[i] * 100.0f + 0.5f);
            int len = snprintf(buf, sizeof(buf), "%d%%", pct);
            if (len > 0) {
                txt_x  = ax + w.w + pad_r + 12.0f;
                txt_y  = ay + pad_t + (w.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
                txt_sc = 1.0f;
                r.draw_text(r.default_font, buf, txt_x, txt_y, w.text_color ? w.text_color : theme.slider_text, txt_sc);
            }
        } else {
            txt_x  = ax + pad_l;
            txt_y  = ay + pad_t;
            txt_sc = w.scale;
            // สำหรับภาษาไทย ต้องใช้ draw_text ที่รองรับ combining characters
            // และใช้ scale ที่เหมาะสม
            if (w.type == (uint8_t)WidgetType::Label) {
                // Thai text needs special handling
                bool has_thai = false;
                for (const char *p = w.text; *p; ++p) {
                    if ((unsigned char)*p >= 0xE0) { // UTF-8 multi-byte
                        has_thai = true;
                        break;
                    }
                }
                if (has_thai) {
                    txt_sc = w.scale * 1.2f; // Slightly larger for Thai readability
                }
            }
            r.draw_text(r.default_font, w.text, txt_x, txt_y, w.text_color, txt_sc);
        }
        r.set_scissor(0, 0, fw, fh);
    }

    if (batch.count > 0) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler);
    }
}

// ─── Theme::load — JSON config via simdjson ─────────────────────
Theme Theme::load(const char *path) noexcept {
    Theme                   t = Theme::dark();

    simdjson::padded_string json;
    if (simdjson::padded_string::load(path).get(json) != simdjson::SUCCESS) {
        return t;
    }

    simdjson::dom::parser  parser;
    simdjson::dom::element doc;
    if (parser.parse(json).get(doc) != simdjson::SUCCESS) {
        return t;
    }

    simdjson::dom::object obj;
    if (doc.get_object().get(obj) != simdjson::SUCCESS) {
        return t;
    }

    auto set = [&](const char *key, uint32_t &field) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        if (val.is_string()) {
            std::string_view sv;
            if (val.get_string().get(sv) == simdjson::SUCCESS) {
                field = static_cast<uint32_t>(std::strtoul(sv.data(), nullptr, 16));
            }
        } else if (val.is_uint64()) {
            uint64_t v;
            if (val.get_uint64().get(v) == simdjson::SUCCESS) {
                field = static_cast<uint32_t>(v);
            }
        } else if (val.is_int64()) {
            int64_t v;
            if (val.get_int64().get(v) == simdjson::SUCCESS) {
                field = static_cast<uint32_t>(v);
            }
        }
    };

    // Parse padding array [top, right, bottom, left]
    auto set_pad = [&](const char *key, int8_t pad[4]) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        simdjson::dom::array arr;
        if (val.get_array().get(arr) != simdjson::SUCCESS) {
            return;
        }
        size_t i = 0;
        for (auto elem : arr) {
            if (i >= 4) {
                break;
            }
            int64_t v;
            if (elem.get_int64().get(v) == simdjson::SUCCESS) {
                pad[i] = static_cast<int8_t>(v);
            }
            ++i;
        }
    };

    auto set_u8 = [&](const char *key, uint8_t &field) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        uint64_t v;
        if (val.get_uint64().get(v) == simdjson::SUCCESS) {
            field = static_cast<uint8_t>(v);
        }
    };

    auto set_float = [&](const char *key, float &field) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        double d;
        if (val.get_double().get(d) == simdjson::SUCCESS) {
            field = static_cast<float>(d);
        } else if (val.is_int64()) {
            int64_t i;
            if (val.get_int64().get(i) == simdjson::SUCCESS) {
                field = static_cast<float>(i);
            }
        } else if (val.is_uint64()) {
            uint64_t i;
            if (val.get_uint64().get(i) == simdjson::SUCCESS) {
                field = static_cast<float>(i);
            }
        }
    };

    auto set_str = [&](const char *key, char *buf, size_t bufsz) noexcept {
        simdjson::dom::element val;
        if (obj[key].get(val) != simdjson::SUCCESS) {
            return;
        }
        std::string_view sv;
        if (val.get_string().get(sv) == simdjson::SUCCESS) {
            size_t n = sv.size();
            if (n >= bufsz) {
                n = bufsz - 1;
            }
            std::memcpy(buf, sv.data(), n);
            buf[n] = '\0';
        }
    };

    // ── Colors ──────────────────────────────────────────────────
    set("panel_bg", t.panel_bg);
    set("button_bg", t.button_bg);
    set("button_text", t.button_text);
    set("toggle_track_on", t.toggle_track_on);
    set("toggle_track_off", t.toggle_track_off);
    set("toggle_thumb", t.toggle_thumb);
    set("toggle_thumb_hot", t.toggle_thumb_hot);
    set("toggle_text", t.toggle_text);
    set("slider_bg", t.slider_bg);
    set("slider_track", t.slider_track);
    set("slider_fill", t.slider_fill);
    set("slider_thumb", t.slider_thumb);
    set("slider_thumb_hot", t.slider_thumb_hot);
    set("slider_text", t.slider_text);
    set("checkbox_on", t.checkbox_on);
    set("checkbox_off", t.checkbox_off);
    set("checkbox_border", t.checkbox_border);
    set("checkbox_check", t.checkbox_check);
    set("checkbox_text", t.checkbox_text);
    set("textfield_bg", t.textfield_bg);
    set("textfield_text", t.textfield_text);
    set("cursor", t.cursor);
    set("focus_color", t.focus_color);
    set("text_primary", t.text_primary);
    set("text_secondary", t.text_secondary);

    // ── Padding ─────────────────────────────────────────────────
    set_pad("panel_pad", t.panel_pad);
    set_pad("label_pad", t.label_pad);
    set_pad("button_pad", t.button_pad);
    set_pad("toggle_pad", t.toggle_pad);
    set_pad("slider_pad", t.slider_pad);
    set_pad("checkbox_pad", t.checkbox_pad);
    set_pad("textfield_pad", t.textfield_pad);

    // ── Layout ──────────────────────────────────────────────────
    set_u8("layout_padding", t.layout_padding);
    set_u8("layout_spacing", t.layout_spacing);

    // ── Font ────────────────────────────────────────────────────
    set_str("font_path", t.font_path, sizeof(t.font_path));
    set_float("font_scale", t.font_scale);

    return t;
}

} // namespace ui

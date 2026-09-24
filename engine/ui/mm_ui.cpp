// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_ui.hpp"
#include "../core/mm_vfs.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <simdjson.h>


// simdjson header-only implementation
#include "../../thirdparty/simdjson/singleheader/simdjson.cpp"

namespace ui {

void Manager::handle(const InputState &input) noexcept {
    rebuild_abs_cache();
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
    if (parent_id >= MAX) {
        return;
    }
    if (layout_type[parent_id] == 1) {
        layout_children_axis<0>(parent_id);
    } else if (layout_type[parent_id] == 2) {
        layout_children_axis<1>(parent_id);
    }
}

// Flow-layout kernel, Axis 0 = horizontal (HBox), 1 = vertical (VBox).
// Template so the axis choice folds at compile time (zero-cost vs branches).
// Two passes: measure total extent, then place with alignment.
// Legacy equivalence: with align=Start/Start, pct=0, margin=0 the output is
// bit-identical to the old sequential pile-from-padding pass.
template <uint8_t Axis> void Manager::layout_children_axis(uint16_t parent_id) noexcept {
    auto &parent = pool[parent_id];
    float lpad   = static_cast<float>(layout_pad[parent_id]);
    float gap    = static_cast<float>(layout_spacing[parent_id]);
    uint8_t pack = layout_align[parent_id];
    uint8_t main_align  = pack & 0x3;
    uint8_t cross_align = (pack >> 2) & 0x3;
    if (main_align > 2) {
        main_align = 0;
    }
    if (cross_align > 2) {
        cross_align = 0;
    }

    float parent_main  = (Axis == 0) ? parent.w : parent.h;
    float parent_cross = (Axis == 0) ? parent.h : parent.w;
    float inner_main   = parent_main - 2.0f * lpad;
    float inner_cross  = parent_cross - 2.0f * lpad;
    if (inner_main < 0.0f) {
        inner_main = 0.0f;
    }
    if (inner_cross < 0.0f) {
        inner_cross = 0.0f;
    }

    auto main_size = [&](uint16_t id) noexcept -> float {
        uint8_t pct = (Axis == 0) ? size_pct_w[id] : size_pct_h[id];
        float   abs = (Axis == 0) ? pool[id].w : pool[id].h;
        return (pct == 0) ? abs : inner_main * (static_cast<float>(pct) / 100.0f);
    };
    auto cross_size = [&](uint16_t id) noexcept -> float {
        uint8_t pct = (Axis == 0) ? size_pct_h[id] : size_pct_w[id];
        float   abs = (Axis == 0) ? pool[id].h : pool[id].w;
        return (pct == 0) ? abs : inner_cross * (static_cast<float>(pct) / 100.0f);
    };
    // Margin indices: HBox main = left[3]/right[1], cross = top[0]/bottom[2];
    // VBox swaps the roles.
    auto m_before_main = [&](uint16_t id) noexcept -> float {
        return static_cast<float>((Axis == 0) ? margin[id][3] : margin[id][0]);
    };
    auto m_after_main = [&](uint16_t id) noexcept -> float {
        return static_cast<float>((Axis == 0) ? margin[id][1] : margin[id][2]);
    };
    auto m_before_cross = [&](uint16_t id) noexcept -> float {
        return static_cast<float>((Axis == 0) ? margin[id][0] : margin[id][3]);
    };
    auto m_after_cross = [&](uint16_t id) noexcept -> float {
        return static_cast<float>((Axis == 0) ? margin[id][2] : margin[id][1]);
    };

    // Pass 1: total main-axis extent.
    float    total = 0.0f;
    uint16_t n     = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (pool[i].parent != parent_id) {
            continue;
        }
        if (!(pool[i].flags & WF_Visible)) {
            continue;
        }
        total += m_before_main(i) + main_size(i) + m_after_main(i);
        ++n;
    }
    if (n > 1) {
        total += gap * static_cast<float>(n - 1);
    }

    // Pass 2: place with main-axis alignment (overflow falls back to Start).
    float start = lpad;
    if (main_align == 1) {
        start += (inner_main - total) * 0.5f;
    } else if (main_align == 2) {
        start += inner_main - total;
    }
    if (start < lpad) {
        start = lpad;
    }
    float cursor = start;
    bool  first  = true;
    for (uint16_t i = 0; i < count; ++i) {
        if (pool[i].parent != parent_id) {
            continue;
        }
        if (!(pool[i].flags & WF_Visible)) {
            continue;
        }
        auto &child = pool[i];
        if (!first) {
            cursor += gap;
        }
        first = false;
        cursor += m_before_main(i);
        float c_main  = main_size(i);
        float c_cross = cross_size(i);
        float c_total_cross = m_before_cross(i) + c_cross + m_after_cross(i);
        float cross_off = 0.0f;
        if (cross_align == 1) {
            cross_off = (inner_cross - c_total_cross) * 0.5f;
        } else if (cross_align == 2) {
            cross_off = inner_cross - c_total_cross;
        }
        if (cross_off < 0.0f) {
            cross_off = 0.0f;
        }
        if constexpr (Axis == 0) {
            child.x = cursor;
            child.y = lpad + m_before_cross(i) + cross_off;
            if (size_pct_w[i] != 0) {
                child.w = c_main;
            }
            if (size_pct_h[i] != 0) {
                child.h = c_cross;
            }
        } else {
            child.y = cursor;
            child.x = lpad + m_before_cross(i) + cross_off;
            if (size_pct_h[i] != 0) {
                child.h = c_main;
            }
            if (size_pct_w[i] != 0) {
                child.w = c_cross;
            }
        }
        cursor += c_main + m_after_main(i);

        if (layout_type[i] != 0) {
            layout_children(i);
        }
    }
}

template void Manager::layout_children_axis<0>(uint16_t) noexcept;
template void Manager::layout_children_axis<1>(uint16_t) noexcept;

// Layout a subtree: containers get laid out (recursing into nested ones),
// plain widgets only forward the search to their children. This fixes
// containers nested under non-layout parents (previously never laid out);
// root-level containers behave exactly as before.
void Manager::layout_subtree(uint16_t id) noexcept {
    if (id >= MAX) {
        return;
    }
    if (!(pool[id].flags & WF_Visible)) {
        return;
    }
    if (layout_type[id] != 0) {
        layout_children(id);
        return;
    }
    for (uint16_t i = 0; i < count; ++i) {
        if (pool[i].parent == id) {
            layout_subtree(i);
        }
    }
}

void Manager::layout(Renderer &r) noexcept {
    if (measure_dirty) {
        measure(r);
        measure_dirty = false;
    }
    abs_cache_dirty = true;
    for (uint16_t i = 0; i < count; ++i) {
        if (pool[i].parent != UINT16_MAX) {
            continue;
        }
        if (!(pool[i].flags & WF_Visible)) {
            continue;
        }
        layout_subtree(i);
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
    rebuild_abs_cache();

    if (measure_dirty) {
        measure(r);
        measure_dirty = false;
    }
    batch.reset();

    uint16_t fw = static_cast<uint16_t>(r.width);
    uint16_t fh = static_cast<uint16_t>(r.height);

    // ── Animation update ─────────────────────────────────────────
    {
        const float anim_speed = 10.0f;
        float t_lerp = 1.0f - std::exp(-anim_speed * dt);
        for (uint16_t i = 0; i < count; ++i) {
            auto &w = pool[i];
            if (!(w.flags & WF_Visible)) continue;
            if (w.type == (uint8_t)WidgetType::Button ||
                w.type == (uint8_t)WidgetType::Toggle ||
                w.type == (uint8_t)WidgetType::Checkbox) {
                if (!(w.flags & WF_Enabled)) {
                    w.press_scale_target = 1.0f;
                } else if (w.state == (uint8_t)BtnState::Pressed) {
                    w.press_scale_target = 0.85f;
                } else {
                    w.press_scale_target = 1.0f;
                }
                w.press_scale += (w.press_scale_target - w.press_scale) * t_lerp;
                if (w.type == (uint8_t)WidgetType::Toggle) {
                    float target = w.state ? 1.0f : 0.0f;
                    w.thumb_pos += (target - w.thumb_pos) * t_lerp;
                }
                if (w.type == (uint8_t)WidgetType::Button) {
                    float target = ((w.flags & WF_Enabled) && w.state == (uint8_t)BtnState::Hover) ? 1.0f : 0.0f;
                    w.hover_factor += (target - w.hover_factor) * t_lerp;
                }
            } else {
                w.press_scale = 1.0f;
            }
            if (w.type == (uint8_t)WidgetType::Panel && w.anim_t < 1.0f) {
                w.anim_t += dt * anim_speed;
                if (w.anim_t > 1.0f) w.anim_t = 1.0f;
            }
        }
    }

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
        if (w.type == (uint8_t)WidgetType::Checkbox) {
            continue; // drawn in pass 1.75 (rounded shader)
        }

        float ax = abs_x(i);
        float ay = abs_y(i);

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

        uint32_t     color   = w.bg_color;
        bool         has_mat = mat != nullptr;
        if (has_mat) {
            // With custom material: draw at full brightness, overlay state later
            color = 0xFFFFFFFF;
        } else if (w.type == (uint8_t)WidgetType::Button) {
            if (!(w.flags & WF_Enabled)) {
                color = ui_darken(color, 80);
            } else if (w.state == (uint8_t)BtnState::Pressed) {
                color = ui_darken(color, 50);
            } else {
                color = ui_lerp_color(color, ui_lighten(color, 30), w.hover_factor);
            }
        }

        // Panel fade-in: modulate alpha by anim_t
        if (w.type == (uint8_t)WidgetType::Panel && w.anim_t < 1.0f) {
            mm_math::color pc = mm_math::color::from_u32_argb(color); // 0xAARRGGBB
            color = pc.with_alpha(pc.a * w.anim_t).to_u32_argb();
        }

        // Look up style for shape/radius/border
        const WidgetStyle &sty = styles[w.style_id];
        ShapeType eff_shape = static_cast<ShapeType>(w.shape);
        if (eff_shape == ShapeType::Default) {
            eff_shape = sty.shape;
        }
        if (eff_shape == ShapeType::Custom) {
            if (has_batch) {
                if (current_mat) {
                    r.flush_sprites(batch, *current_mat);
                } else {
                    r.flush_sprites(batch, r.white_tex, r.default_sampler);
                }
                batch.reset();
                has_batch  = false;
                current_mat = nullptr;
            }
            continue;
        }

        float    ps     = w.press_scale;
        float    radius = sty.corner_r;
        float    bw     = sty.border_width;
        uint32_t bcol   = sty.border_color;

        // Toggle draws a track half the height, not a full rect
        if (w.type == (uint8_t)WidgetType::Toggle) {
            float track_h = w.h * 0.5f;
            float track_y = ay + (w.h - track_h) * 0.5f;
            batch.add(ax + w.w * 0.5f, track_y + track_h * 0.5f, w.w * ps, track_h * ps, 0.0f, color, 0, 0, radius, bw, bcol);
        } else {
            batch.add(ax + w.w * 0.5f, ay + w.h * 0.5f, w.w * ps, w.h * ps, 0.0f, color, 0, 0, radius, bw, bcol);
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

    // ── Pass 1.5: state overlays for material-backed buttons ─────
    has_batch   = false;
    current_mat = nullptr;
    for (uint16_t i = 0; i < count; ++i) {
        auto &w = pool[i];
        if (!(w.flags & WF_Visible)) continue;
        if (!widget_material[i].pipeline.is_valid()) continue;
        if (w.type != (uint8_t)WidgetType::Button) continue;

        uint32_t overlay = 0;
        if (!(w.flags & WF_Enabled)) {
            overlay = 0x50000000;
        } else if (w.state == (uint8_t)BtnState::Pressed) {
            overlay = 0x80000000;
        } else if (w.state == (uint8_t)BtnState::Hover) {
            overlay = 0x30FFFFFF;
        }
        if (overlay == 0) continue;

        int16_t  cx, cy;
        uint16_t cw, ch;
        if (get_clip(i, cx, cy, cw, ch)) {
            r.set_scissor(cx, cy, cw, ch);
        }

        float ax = abs_x(i);
        float ay = abs_y(i);
        batch.add(ax + w.w * 0.5f, ay + w.h * 0.5f, w.w, w.h, 0.0f, overlay, 0);
        has_batch = true;
    }
    if (has_batch) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler);
    }

    r.set_scissor(0, 0, fw, fh);
    batch.reset();

    // ── Pass 1.75: checkbox rounded rects (batched, single flush) ──
    {
        bool has_rounded = false;
        for (uint16_t i = 0; i < count; ++i) {
            auto &w = pool[i];
            if (!(w.flags & WF_Visible)) continue;
            if (w.type != (uint8_t)WidgetType::Checkbox) continue;

            float ax = abs_x(i);
            float ay = abs_y(i);
            float ps = w.press_scale;
            float s  = 28.0f * ps;
            float norm_radius = 4.0f / 28.0f;
            float norm_border = 2.0f / 28.0f;
            uint32_t fill_col  = w.state ? on_color[i] : off_color[i];
            uint32_t border_col = w.state ? ui_lighten(on_color[i], 20) : theme.checkbox_border;

            batch.add(ax + 28.0f * 0.5f, ay + 28.0f * 0.5f, s, s, 0.0f, fill_col, 0, 0, norm_radius, norm_border, border_col);
            has_rounded = true;
        }
        if (has_rounded) {
            r.flush_rounded_sprites(batch);
            batch.reset();
        }
    }

    // ── Pass 2: toggles, sliders, checkmarks, cursor, callbacks ──
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
            float    ps          = w.press_scale;
            float    base_thumb  = track_h * 0.75f;
            float    thumb_size  = base_thumb * ps;
            float    track_margin = (track_h - base_thumb) * 0.5f;
            float    thumb_lx    = ax + track_margin + (w.w - thumb_size - 2.0f * track_margin) * w.thumb_pos;
            float    thumb_ly    = track_y + track_margin;
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

        // ── Checkmark (box already drawn in pass 1.75) ───────────
        if (w.type == (uint8_t)WidgetType::Checkbox && w.state) {
            float ps      = w.press_scale;
            float ccx     = ax + 14.0f;
            float ccy     = ay + 14.0f;
            float x1 = ccx + (-8.0f) * ps, y1 = ccy + 2.0f  * ps;
            float x2 = ccx + (-3.0f) * ps, y2 = ccy + 8.0f  * ps;
            float x3 = ccx + 9.0f  * ps, y3 = ccy + (-7.0f) * ps;
            draw_line(batch, x1, y1, x2, y2, 2.5f * ps, theme.checkbox_check);
            draw_line(batch, x2, y2, x3, y3, 2.5f * ps, theme.checkbox_check);
        }

        // ── TextField cursor ─────────────────────────────────────
        if (w.type == (uint8_t)WidgetType::TextField && editing_id == i && cursor_blink > 0.0f) {
            float    cursor_x = ax + static_cast<float>(pad[i][3]) + 8.0f;
            uint32_t len      = static_cast<uint32_t>(std::strlen(w.text));
            Utf8Decoder dec(w.text, len);
            uint8_t  pos = 0;
            while (pos < cursor_pos[i]) {
                uint32_t cp = dec.next();
                if (cp == 0) break;
                auto *g = r.default_font.get_glyph(cp);
                if (g) cursor_x += static_cast<float>(g->advance) * w.scale;
                ++pos;
            }
            float cursor_y   = ay + static_cast<float>(pad[i][0]) + 6.0f;
            float cursor_h   = 20.0f * w.scale;
            batch.add(cursor_x + 1.0f, cursor_y + cursor_h * 0.5f, 2.0f, cursor_h, 0.0f, theme.cursor, 0);
        }

        // ── Custom draw callbacks ────────────────────────────────
        if (w.on_draw) {
            if (batch.count > 0) {
                r.flush_sprites(batch, r.white_tex, r.default_sampler);
                batch.reset();
            }
            w.on_draw(i, r, batch, ax, ay, dt);
        }

        if (clipped) {
            r.set_scissor(0, 0, fw, fh);
        }

        // ── Focus indicator ─────────────────────────────────────
        if (focus_id == i && (w.flags & WF_Focusable)) {
            float    focus_w = w.w > 0 ? w.w : 100.0f;
            float    focus_h = w.h > 0 ? w.h : 30.0f;
            float pulse = 0.6f + 0.4f * std::sin(cursor_timer * 6.0f);
            uint32_t fc = (theme.focus_color & 0x00FFFFFF) |
                          (static_cast<uint32_t>(pulse * 255.0f) << 24);
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
        bool     clipped = get_clip(i, cx, cy, cw, ch);
        if (clipped) {
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
            // Label goes right of the track (same pattern as the slider % label).
            // (Was missing: txt_x stayed 0.0 and every toggle label piled at the window edge.)
            txt_x  = ax + w.w + pad_r + 8.0f;
            txt_y  = ay + pad_t + (w.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc = w.scale;
            r.draw_text(r.default_font, w.text, txt_x, txt_y, w.text_color, txt_sc);
        } else if (w.type == (uint8_t)WidgetType::Checkbox) {
            txt_x  = ax + pad_l + 34.0f;
            txt_y  = ay + pad_t + (w.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
            txt_sc = w.scale;
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
        if (clipped) r.set_scissor(0, 0, fw, fh);
    }

    if (batch.count > 0) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler);
    }
}

// ─── Theme::load — JSON config via simdjson ─────────────────────
Theme Theme::load(const char *path) noexcept {
    Theme                   t = Theme::dark();

    VfsBlob blob = g_vfs.read_bundle(path);
    if (!blob.valid()) {
        return t;
    }

    // simdjson::padded_string expects padding, VFS might not provide it.
    // However, simdjson can parse from a buffer.
    simdjson::dom::parser  parser;
    simdjson::dom::element doc;
    if (parser.parse(static_cast<const uint8_t*>(blob.data), blob.size).get(doc) != simdjson::SUCCESS) {
        blob.free();
        return t;
    }
    blob.free();


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

    // ── Colors (format: 0xAARRGGBB) ──
    // JSON example: "FF3A3A3A" = alpha=0xFF, red=0x3A, green=0x3A, blue=0x3A
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

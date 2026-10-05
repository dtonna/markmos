// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_ui_wlist.hpp — listbox / combobox family (header-only).
// Included by mm_ui.hpp tail (Manager complete).
#pragma once
#include "mm_ui.hpp"

namespace ui {

// Row contents, for apps that draw a list row themselves (set_row_renderer
// or their own overlay list). The engine owns the PLUMBING of a row - scroll,
// selection, hit-test, clipping - but the fields inside a row are the app's,
// and every app was re-deriving the same two numbers: where the baseline sits
// inside the row box, and which edge a value hangs from.
namespace rows {

// One field inside a row. `x`/`w` are the field's box RELATIVE to the row's
// left edge; `right` hangs the text off the box's right edge (values, counts)
// instead of its left.
struct Field {
    const char *text;
    f32       x;
    f32       w;
    u32    color;
    bool        right;
};

// Baseline for text vertically centred in a row box.
//
// `measure_text`'s out_h is the LINE box (it reserves leading a row never
// draws), so centring on it sits every line visibly high - the same reason
// the button label and the tooltip centre on the ascent instead. Pure so it
// is testable without a Renderer.
inline f32 baseline_from(f32 line_h, f32 row_y, f32 row_h, f32 scale) noexcept {
    const f32 glyph_h = line_h * scale;
    return row_y + (row_h - glyph_h) * 0.5f + glyph_h;
}

inline f32 baseline(Renderer &r, f32 row_y, f32 row_h, f32 scale) noexcept {
    return baseline_from(static_cast<f32>(r.default_font.line_height), row_y, row_h, scale);
}

// Draws each field in the row. Null/empty text is skipped, so a caller can
// leave a slot unused without a branch of its own. Returns fields drawn.
inline u8 draw(Renderer &r, const Field *fields, u8 count, f32 row_x, f32 row_y, f32 row_h, f32 scale) noexcept {
    if (fields == nullptr) {
        return 0;
    }
    u8 drawn = 0;
    for (u8 i = 0; i < count; ++i) {
        const Field &f = fields[i];
        if (f.text == nullptr || f.text[0] == '\0') {
            continue;
        }
        f32 tw = 0.0f, th = 0.0f;
        r.measure_text(r.default_font, f.text, scale, tw, th);
        const f32 tx = align_text_x(f.right ? text_align::RIGHT : text_align::LEFT, row_x + f.x, f.w, tw);
        r.draw_text(r.default_font, f.text, tx, baseline(r, row_y, row_h, scale), f.color, scale);
        ++drawn;
    }
    return drawn;
}

} // namespace rows

namespace listbox {

// Scroll geometry over cached metrics (moved from mm_ui.cpp, byte-identical).
inline f32 maxscroll(u8 count, const ListboxMetrics &m) noexcept {
    f32 x = static_cast<f32>(count) - static_cast<f32>(m.visible);
    return x > 0.0f ? x : 0.0f;
}

inline void clamp_scroll(f32 &scroll, u8 count, const ListboxMetrics &m) noexcept {
    f32 x = maxscroll(count, m);
    if (scroll < 0.0f) {
        scroll = 0.0f;
    }
    if (scroll > x) {
        scroll = x;
    }
}

inline f32 thumb_h(u8 count, f32 h, const ListboxMetrics &m) noexcept {
    if (count <= m.visible) {
        return h;
    }
    f32 t = h * static_cast<f32>(m.visible) / static_cast<f32>(count);
    if (t < m.row_h) {
        t = m.row_h; // never shorter than one row
    }
    return t;
}

// ComboBox popup thumb: same shape as Listbox but the "page" is
// K_MAX_POPUP_ROWS (lm.visible belongs to the closed field, not the popup).

// Pass 1.8: selected row + scrollbar, own flush (moved from Manager::render).
inline void render_pass(Manager &m, SpriteBatch &batch, Renderer &r) noexcept {
    // One flush PER LISTBOX, not one for the lot. Each listbox scissors its own
    // rect, so batching them together meant only the LAST rect applied to the
    // whole batch - every earlier listbox's selection highlight and scrollbar
    // were clipped away by a rect that belonged to a different widget.
    //
    // This was invisible while scissors sorted before every sprite draw (the
    // sort-key bug), because then none of them applied at all. Fixing the key
    // made the scissors real, which exposed this underneath it - the same
    // "per-widget state, one flush" shape as the Pass 1 bug.
    for (u16 i = 0; i < m.count; ++i) {
        auto &w = m.pool[i];
        if (!m.is_visible(i)) {
            continue;
        }
        if (w.type != (u8)widget_type::LISTBOX) {
            continue;
        }
        f32 ax = m.abs_x(i);
        f32 ay = m.abs_y(i);
        r.set_scissor(static_cast<i16>(ax), static_cast<i16>(ay), static_cast<u16>(w.frame.w), static_cast<u16>(w.frame.h));
        const ListboxMetrics &lm  = m.listbox_metrics[i];
        int                   sel = m.listbox_selected[i];
        f32                 sc  = m.listbox_scroll[i];
        int                   n   = m.listbox_count[i];
        // Content rect inside the style border (same rule as slider /
        // progressbar): rows, highlight, text and scrollbar all live here;
        // the border band is input-dead + paint-free. Borderless styles
        // inset 0 → geometry identical to before; degenerate styles fall
        // back to frame geometry (today's behavior, never blank).
        f32 cx0 = 0.0f, cy0 = 0.0f, cx1 = 0.0f, cy1 = 0.0f;
        content_rect(m.styles[w.style_id], ax, ay, w.frame.w, w.frame.h, cx0, cy0, cx1, cy1);
        bool  cok   = (cx1 > cx0 && cy1 > cy0);
        f32 y_top = cok ? cy0 : ay;
        f32 y_bot = cok ? cy1 : ay + w.frame.h;
        // A grid's header occupies the top of the content rect. Every ROW site
        // below starts under it, via the one shared definition - Pass 3 and the
        // tap path call the same row_area_top(), so the header and the rows
        // cannot drift apart. 0 for a plain listbox (identical to before).
        const f32 head   = m.grid_header_h[i];
        const f32 rows_y = cok ? m.row_area_top(i, cy0) : ay + head;
        f32 sel_cx = cok ? (cx0 + cx1) * 0.5f : ax + w.frame.w * 0.5f;
        f32 sel_w  = cok ? (cx1 - cx0) : w.frame.w;
        if (sel >= 0 && sel < n && lm.row_h > 0.0f) {
            f32 ry = rows_y + (static_cast<f32>(sel) - sc) * lm.row_h;
            if (ry + lm.row_h > rows_y && ry < y_bot) {
                f32 ry0 = ry < rows_y ? rows_y : ry;
                f32 ry1 = ry + lm.row_h > y_bot ? y_bot : ry + lm.row_h;
                if (ry1 > ry0) {
                    batch.add(sel_cx, (ry0 + ry1) * 0.5f, sel_w, ry1 - ry0, 0.0f, m.theme.listbox_sel, 0);
                }
            }
        }
        if (n > lm.visible && lm.row_h > 0.0f) {
            f32 maxsc = ui::listbox::maxscroll(static_cast<u8>(n), lm);
            // Track spans the ROW area only: a scrollbar running up into the
            // header band would sit on top of the last column's title.
            f32 track_h = y_bot - rows_y;
            f32 track_cy = (rows_y + y_bot) * 0.5f;
            f32 sb_right = cok ? cx1 : ax + w.frame.w;
            f32 sb_left  = cok ? cx0 : ax;
            f32 sb_cx = sb_right - lm.scrollbar_w * 0.5f;
            if (sb_cx - lm.scrollbar_w * 0.5f < sb_left) {
                sb_cx = sb_left + lm.scrollbar_w * 0.5f; // narrow box: pin inside left cap
            }
            batch.add(sb_cx, track_cy, lm.scrollbar_w, track_h, 0.0f, m.theme.slider_track, 0);
            f32 th = ui::listbox::thumb_h(static_cast<u8>(n), track_h, lm);
            f32 ty = rows_y + (maxsc > 0.0f ? sc / maxsc * (track_h - th) : 0.0f);
            batch.add(sb_cx, ty + th * 0.5f, lm.scrollbar_w, th, 0.0f, m.theme.slider_thumb, 0);
        }
        // Grid header band. Drawn HERE rather than as a row of child widgets
        // because it has to take the TAP, and the tap path already holds the
        // column model and the sort state - a child label would need its own
        // callback per column, plus a hit test, to say the same thing.
        if (m.is_grid(i) && head > 0.5f) {
            const GridMetrics &gm    = m.grid_metrics[i];
            const bool          sbvis = n > lm.visible && lm.row_h > 0.0f;
            const f32         band_w = (cok ? cx1 - cx0 : w.frame.w) - (sbvis ? lm.scrollbar_w : 0.0f);
            const f32         band_cx = (cok ? cx0 : ax) + band_w * 0.5f;
            const f32         band_cy = y_top + head * 0.5f;
            if (band_w > 0.0f) {
                batch.add(band_cx, band_cy, band_w, head, 0.0f, m.theme.panel_bg, 0);
                // Bottom rule: without it the band is a tint that reads as a
                // mis-painted first row rather than as a header.
                batch.add(band_cx, rows_y - 0.5f, band_w, 1.0f, 0.0f, m.theme.separator_color, 0);
                // Sort marker: a diamond per sortable column, bright on the
                // sorted one. A glyph would need a font that has a triangle;
                // SpriteBatch::add already rotates.
                for (u8 c = 0; c < gm.columns; ++c) {
                    if (!m.grid_cols[i][c].sortable) {
                        continue;
                    }
                    const grid_sort sd = m.grid_sort_of(i, c);
                    const f32     sz = 4.0f;
                    const f32     sx = (cok ? cx0 : ax) + gm.x[c] + gm.w[c] - 8.0f;
                    // Position carries the DIRECTION: the font has no triangle, and
                    // a marker that only says "this column is sorted" leaves the
                    // user tapping again to find out which way it went. Up = ASC,
                    // down = DESC, centred = unsorted.
                    const f32     dy = sd == grid_sort::ASC ? -head * 0.18f : (sd == grid_sort::DESC ? head * 0.18f : 0.0f);
                    batch.add(sx, band_cy + dy, sz, sz, 0.78539816f, sd == grid_sort::NONE ? m.theme.text_secondary : m.theme.text_primary, 0);
                }
            }
        }
        // Drain HERE, while this listbox's scissor is the active one.
        if (batch.count > 0) {
            r.flush_sprites(batch, r.white_tex, r.default_sampler);
            batch.reset();
        }
    }
}

// Pass 3 rows (moved from Manager::render). Scissor restore stays with the
// caller (needs the loop clip state); this draws rows only.
inline void render_rows(Manager &m, u16 i, f32 ax, f32 ay, SpriteBatch &batch, Renderer &r, bool clipped, i16 cx, i16 cy,
                        u16 cw, u16 ch, u16 fw, u16 fh) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::LISTBOX) {
        return;
    }
        // Visible rows only, clipped to the listbox CONTENT rect (not the
        // frame, not the parent clip — rows scroll inside this box and must
        // never paint over the border bands).
        int                   n     = m.listbox_count[i];
        f32                 sc    = m.listbox_scroll[i];
        const ListboxMetrics &lm    = m.listbox_metrics[i];
        int                   first = static_cast<int>(sc);
        f32                 gx0 = 0.0f, gy0 = 0.0f, gx1 = 0.0f, gy1 = 0.0f;
        content_rect(m.styles[w.style_id], ax, ay, w.frame.w, w.frame.h, gx0, gy0, gx1, gy1);
        bool  gok = (gx1 > gx0 && gy1 > gy0);
        f32 rx0 = gok ? gx0 : ax;
        f32 rx1 = gok ? gx1 : ax + w.frame.w;
        f32 ry1 = gok ? gy1 : ay + w.frame.h;
        // Same shared definition as Pass 1.8 / the tap path, so a partially
        // scrolled row slides UNDER the header instead of over it.
        f32 ry0 = gok ? m.row_area_top(i, gy0) : ay + m.grid_header_h[i];
        f32 il = 0.0f, it = 0.0f, ir = 0.0f, ib = 0.0f;
        content_insets(m.styles[w.style_id], w.frame.w, w.frame.h, il, it, ir, ib);
        f32 tx = ax + (lm.indent > il ? lm.indent : il); // text clears the left cap
        // The item ARRAY is only what the DEFAULT row draws, so it must not
        // gate the loop: a listbox with a row renderer (and every DataGrid, whose
        // cells live in the app's own model) has no items[] at all, and the
        // `listbox_items[i] &&` guard silently drew an EMPTY box.
        if (n > 0 && lm.row_h > 0.0f) {
            r.set_scissor(static_cast<i16>(rx0), static_cast<i16>(ry0), static_cast<u16>(rx1 - rx0),
                          static_cast<u16>(ry1 - ry0), ui::text_key(r));
            for (int r_ = first; r_ < n; ++r_) {
                f32 ry = ry0 + (static_cast<f32>(r_) - sc) * lm.row_h;
                if (ry >= ry1) {
                    break;
                }
                if (ry + lm.row_h <= ry0) {
                    continue;
                }
                // Custom row contents: the app owns the visuals, the engine
                // still owns scroll / selection / clipping. The default
                // one-string row is skipped entirely (no f64 draw).
                if (m.listbox_row_cb[i] != nullptr) {
                    m.listbox_row_cb[i](i, r, batch, rx0, ry, rx1 - rx0, lm.row_h, r_, m.listbox_row_user[i]);
                    continue;
                }
                const char *s = m.listbox_items[i] != nullptr ? m.listbox_items[i][r_] : nullptr;
                if (!s) {
                    continue;
                }
                u32 tc    = (r_ == m.listbox_selected[i]) ? 0xFFFFFFFFu : (w.text_color ? w.text_color : m.theme.listbox_text);
                // Per-row ascent/descent, same convention as measure():
                // draw_text places glyphs by signed bearings, so the
                // origin needs the +ascent term (button-identical math).
                f32    max_a = 0.0f, max_d = 0.0f;
                {
                    u32    slen = static_cast<u32>(std::strlen(s));
                    Utf8Decoder dec(s, slen);
                    for (;;) {
                        u32 cp = dec.next();
                        if (cp == 0) {
                            break;
                        }
                        if (cp == '\n') {
                            continue;
                        }
                        auto *g = r.default_font.get_glyph(cp);
                        if (g) {
                            f32 asc = static_cast<f32>(-g->bearing_y) * lm.text_scale;
                            if (asc > max_a) {
                                max_a = asc;
                            }
                            int dpx = g->bearing_y + static_cast<i16>(g->h);
                            if (dpx > 0) {
                                f32 desc = static_cast<f32>(dpx) * lm.text_scale;
                                if (desc > max_d) {
                                    max_d = desc;
                                }
                            }
                        }
                    }
                }
                if (max_a < 0.001f) {
                    max_a = 26.0f * lm.text_scale;
                }
                f32 hc2 = max_a + max_d;
                r.draw_text(r.default_font, s, tx, ry + (lm.row_h + 2.0f * max_a - hc2) * 0.5f, tc, lm.text_scale);
            }
        }
    if (clipped) {
        r.set_scissor(cx, cy, cw, ch, ui::text_key(r));
    } else {
        r.set_scissor(0, 0, fw, fh, ui::text_key(r));
    }
}

} // namespace listbox

namespace combobox {

inline f32 thumb_h(u8 vis_count, f32 ph, const ListboxMetrics &m) noexcept {
    if (vis_count <= Manager::K_MAX_POPUP_ROWS) {
        return ph;
    }
    f32 t = ph * static_cast<f32>(Manager::K_MAX_POPUP_ROWS) / static_cast<f32>(vis_count);
    if (t < m.row_h) {
        t = m.row_h;
    }
    return t;
}

// Popup draw transform (Pass 4), chosen per widget by combobox_anim_mode.
// All modes animate from the edge that touches the field, and all scale/clip
// the popup RECT - never the rows - so row_h stays authoritative and the rows
// follow because they are positioned from the rect.
inline void popup_anim_apply(Manager &m, u16 i, f32 &qx, f32 &qy, f32 &qw, f32 &qh) noexcept {
    const f32 a = m.combobox_anim[i];
    if (a >= 0.999f) {
        return; // settled: identity, no cost
    }
    const f32 e = 1.0f - (1.0f - a) * (1.0f - a) * (1.0f - a); // ease-out cubic
    // Anchor on the field side: below the field -> grows from its top edge,
    // flipped above -> grows from its bottom edge.
    const bool below = qy >= m.abs_y(i) + m.pool[i].frame.h - 0.5f;
    switch (m.combobox_anim_mode[i]) {
    case popup_anim_mode::GARAGE: {
        // Roller door: the popup ROLLS out of the field. Growing qh from the
        // anchored edge is the whole effect - the row scissor is this rect, so
        // rows past the moving edge are clipped away and appear to be behind
        // the shutter. Width never changes, like a real door.
        const f32 full = qh;
        qh               = full * e;
        if (!below) {
            qy = (qy + full) - qh;
        }
        break;
    }
    case popup_anim_mode::SLIDE: {
        // Translates a few px out of the field, no rescale.
        const f32 d = (1.0f - e) * 8.0f;
        qy += below ? d : -d;
        break;
    }
    case popup_anim_mode::FADE:
        break; // geometry untouched
    case popup_anim_mode::SCALE_FADE:
    default: {
        const f32 sc     = 0.94f + 0.06f * e;
        const f32 cx     = qx + qw * 0.5f;
        const f32 bottom = qy + qh;
        qw *= sc;
        qh *= sc;
        qx  = cx - qw * 0.5f;
        qy  = below ? qy : (bottom - qh); // grow out of the field either way
        break;
    }
    }
}

// Per-row reveal ramp for the GARAGE mode: a row fades in over the last row of
// travel, so the shutter edge is soft instead of a hard cut. Returns 1 for every
// other mode and for a settled popup.
inline f32 garage_row_alpha(const Manager &m, u16 i, f32 row_y, f32 row_h, f32 popup_bottom) noexcept {
    if (m.combobox_anim_mode[i] != popup_anim_mode::GARAGE || m.combobox_anim[i] >= 0.999f) {
        return 1.0f;
    }
    if (row_h <= 0.0f) {
        return 1.0f;
    }
    const f32 v = (popup_bottom - row_y) / row_h;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Pass 4 overlay popups (moved from Manager::render).
inline void render_overlay(Manager &m, SpriteBatch &batch, Renderer &r, u16 fw, u16 fh) noexcept {
    bool has_pop = false;
    for (u16 i = 0; i < m.count; ++i) {
        auto &w = m.pool[i];
        if (!m.is_visible(i)) {
            continue;
        }
        if (w.type != (u8)widget_type::COMBOBOX) {
            continue;
        }
        if (!m.combobox_open[i]) {
            continue;
        }
        f32 qx, qy, qw, qh;
        if (!m.combobox_popup_rect(i, qx, qy, qw, qh)) {
            continue;
        }
        popup_anim_apply(m, i, qx, qy, qw, qh);
        // GARAGE starts at zero height. The row scissor would then be 0 tall
        // (uint16 cast), which is backend-dependent - skip instead of asking
        // for a degenerate clip.
        if (qh < 1.0f) {
            continue;
        }
        const f32 pa = m.combobox_anim[i]; // popup fade
        // Text layer (same key as the rows below): the popup is overlay
        // and must cover text of widgets underneath (draw_text is key 1,
        // so a key-0 bg would sort underneath it and let it bleed through).
        r.set_scissor(static_cast<i16>(qx), static_cast<i16>(qy), static_cast<u16>(qw), static_cast<u16>(qh), ui::text_key(r));
        const ListboxMetrics &lm    = m.combobox_metrics[i];
        u8               vis_n = m.combobox_visible_count(i);
        f32                 sc    = m.combobox_scroll[i];
        batch.add(qx + qw * 0.5f, qy + qh * 0.5f, qw, qh, 0.0f, ui_alpha(m.theme.listbox_bg, pa), 0);
        // Highlight: live arrow highlight wins; otherwise the selection.
        int selv = m.combobox_hl[i];
        if (selv < 0 || selv >= static_cast<int>(vis_n)) {
            selv = m.combobox_item_to_visible(i, m.combobox_selected[i]);
        }
        if (selv >= 0 && lm.row_h > 0.0f) {
            f32 ry = qy + (static_cast<f32>(selv) - sc) * lm.row_h;
            if (ry + lm.row_h > qy && ry < qy + qh) {
                batch.add(qx + qw * 0.5f, ry + lm.row_h * 0.5f, qw, lm.row_h, 0.0f, ui_alpha(m.theme.listbox_sel, pa), 0);
            }
        }
        if (vis_n > Manager::K_MAX_POPUP_ROWS && lm.row_h > 0.0f) {
            f32 maxsc = m.combobox_maxscroll(vis_n);
            f32 th    = ui::combobox::thumb_h(vis_n, qh, lm);
            f32 ty    = qy + (maxsc > 0.0f ? sc / maxsc * (qh - th) : 0.0f);
            batch.add(qx + qw - lm.scrollbar_w * 0.5f, qy + qh * 0.5f, lm.scrollbar_w, qh, 0.0f, ui_alpha(m.theme.slider_track, pa), 0);
            batch.add(qx + qw - lm.scrollbar_w * 0.5f, ty + th * 0.5f, lm.scrollbar_w, th, 0.0f, ui_alpha(m.theme.slider_thumb, pa), 0);
        }
        has_pop = true;
    }
    // ONE flush for every popup, which is only correct because at most one popup
    // can be open: combobox_open_now() closes every other combobox first. Each
    // popup scissors its own rect, so N popups in one batch would take the LAST
    // rect - the same defect Pass 1 / 1.5 / 1.8 / 2 had. If that invariant is
    // ever relaxed, this loop needs the clip in its batch identity like the rest.
    // (The qh < 1.0f skip above is the same rule as clip_draws(), applied by
    // hand because the GARAGE animation drives the height to zero on purpose.)
    if (has_pop) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler, ui::text_key(r));
        batch.reset();
    }
    // Rows (text pass, same per-row ascent convention as ListBox).
    for (u16 i = 0; i < m.count; ++i) {
        auto &w = m.pool[i];
        if (!m.is_visible(i)) {
            continue;
        }
        if (w.type != (u8)widget_type::COMBOBOX) {
            continue;
        }
        if (!m.combobox_open[i]) {
            continue;
        }
        f32 qx, qy, qw, qh;
        if (!m.combobox_popup_rect(i, qx, qy, qw, qh)) {
            continue;
        }
        popup_anim_apply(m, i, qx, qy, qw, qh);
        // GARAGE starts at zero height. The row scissor would then be 0 tall
        // (uint16 cast), which is backend-dependent - skip instead of asking
        // for a degenerate clip.
        if (qh < 1.0f) {
            continue;
        }
        const f32 pa = m.combobox_anim[i]; // popup fade
        const ListboxMetrics &lm    = m.combobox_metrics[i];
        u8               vis_n = m.combobox_visible_count(i);
        f32                 sc    = m.combobox_scroll[i];
        r.set_scissor(static_cast<i16>(qx), static_cast<i16>(qy), static_cast<u16>(qw), static_cast<u16>(qh), ui::text_key(r));
        int first = static_cast<int>(sc);
        if (vis_n == 0) {
            // Empty filter: a dim "No match" row (tapping it shuts the
            // popup without changing the selection — see pre-pass).
            // Same origin math as a normal row ("No match" has no
            // descenders, so hc == max_a).
            f32 max_a = 26.0f * lm.text_scale;
            f32 ry0   = qy - sc * lm.row_h;
            r.draw_text(r.default_font, "No match", qx + lm.indent, ry0 + (lm.row_h + max_a) * 0.5f, ui_alpha(m.theme.text_secondary, pa), lm.text_scale);
        }
        for (int vr = first; vr < vis_n; ++vr) {
            f32 ry = qy + (static_cast<f32>(vr) - sc) * lm.row_h;
            if (ry >= qy + qh) {
                break;
            }
            if (ry + lm.row_h <= qy) {
                continue;
            }
            int item = m.combobox_visible_to_item(i, vr);
            if (item < 0) {
                continue;
            }
            const char *s = m.combobox_items[i][item];
            if (!s) {
                continue;
            }
            u32 tc    = (item == m.combobox_selected[i]) ? 0xFFFFFFFFu : (w.text_color ? w.text_color : m.theme.listbox_text);
            f32    max_a = 0.0f, max_d = 0.0f;
            {
                u32    slen = static_cast<u32>(std::strlen(s));
                Utf8Decoder dec(s, slen);
                for (;;) {
                    u32 cp = dec.next();
                    if (cp == 0) {
                        break;
                    }
                    if (cp == '\n') {
                        continue;
                    }
                    auto *g = r.default_font.get_glyph(cp);
                    if (g) {
                        f32 asc = static_cast<f32>(-g->bearing_y) * lm.text_scale;
                        if (asc > max_a) {
                            max_a = asc;
                        }
                        int dpx = g->bearing_y + static_cast<i16>(g->h);
                        if (dpx > 0) {
                            f32 desc = static_cast<f32>(dpx) * lm.text_scale;
                            if (desc > max_d) {
                                max_d = desc;
                            }
                        }
                    }
                }
            }
            if (max_a < 0.001f) {
                max_a = 26.0f * lm.text_scale;
            }
            f32 hc = max_a + max_d;
            // Garage mode: fade each row in over the last row of travel so the
            // shutter edge is soft, not a hard cut.
            tc = ui_alpha(tc, pa * garage_row_alpha(m, i, ry, lm.row_h, qy + qh));
            r.draw_text(r.default_font, s, qx + lm.indent, ry + (lm.row_h + 2.0f * max_a - hc) * 0.5f, tc, lm.text_scale);
        }
    }
    if (has_pop) {
        r.set_scissor(0, 0, fw, fh, ui::text_key(r));
        batch.reset();
    }
}

} // namespace combobox

// ─── listbox / combobox factories (moved from mm_ui.hpp, byte-identical) ───

inline u16 Manager::listbox(f32 x, f32 y, f32 w, f32 h, const char *const *items, u8 item_count, ClickCallback cb, u16 parent,
                                 u8 style_id) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg              = pool[id];
    wg.frame.x            = x;
    wg.frame.y            = y;
    wg.frame.w            = w;
    wg.frame.h            = h;
    wg.scale              = 1.0f; // neutral: row text size comes from listbox_metrics, not wg.scale
    wg.bg_color           = theme.listbox_bg;
    wg.text_color         = theme.listbox_text;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f; // reused: body-drag anchor pointer-y (Listbox only)
    wg.hover_factor       = 0.0f;
    wg.text[0]            = '\0'; // rows are drawn from listbox_items[], not w.text
    wg.parent             = parent;
    wg.type               = (u8)widget_type::LISTBOX;
    wg.flags              = WF_VISIBLE | WF_ENABLED | WF_FOCUSABLE | WF_CLIP;
    wg.state              = 0; // drag mode: 0=none, 1=thumb, 2=body
    wg.style_id           = style_id;
    wg.shape              = 0;
    wg.pos_mode           = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt          = mm_math::anchor::TOP_LEFT;
    wg.on_click           = cb;
    wg.on_draw            = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    listbox_items[id]     = items;
    listbox_count[id]     = item_count;
    listbox_selected[id]  = -1;
    listbox_scroll[id]    = 0.0f;
    listbox_anchor[id]    = 0.0f;
    std::memcpy(wg.pad, theme.listbox_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

inline u16 Manager::combobox(f32 x, f32 y, f32 w, f32 h, const char *const *items, u8 item_count, bool editable, ClickCallback cb,
                                  u16 parent, u8 style_id) noexcept {
    u16 id = alloc();
    if (id == UINT16_MAX) {
        return id;
    }
    auto &wg               = pool[id];
    wg.frame.x             = x;
    wg.frame.y             = y;
    wg.frame.w             = w;
    wg.frame.h             = h;
    wg.scale               = 1.0f; // row text size comes from combobox_metrics, not wg.scale
    wg.bg_color            = theme.button_bg;
    wg.text_color          = theme.button_text;
    wg.press_scale         = 1.0f;
    wg.press_scale_target  = 1.0f;
    wg.anim_t              = 0.0f;
    wg.thumb_pos           = 0.0f; // popup body-drag anchor pointer-y (Combobox only)
    wg.hover_factor        = 0.0f;
    wg.text[0]             = '\0'; // closed label; editable mode doubles as filter buffer
    wg.parent              = parent;
    wg.type                = (u8)widget_type::COMBOBOX;
    wg.flags               = WF_VISIBLE | WF_ENABLED | WF_FOCUSABLE;
    wg.state               = 0; // popup drag mode: 0=none, 1=thumb, 2=body
    wg.style_id            = style_id;
    wg.shape               = 0;
    wg.pos_mode            = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt           = mm_math::anchor::TOP_LEFT;
    wg.on_click            = cb;
    wg.on_draw             = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    combobox_items[id]     = items;
    combobox_count[id]     = item_count;
    combobox_selected[id]  = -1;
    combobox_scroll[id]    = 0.0f;
    combobox_anchor[id]    = 0.0f;
    combobox_open[id]      = false;
    combobox_editable[id]  = editable;
    combobox_filtering[id] = false;
    combobox_hl[id]        = -1;
    combobox_metrics[id]   = ListboxMetrics{};
    std::memcpy(wg.pad, theme.button_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

} // namespace ui

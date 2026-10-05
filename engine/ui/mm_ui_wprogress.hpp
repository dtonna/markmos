// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// mm_ui_wprogress.hpp — progressbar family (header-only).
// Included by mm_ui.hpp tail (Manager complete).
//
// Display-only determinate bar 0..1 in two shapes, chosen by the widget
// `shape` (existing skinning infra, no new API surface for the variant):
//   Rect / RoundedRect / style-default → horizontal line (track + fill)
//   Circle                            → ring sweep (track ring + arc)
// Non-square Circle frames sweep an ellipse (documented, accepted).
// Value flow: set_progress() writes the TARGET; Manager::update() lerps the
// displayed value toward it (same t_lerp as toggle thumbs); render draws
// the displayed value only (Plan B: no state mutation in render).
#pragma once
#include "mm_ui.hpp"
#include <cmath>
#include <cstdio> // snprintf for the percent label

namespace ui {
namespace progressbar {

// Stripe band speed (px/s) and duty (fraction of the pitch that is painted).
// Both are constants, not style fields: a look is a style, a timing is not.
static constexpr f32 K_STRIPE_SPEED = 26.0f;
static constexpr f32 K_STRIPE_DUTY  = 0.5f;

// Arc sweep for the Circle variant: ONE quad carrying the circle bounds,
// drawn through the analytic ring pipeline (fragment computes the band +
// sweep caps mathematically — no tile seams at any scale, 1 draw instead
// of 128 quads × 2 passes). Starts at 12 o'clock, sweeps clockwise by
// val * 2π. Same geometry as the old quad arc (outer edge at rad).
inline void render_arc(Manager &m, u16 i, f32 ax, f32 ay, SpriteBatch &batch, Renderer &r, f32 val, u32 color,
                      TextureHandle tex = {}) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::PROGRESSBAR) {
        return;
    }
    if (val <= 0.001f) {
        return;
    }
    if (val > 1.0f) {
        val = 1.0f;
    }
    const f32 cx   = ax + w.frame.w * 0.5f;
    const f32 cy   = ay + w.frame.h * 0.5f;
    const f32 side = (w.frame.w < w.frame.h ? w.frame.w : w.frame.h);
    // Keep the arc inside the ring border (same overlap class as the line
    // variant's fill vs caps): the SDF ring occupies an outer band rw of
    // the frame, and the arc's outer edge used to sit exactly on the frame
    // edge, painting over it. Ringless styles resolve rw = 0 → unchanged.
    f32       rw;
    u32    rc;
    resolve_ring(m.styles[w.style_id], rw, rc);
    const f32 rad = side * (0.5f - rw);
    if (rad <= 0.0f) {
        return;
    }
    const f32 thick = rad * 0.28f; // same visual weight as the quad arc
    const f32 diam  = rad * 2.0f;
    // Drain pending Pass-2 quads first (same pattern as render_burst):
    // flush_ring would otherwise draw FOREIGN quads (line-bar tracks)
    // as rings too — pipelines are per-flush, not per-quad.
    if (batch.count > 0) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler);
    }
    batch.reset();
    const f32 sweep = val * mm_math::MM_TWO_PI;
    // White tint when textured: the ramp supplies the colour, and v_color
    // multiplies it, so passing the theme fill would f64-darken it.
    //
    // The SWEEP travels in the quad's radius slot (ring.frag reads it there,
    // not from the UBO): a UBO's content is global to the frame, so rings
    // sharing one would all render the last ring's sweep. Every ring now shows
    // its OWN value - which is the whole point of four rings on a page.
    batch.add(cx, cy, diam, diam, 0.0f, tex.is_valid() ? 0xFFFFFFFFu : color, 0, 0, sweep);
    r.flush_ring(batch, 0.5f, thick / diam, sweep, tex);
    batch.reset();
}

// Completion-burst timeline (seconds): pop with overshoot, spin exactly
// one full turn, then shrink + fade away. Trigger/advance lives in
// Manager::update (state: progressbar_burst/celebrated); this only draws.
static constexpr f32 K_BURST_DUR    = 0.90f;
static constexpr f32 K_BURST_POP    = 0.22f;
static constexpr f32 K_BURST_SPIN0  = 0.10f;
static constexpr f32 K_BURST_SPIN1  = 0.75f;
static constexpr f32 K_BURST_SHRINK = 0.60f;
static constexpr f32 K_BURST_FADE   = 0.65f;

inline f32 ease_out_back(f32 u) noexcept {
    const f32 c1 = 1.70158f;
    const f32 c3 = c1 + 1.0f;
    f32       d  = u - 1.0f;
    return 1.0f + c3 * d * d * d + c1 * d * d;
}

// 4-point sparkle (two crossed pills: long glint + short crossbar) at the
// tip where the completed sweep meets its start (12 o'clock). Own flushes:
// pending plain batch is drained first so no foreign quad ever rides the
// rounded pipeline, then the pills flush rounded (SDF-AA capsules) alone.
inline void render_burst(Manager &m, u16 i, f32 ax, f32 ay, SpriteBatch &batch, Renderer &r) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::PROGRESSBAR || w.shape != (u8)shape_type::CIRCLE) {
        return;
    }
    f32 t = m.progressbar_burst[i];
    if (t < 0.0f || t > K_BURST_DUR) {
        return;
    }
    // Same ring-inset geometry as the arc (duplicated, not shared — keeps
    // render_arc byte-identical to its verified form).
    const f32 side = (w.frame.w < w.frame.h ? w.frame.w : w.frame.h);
    f32       rw;
    u32    rc;
    resolve_ring(m.styles[w.style_id], rw, rc);
    const f32 rad = side * (0.5f - rw);
    if (rad <= 0.0f) {
        return;
    }
    const f32 thick = rad * 0.28f;
    const f32 cx    = ax + w.frame.w * 0.5f;
    const f32 cy    = ay + w.frame.h * 0.5f;
    const f32 tx    = cx;
    const f32 ty    = cy - (rad - thick * 0.5f); // tip at 12 o'clock
    // Envelope: pop (overshoot) × shrink; spin linear one turn; fade late.
    f32 pop = (t < K_BURST_POP) ? ease_out_back(t / K_BURST_POP) : 1.0f;
    f32 su  = (t - K_BURST_SHRINK) / (K_BURST_DUR - K_BURST_SHRINK);
    if (su < 0.0f) {
        su = 0.0f;
    }
    if (su > 1.0f) {
        su = 1.0f;
    }
    f32 s = pop * (1.0f - su);
    f32 fu = (t - K_BURST_FADE) / (K_BURST_DUR - K_BURST_FADE);
    if (fu < 0.0f) {
        fu = 0.0f;
    }
    if (fu > 1.0f) {
        fu = 1.0f;
    }
    f32 alpha = 1.0f - fu;
    if (s <= 0.0f || alpha <= 0.0f) {
        return;
    }
    f32 u = (t - K_BURST_SPIN0) / (K_BURST_SPIN1 - K_BURST_SPIN0);
    if (u < 0.0f) {
        u = 0.0f;
    }
    if (u > 1.0f) {
        u = 1.0f;
    }
    f32 rot = u * mm_math::MM_TWO_PI; // exactly one turn
    f32 l1  = thick * 1.5f * s;        // long glint arm
    f32 l2  = thick * 1.0f * s;        // short crossbar arm
    f32 wd  = thick * 0.32f * s;
    if (wd < 0.5f) {
        return;
    }
    u32 col = (static_cast<u32>(alpha * 255.0f) << 24) | 0x00FFFFFFu;
    if (batch.count > 0) {
        r.flush_sprites(batch, r.white_tex, r.default_sampler);
    }
    batch.reset();
    batch.add(tx, ty, l1, wd, rot, col, 0, 0, 0.5f);
    batch.add(tx, ty, l2, wd, rot + mm_math::MM_HALF_PI, col, 0, 0, 0.5f);
    r.flush_rounded_sprites(batch);
    batch.reset();
}

// Track + fill (line variant) or arc sweep (circle variant).
// Type guard lives here (not at the call site) so the helper is safe
// to call for any widget id.
inline void render_parts(Manager &m, u16 i, f32 ax, f32 ay, SpriteBatch &batch, Renderer &r) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::PROGRESSBAR) {
        return;
    }
    f32 val = m.progressbar_value[i];
    if (w.shape == (u8)shape_type::CIRCLE) {
        // The SAME fill_tex the line variant uses, so one style skins both:
        // a line bar wants a linear ramp, a ring wants a sweep gradient
        // (Renderer::make_sweep_gradient_texture) - the field means "the paint
        // of the value", and the shape decides how it is laid out.
        render_arc(m, i, ax, ay, batch, r, val, m.theme.progressbar_fill, m.styles[w.style_id].fill_tex);
        render_burst(m, i, ax, ay, batch, r);
        return;
    }
    f32 x0 = 0.0f, x1 = 0.0f;
    content_box(m.styles[w.style_id], ax, w.frame.w, x0, x1);
    if (x1 <= x0) {
        return;
    }
    f32 span     = x1 - x0;
    f32 track_h  = w.frame.h * 0.4f;
    f32 track_cx = (x0 + x1) * 0.5f;
    f32 track_cy = ay + w.frame.h * 0.5f;
    // Track / fill / stripes share the STYLE's corner radius (see the slider
    // note): the rounded frame around a square track is the reported look.
    const f32 tcr = style_corner_radius(m.styles[w.style_id], w.shape);
    batch.add(track_cx, track_cy, span, track_h, 0.0f, m.theme.progressbar_track, 0, 0, tcr);
    if (val > 0.001f) {
        f32 fill_cx = x0 + (span * val) * 0.5f; // fill left-aligned at x0
        // Gradient (or any) fill texture: its own flush, because a texture
        // bind is per-FLUSH and Pass 2 otherwise packs every track / fill /
        // thumb into one batch. Drain first (drain-first rule) so no track
        // quad rides the textured flush and samples the ramp.
        // is_valid(), NOT handle.id != 0: a zero-init WidgetStyle leaves the
        // handle at SlotHandle::invalid() (0xFFFFFFFF), so an `!= 0` test
        // reads "has texture" for EVERY bar and the flat fill silently became
        // white. (mm_ui_wslider's thumb_tex check has the same shape — left
        // alone there, it currently looks right by luck.)
        const WidgetStyle  &sty  = m.styles[w.style_id];
        const TextureHandle ftex = sty.fill_tex;
        if (ftex.is_valid()) {
            if (batch.count > 0) {
                r.flush_sprites(batch, r.white_tex, r.default_sampler);
            }
            batch.reset();
            // White tint: the ramp's own colours are the fill's colours.
            batch.add(fill_cx, track_cy, span * val, track_h, 0.0f, 0xFFFFFFFF, 0, 0, tcr);
            r.flush_sprites(batch, ftex, r.default_sampler);
            batch.reset();
        } else {
            batch.add(fill_cx, track_cy, span * val, track_h, 0.0f, m.theme.progressbar_fill, 0, 0, tcr);
        }
        // Striped fill: bands of `pitch` px drawn from the fill's left edge,
        // phase-shifted by the value update() advanced. Anchoring at x0 (not at
        // the leading edge) is what keeps the pitch CONSTANT as the bar grows;
        // the band that would straddle the fill's right edge is simply
        // narrower, which is how CSS repeating gradients read too.
        const f32 pitch = sty.stripe_pitch;
        if (pitch > 0.0f) {
            const u32 scol = sty.stripe_color ? sty.stripe_color : 0x40000000u;
            const f32    fill_r  = x0 + span * val;
            f32          band_x  = x0 - m.progressbar_stripe_t[i];
            while (band_x < fill_r) {
                const f32 bx0 = band_x > x0 ? band_x : x0;
                const f32 bx1 = band_x + pitch * K_STRIPE_DUTY;
                if (bx1 > bx0) {
                    const f32 cw = bx1 - bx0;
                    if (bx1 > fill_r) {
                        break; // leading edge cuts the band: keep it unpainted
                    }
                    batch.add(bx0 + cw * 0.5f, track_cy, cw, track_h, 0.0f, scol, 0, 0, tcr);
                }
                band_x += pitch;
            }
        }
    }
}

// Percent label (drawn directly, same placement math as slider).
inline void render_text(Manager &m, u16 i, f32 ax, f32 ay, f32 pad_r, f32 pad_t, f32 pad_b, f32 a, f32 hc,
                        Renderer &r) noexcept {
    auto &w = m.pool[i];
    if (w.type != (u8)widget_type::PROGRESSBAR) {
        return;
    }
    char buf[16];
    int  pct = static_cast<int>(m.progressbar_value[i] * 100.0f + 0.5f);
    int  len = snprintf(buf, sizeof(buf), "%d%%", pct);
    if (len > 0) {
        f32 tw = 0.0f, th = 0.0f;
        if (w.shape == (u8)shape_type::CIRCLE) {
            // Gauge variant: the value belongs INSIDE the ring, centred. The
            // shared rule put it 12pt to the right of the frame, which for a
            // 120pt circle means on top of whatever sits next to it.
            f32    rw;
            u32 rc;
            resolve_ring(m.styles[w.style_id], rw, rc);
            const f32 side   = w.frame.w < w.frame.h ? w.frame.w : w.frame.h;
            const f32 rad    = side * (0.5f - rw);
            const f32 thick  = rad * 0.28f; // same band geometry as render_arc
            // Fit by the label's own INK BOX, not the line box: the ring is a
            // CIRCLE, so the binding constraint is the text's corner distance
            // from the centre, and at 100% the band is closed all the way round.
            // Sizing off the line box (or off font_scale alone) is what let a
            // "100%" grow into the band.
            const f32 r_in   = fmaxf(rad - thick, 1.0f) - 4.0f; // band's inner edge, minus a margin
            f32       ink_w = 0.0f, ink_h = 0.0f;
            r.measure_text(r.default_font, buf, 1.0f, ink_w, ink_h);
            const f32 diag    = 0.5f * __builtin_sqrtf(ink_w * ink_w + ink_h * ink_h);
            f32       sc      = (diag > 0.0f) ? fminf(m.theme.font_scale, r_in / diag) : m.theme.font_scale;
            if (sc < 0.12f) {
                sc = 0.12f; // never vanish entirely
            }
            r.measure_text(r.default_font, buf, sc, tw, th);
            const f32 cx    = ax + w.frame.w * 0.5f - tw * 0.5f;
            const f32 txt_y = ay + w.frame.h * 0.5f + sc * 8.0f;
            r.draw_text(r.default_font, buf, cx, txt_y, w.text_color ? w.text_color : m.theme.progressbar_fill, sc);
            return;
        }
        // Position + scale come from the shared helpers so this label cannot
        // spill out of its container (see Manager::value_label_x).
        const f32 sc = m.value_label_scale(r, w.frame.h, pad_t, pad_b);
        r.measure_text(r.default_font, buf, sc, tw, th);
        const f32 txt_x = m.value_label_x(i, ax, w.frame.w, tw);
        f32       txt_y = ay + pad_t + (w.frame.h - pad_t - pad_b + 2.0f * a - hc) * 0.5f;
        r.draw_text(r.default_font, buf, txt_x, txt_y, w.text_color ? w.text_color : m.theme.progressbar_fill, sc);
    }
}

} // namespace progressbar

// ─── progressbar factory ───

inline u16 Manager::progressbar(f32 x, f32 y, f32 w, f32 h, f32 initial, u16 parent, f32 scale, u8 style_id,
                                      u8 shape) noexcept {
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
    wg.bg_color           = theme.progressbar_bg;
    wg.text_color         = 0;
    wg.press_scale        = 1.0f;
    wg.press_scale_target = 1.0f;
    wg.anim_t             = 0.0f;
    wg.thumb_pos          = 0.0f;
    wg.hover_factor       = 0.0f;
    wg.text[0]            = '\0';
    wg.parent             = parent;
    wg.type               = (u8)widget_type::PROGRESSBAR;
    wg.flags              = WF_VISIBLE | WF_ENABLED;
    wg.state              = 0;
    wg.style_id           = style_id;
    wg.shape              = shape;
    wg.pos_mode           = mm_math::position_mode::RELATIVE; // legacy: parent offset
    wg.anchor_pt          = mm_math::anchor::TOP_LEFT;
    wg.on_click           = nullptr;
    wg.on_draw            = nullptr;
    click_user[id] = nullptr;
    draw_user[id]  = nullptr;
    if (initial < 0.0f) {
        initial = 0.0f;
    }
    if (initial > 1.0f) {
        initial = 1.0f;
    }
    progressbar_value[id]  = initial;
    progressbar_target[id] = initial;
    progressbar_burst[id]  = -1.0f;
    progressbar_celebrated[id] = (initial >= 0.999f); // born full = no pop
    std::memcpy(wg.pad, theme.slider_pad, sizeof(wg.pad));
    measure_dirty = true;
    // Recycled slots inherit these otherwise (alloc() resets nothing).
    align[id] = theme.label_align;
    text_box[id] = 0;
    return id;
}

inline void Manager::set_progress(u16 id, f32 v) noexcept {
    if (id >= MAX) {
        return;
    }
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    progressbar_target[id] = v;
}

} // namespace ui

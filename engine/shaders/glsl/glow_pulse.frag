// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// glow_pulse.frag — engine port of lab_glow: outer free-glow ring that
// starts tight at the card edge, expands outward (phase 0→1) and
// fades alpha 1→0. The host draws a quad k× larger than the card so the
// ring has margin to travel in (see misc.z).
//
// The phase has TWO sources. A negative misc.w keeps the original perpetual
// loop off render_time, which is what the lab demos want. A non-negative
// misc.w is the phase itself, 0..1, driven by the host — which is the only
// way to make the ring START when something happens rather than whenever the
// clock happens to say so. See the note on misc.w below for why an OFFSET of
// the clock cannot do that.

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform GlowParams {
    vec4 timing; // x = time (s), y = duration (s), z = max expand (uv units), w = ring width
    vec4 glow;   // rgba glow color
    vec4 misc;   // x = card aspect (w/h), y = intensity, z = quad scale k,
                 // w = phase override: >= 0 is the phase 0..1, < 0 means "loop off time"
} params;

float sd_round_box(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    float k = max(params.misc.z, 1.0);
    vec2 card_uv = (v_uv - 0.5) * k + 0.5;
    vec4 base = texture(u_tex, card_uv) * v_color;
    vec2 inside2 = step(vec2(0.0), card_uv) * step(card_uv, vec2(1.0));
    base *= inside2.x * inside2.y;

    float aspect = max(params.misc.x, 0.001);
    vec2 pc = (card_uv - 0.5) * vec2(aspect, 1.0);
    vec2 half_size = vec2(aspect * 0.5, 0.5) - 0.03;
    float dist = sd_round_box(pc, half_size, 0.03);

    float aa = max(fwidth(dist), 0.001);
    float body = 1.0 - smoothstep(-aa, aa, dist);

    float dur = max(params.timing.y, 0.001);
    // Host-driven phase when misc.w >= 0, else the perpetual clock loop.
    //
    // Why an OFFSET of render_time would not work: phase would be
    // fract((render_time - since)/dur), which is 0 only when render_time ==
    // since — but render_time counts from app start and `since` from the
    // event, so those two never meet. The event has to hand over the phase.
    float phase = (params.misc.w >= 0.0) ? clamp(params.misc.w, 0.0, 1.0) : fract(params.timing.x / dur);
    float center = phase * params.timing.z;
    float ring = 1.0 - smoothstep(0.0, max(params.timing.w, 0.0001), abs(dist - center));
    float outside = smoothstep(-aa, aa, dist); // free (outer) glow only
    float a = ring * outside * (1.0 - phase) * params.glow.a * params.misc.y;

    // The blend is SrcAlpha / OneMinusSrcAlpha — NOT premultiplied — so this
    // fragment must return an unpremultiplied colour and let the blend apply `a`.
    // Multiplying the glow colour by `a` here (the obvious line) made the blend
    // multiply it a SECOND time: out.rgb * out.a = glow.rgb * a * a. A ring at
    // half strength contributed a quarter of the glow and faded quadratically,
    // which is why it was barely visible at every setting.
    //
    // (1.0 - body) keeps the glow OUT of the card interior, so the art under it
    // blends normally instead of being tinted by the glow colour.
    vec3 col = base.rgb * body + params.glow.rgb * (1.0 - body);
    frag_color = vec4(col, max(base.a * body, a));
}

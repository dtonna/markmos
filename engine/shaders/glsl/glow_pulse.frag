// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// glow_pulse.frag — engine port of lab_glow: outer free-glow ring that
// starts tight at the card edge, expands outward (phase 0→1 loop) and
// fades alpha 1→0. The host draws a quad k× larger than the card so the
// ring has margin to travel in (see misc.z).

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform GlowParams {
    vec4 timing; // x = time (s), y = duration (s), z = max expand (uv units), w = ring width
    vec4 glow;   // rgba glow color
    vec4 misc;   // x = card aspect (w/h), y = intensity, z = quad scale k, w = spare
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
    float phase = fract(params.timing.x / dur);
    float center = phase * params.timing.z;
    float ring = 1.0 - smoothstep(0.0, max(params.timing.w, 0.0001), abs(dist - center));
    float outside = smoothstep(-aa, aa, dist); // free (outer) glow only
    float a = ring * outside * (1.0 - phase) * params.glow.a * params.misc.y;

    vec3 col = base.rgb * body + params.glow.rgb * a;
    frag_color = vec4(col, max(base.a * body, a));
}

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// gold_stay.frag — engine port of lab_stay: persistent border with a
// gentle pulse riding on top. Strong base (always clearly visible) +
// small breathe (±0.30). No travel.

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform GlowParams {
    vec4 timing; // x = time (s), y = pulse speed (Hz), z = band width (card-uv), w = spare
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

    float pulse = 0.5 + 0.5 * sin(params.timing.x * max(params.timing.y, 0.001) * 6.2831853);
    pulse = pulse * pulse * (3.0 - 2.0 * pulse); // eased breathe
    float w = max(params.timing.z, 0.0001);
    float band = 1.0 - smoothstep(0.0, w, abs(dist));
    float outside = smoothstep(-aa, aa, dist); // outer glow only
    float base_level = 0.70; // stays on
    float pulse_amp = 0.30;  // gentle breathe on top
    float a = band * outside * (base_level + pulse_amp * pulse) * params.glow.a * params.misc.y;

    vec3 col = base.rgb * body + params.glow.rgb * a;
    frag_color = vec4(col, max(base.a * body, a));
}

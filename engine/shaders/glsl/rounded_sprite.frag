// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// rounded_sprite.frag
#version 460

layout(location = 0) in vec2  v_uv;
layout(location = 1) in vec4  v_color;
layout(location = 2) in vec2  v_local;
layout(location = 3) in float v_radius;
layout(location = 4) in float v_border_w;
layout(location = 5) in vec4  v_border_color;

layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D u_texture;

layout(binding = 2) uniform RoundedParams {
    float time;
    float glow_intensity;
    float glow_width;
    float glow_pulse_freq;
    float sdf_aa_scale;
    float _pad0;
    float _pad1;
    float _pad2;
    vec4  glow_color;
} rounded_params;

float sdf_rounded_box(vec2 p, float r) {
    vec2 q = abs(p) - 0.5 + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    float dist = sdf_rounded_box(v_local, v_radius);
    float aa_raw = fwidth(dist);
    float aa     = max(aa_raw * rounded_params.sdf_aa_scale, 0.001);

    // Procedural SDF rounded box has edge at dist=0 (not 0.5 like baked SDF textures)
    float outer_mask = 1.0 - smoothstep(-aa, aa, dist);

    vec4 fill = texture(u_texture, v_uv) * v_color;

    // Border from per-vertex only (no UBO override)
    float bw = v_border_w;
    float border_mask = 0.0;
    if (bw > 0.0) {
        border_mask =
        1.0 - smoothstep(
            -bw - aa,
            -bw + aa,
            dist
        );
        border_mask = max(outer_mask - border_mask, 0.0);
    }

    // Composite (straight-alpha premul over)
    vec4 result = fill * outer_mask;

    // Apply border if border width > 0 (premul over)
    if (bw > 0.0) {
        float bm = border_mask * v_border_color.a;
        result.rgb = mix(result.rgb, v_border_color.rgb, bm);
        result.a   = max(result.a, bm);
    }

    // Conditional glow (selected/hint cards)
    if (rounded_params.glow_intensity > 0.0) {
        float gw = rounded_params.glow_width;
        float pulse = 0.5 + 0.5 * sin(rounded_params.time * rounded_params.glow_pulse_freq);
        float border_w = max(bw, 0.001);
        float glow_mask = smoothstep(-border_w, 0.0, dist) * (1.0 - smoothstep(0.0, gw, dist));
        float glow_alpha = rounded_params.glow_color.a * rounded_params.glow_intensity * pulse;
        vec3 glow_rgb = rounded_params.glow_color.rgb * glow_alpha * glow_mask;
        result.rgb = clamp(result.rgb + glow_rgb, 0.0, 1.0);
        result.a   = max(result.a, glow_alpha * glow_mask);
    }

    out_color = result;
}

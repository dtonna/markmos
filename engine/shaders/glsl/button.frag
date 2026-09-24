// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2  v_uv;
layout(location = 1) in vec4  v_color;
layout(location = 2) in vec2  v_local;
layout(location = 3) in float v_radius;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform ButtonParams {
    vec4  border_color;
    vec4  icon_color;
    vec2  aspect;         // width/height of the button quad
    float border_width;   // normalized 0..1
    float has_texture;    // 0 = solid fill, 1 = sample texture
} p;

float sdf_rounded_box_aspect(vec2 pos, vec2 half_size, float r) {
    vec2 q = abs(pos * half_size * 2.0) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2  half_size = vec2(0.5 * p.aspect.x, 0.5);
    float r_scaled  = v_radius * min(half_size.x, half_size.y);
    float dist      = sdf_rounded_box_aspect(v_local, half_size, r_scaled);
    float aa        = fwidth(dist);
    float mask      = 1.0 - smoothstep(-aa, aa, dist);

    vec4 fill;
    if (p.has_texture > 0.5) {
        fill = texture(u_tex, v_uv) * p.icon_color;
    } else {
        fill = v_color;
    }

    vec4 col;
    if (p.border_width > 0.0 && p.border_color.a > 0.0) {
        vec2  inner_half = half_size - p.border_width;
        float inner_r    = max(r_scaled - p.border_width, 0.0);
        float inner_dist = sdf_rounded_box_aspect(v_local, inner_half, inner_r);
        float border     = smoothstep(-aa, aa, inner_dist);
        col = mix(p.border_color, fill, border);
    } else {
        col = fill;
    }

    frag_color = vec4(col.rgb, col.a * mask);
}

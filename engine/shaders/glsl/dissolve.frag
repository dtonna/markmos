// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform DissolveParams {
    float progress;    // 0.0 = fully visible, 1.0 = fully dissolved
    float edge_width;  // glow edge width, e.g. 0.05
    vec4  edge_color;  // glow color (e.g. orange for fire)
    float _pad;
} params;

// Hash-based noise (no texture needed)
float hash(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i + vec2(0,0)), hash(i + vec2(1,0)), u.x),
        mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), u.x),
        u.y
    );
}

void main() {
    vec4 base = texture(u_tex, v_uv);

    float n = noise(v_uv * 8.0);  // dissolve pattern scale

    if (n < params.progress) discard;

    // Edge glow
    float edge = smoothstep(params.progress, params.progress + params.edge_width, n);
    vec3 col = mix(params.edge_color.rgb, base.rgb, edge);
    float a  = mix(params.edge_color.a,  base.a,   edge);

    frag_color = vec4(col, a);
}
// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform OutlineParams {
    vec4 outline_color;
    float thickness;    // in UV space, e.g. 0.005
    float _pad[3];
} params;

void main() {
    vec4 base = texture(u_tex, v_uv) * v_color;
    float a = base.a;

    vec2 t = vec2(params.thickness);
    float neighbors =
        texture(u_tex, v_uv + vec2( t.x,  0.0)).a +
        texture(u_tex, v_uv + vec2(-t.x,  0.0)).a +
        texture(u_tex, v_uv + vec2( 0.0,  t.y)).a +
        texture(u_tex, v_uv + vec2( 0.0, -t.y)).a +
        texture(u_tex, v_uv + vec2( t.x,  t.y)).a * 0.707 +
        texture(u_tex, v_uv + vec2(-t.x,  t.y)).a * 0.707 +
        texture(u_tex, v_uv + vec2( t.x, -t.y)).a * 0.707 +
        texture(u_tex, v_uv + vec2(-t.x, -t.y)).a * 0.707;

    // Edge = neighbor has alpha but current pixel doesn't
    float outline = clamp(neighbors, 0.0, 1.0) * (1.0 - a);
    frag_color = mix(base, vec4(params.outline_color.rgb, outline * params.outline_color.a), outline);
}
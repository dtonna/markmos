// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform GrayscaleParams {
    float amount;   // 0.0 = full color, 1.0 = full gray
    float _pad[3];
} params;

void main() {
    vec4 c = texture(u_tex, v_uv) * v_color;
    float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    c.rgb = mix(c.rgb, vec3(lum), params.amount);
    frag_color = c;
}
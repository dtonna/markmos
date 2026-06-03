// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_alpha_scale;

layout(binding = 0) uniform sampler2D u_tex;

const float SDF_SMOOTH = 0.125;
const float SDF_CENTER = 0.5;

void main() {
    float dist = texture(u_tex, v_uv).r;
    float alpha = smoothstep(SDF_CENTER - SDF_SMOOTH, SDF_CENTER + SDF_SMOOTH, dist);
    alpha *= v_alpha_scale;
    frag_color = vec4(v_color.rgb, v_color.a * alpha);
}

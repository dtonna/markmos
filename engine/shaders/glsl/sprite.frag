// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform texture2D u_tex;
layout(binding = 0) uniform sampler u_sam;

void main() {
    vec4 sampled = texture(sampler2D(u_tex, u_sam), v_uv);
    frag_color = sampled * v_color;
}

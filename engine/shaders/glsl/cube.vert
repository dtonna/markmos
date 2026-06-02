// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) in vec3 a_pos;
layout(location = 1) in uvec4 a_color;

layout(location = 0) out vec4 v_color;

layout(binding = 2) uniform Uniforms {
    mat4 mvp;
} u;

void main() {
    gl_Position = u.mvp * vec4(a_pos, 1.0);
    vec4 c = vec4(a_color);
    v_color = vec4(c.rgb / 255.0, 1.0);
}

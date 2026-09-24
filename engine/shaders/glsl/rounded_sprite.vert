// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// rounded_sprite.vert
#version 460

layout(location = 0) in vec2  a_position;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in vec4  a_color;
layout(location = 3) in vec2  a_local;
layout(location = 4) in vec2  a_radius_bw;    // x = radius, y = border_w
layout(location = 5) in vec4  a_border_color;

layout(location = 0) out vec2  v_uv;
layout(location = 1) out vec4  v_color;
layout(location = 2) out vec2  v_local;
layout(location = 3) out float v_radius;
layout(location = 4) out float v_border_w;
layout(location = 5) out vec4  v_border_color;

layout(binding = 1) uniform CameraUBO {
    mat4 view_proj;
} camera;

void main() {
    gl_Position   = camera.view_proj * vec4(a_position, 0.0, 1.0);
    v_uv          = a_uv;
    v_color       = a_color;
    v_local       = a_local;
    v_radius      = a_radius_bw.x;
    v_border_w    = a_radius_bw.y;
    v_border_color = a_border_color;
}
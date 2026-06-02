// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) in vec4 a_vert;  // xy = pos, zw = uv

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_alpha_scale;

layout(binding = 1) uniform CameraUBO {
    mat4 view_proj;
} camera;

void main() {
    vec4 pos = vec4(a_vert.xy, 0.0, 1.0);
    gl_Position = camera.view_proj * pos;
    v_uv = a_vert.zw;
    v_color = vec4(1.0);
    v_alpha_scale = 1.0;
}

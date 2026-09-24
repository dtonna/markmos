// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

// Fullscreen triangle — no VBO needed. Call draw(3, 1, 0, 0).

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    v_uv = uv;
}

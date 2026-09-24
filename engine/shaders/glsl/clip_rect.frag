// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform ClipRect {
    vec2 origin;  // screen space x, y
    vec2 size;    // w, h
} clip;

void main() {
    // Note: screen space position not available in fragment without passing from vertex
    // This shader expects clip rect in UV space (0-1) for simplicity
    if (v_uv.x < clip.origin.x || v_uv.x > clip.origin.x + clip.size.x ||
        v_uv.y < clip.origin.y || v_uv.y > clip.origin.y + clip.size.y) {
        discard;
    }

    frag_color = texture(u_tex, v_uv) * v_color;
}
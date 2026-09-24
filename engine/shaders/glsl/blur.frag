// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Gaussian blur fragment (two-pass separable: horizontal then vertical)
#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;

layout(binding = 0) uniform sampler2D u_texture;

layout(binding = 2) uniform BlurParams {
    float direction_x;   // 1.0 = horizontal, 0.0 = vertical
    float direction_y;   // 0.0 = horizontal, 1.0 = vertical
    float radius;        // blur strength in pixels
    float weights[7];    // Gaussian weights (7 taps = 15 samples with center)
    float _pad[2];
} params;

void main() {
    vec2 texel = vec2(params.direction_x, params.direction_y) * params.radius / vec2(textureSize(u_texture, 0));
    vec4 result = texture(u_texture, v_uv) * params.weights[0];

    for (int i = 1; i < 7; ++i) {
        vec2 offset = texel * float(i);
        result += texture(u_texture, v_uv + offset) * params.weights[i];
        result += texture(u_texture, v_uv - offset) * params.weights[i];
    }
    frag_color = result;
}

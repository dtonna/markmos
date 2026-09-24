// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;

layout(binding = 0) uniform sampler2D u_texture;

layout(binding = 2) uniform ColorGrade {
    float brightness;  // 0..2,  default 1.0
    float contrast;    // 0..2,  default 1.0
    float saturation;  // 0..2,  default 1.0
    mat3  hue_matrix;  // precomputed hue rotation matrix
} grade;

void main() {
    vec4 c = texture(u_texture, v_uv);

    // Hue shift (precomputed matrix)
    c.rgb = grade.hue_matrix * c.rgb;

    // Saturation
    float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    c.rgb = mix(vec3(lum), c.rgb, grade.saturation);

    // Contrast + Brightness
    c.rgb = (c.rgb - 0.5) * grade.contrast + 0.5;
    c.rgb *= grade.brightness;

    frag_color = vec4(clamp(c.rgb, 0.0, 1.0), c.a);
}

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec4 v_color;

void main() {
    vec3 light = normalize(vec3(1.0, 1.0, 0.5));
    float diff = max(0.2, dot(normalize(v_color.xyz), light));
    frag_color = vec4(v_color.rgb * diff, 1.0);
}

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// normal_map.frag — engine port of lab_proc/lab_png (identical lighting,
// the normal source differs only host-side): light albedo with a
// tangent-space normal map (+Y up, UNORM linear data, never sRGB).
// Second texture binds at logical slot 2 (Metal quirk: index 1 aliases
// slot 0 — see lab notes). NOTE: the Vulkan backend currently supports a
// single fragment texture per pipeline, so this material is Metal-ready
// and Vulkan-deferred until the backend gains slot-1 binding.

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;
layout(binding = 1) uniform sampler2D u_norm;

layout(binding = 2) uniform NormalParams {
    vec4 light_dir; // xyz = direction TO light, w = intensity
    vec4 misc;      // x = ambient, y = spec strength, z = unused, w = spare
} params;

void main() {
    vec3 n = normalize(texture(u_norm, v_uv).rgb * 2.0 - 1.0);

    vec3 ldir = normalize(params.light_dir.xyz);
    vec4 base = texture(u_tex, v_uv) * v_color;

    float diff = max(dot(n, ldir), 0.0) * params.light_dir.w;

    vec3 h = normalize(ldir + vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, h), 0.0), 32.0) * params.misc.y;

    vec3 col = base.rgb * (params.misc.x + diff) + spec;
    frag_color = vec4(col, base.a);
}

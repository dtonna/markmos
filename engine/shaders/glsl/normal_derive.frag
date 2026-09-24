// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// normal_derive.frag — engine port of lab_derive: derive a normal in-shader
// from a Sobel gradient of the albedo luminance, then Blinn-Phong light it.
// No second texture needed. Same lighting math as normal_map.

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform NormalParams {
    vec4 light_dir; // xyz = direction TO light, w = intensity
    vec4 misc;      // x = ambient, y = spec strength, z = height_scale, w = spare
} params;

float lab_luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(u_tex, 0));

    // Sobel 3x3 on albedo luminance
    float tl = lab_luma(texture(u_tex, v_uv + texel * vec2(-1.0, -1.0)).rgb);
    float t  = lab_luma(texture(u_tex, v_uv + texel * vec2( 0.0, -1.0)).rgb);
    float tr = lab_luma(texture(u_tex, v_uv + texel * vec2( 1.0, -1.0)).rgb);
    float l  = lab_luma(texture(u_tex, v_uv + texel * vec2(-1.0,  0.0)).rgb);
    float r  = lab_luma(texture(u_tex, v_uv + texel * vec2( 1.0,  0.0)).rgb);
    float bl = lab_luma(texture(u_tex, v_uv + texel * vec2(-1.0,  1.0)).rgb);
    float b  = lab_luma(texture(u_tex, v_uv + texel * vec2( 0.0,  1.0)).rgb);
    float br = lab_luma(texture(u_tex, v_uv + texel * vec2( 1.0,  1.0)).rgb);

    float dhdx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
    float dhdy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);
    vec3 n = normalize(vec3(-dhdx * params.misc.z, -dhdy * params.misc.z, 1.0));

    vec3 ldir = normalize(params.light_dir.xyz);
    vec4 base = texture(u_tex, v_uv) * v_color;

    float diff = max(dot(n, ldir), 0.0) * params.light_dir.w;

    // Blinn-Phong, viewer looks down +z
    vec3 h = normalize(ldir + vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, h), 0.0), 32.0) * params.misc.y;

    vec3 col = base.rgb * (params.misc.x + diff) + spec;
    frag_color = vec4(col, base.a);
}

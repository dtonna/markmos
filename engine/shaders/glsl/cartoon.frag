// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// cartoon.frag — engine port of lab_cartoon: toon shading on albedo.
// Quantized diffuse bands + one-step specular + Sobel ink outlines
// (gradient magnitude catches ALL print edges).

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;

layout(binding = 2) uniform CartoonParams {
    vec4 light_dir; // xyz = direction TO light, w = intensity
    vec4 misc;      // x = ambient, y = spec on/off, z = height_scale, w = spare
    vec4 toon;      // x = bands (2..5), y = ink threshold, z = ink strength, w = spare
} params;

float cartoon_luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(u_tex, 0));

    // Sobel 3x3 on albedo luminance: gradient magnitude = ink mask,
    // gradient direction = shade normal (emboss look).
    float tl = cartoon_luma(texture(u_tex, v_uv + texel * vec2(-1.0, -1.0)).rgb);
    float t  = cartoon_luma(texture(u_tex, v_uv + texel * vec2( 0.0, -1.0)).rgb);
    float tr = cartoon_luma(texture(u_tex, v_uv + texel * vec2( 1.0, -1.0)).rgb);
    float l  = cartoon_luma(texture(u_tex, v_uv + texel * vec2(-1.0,  0.0)).rgb);
    float r  = cartoon_luma(texture(u_tex, v_uv + texel * vec2( 1.0,  0.0)).rgb);
    float bl = cartoon_luma(texture(u_tex, v_uv + texel * vec2(-1.0,  1.0)).rgb);
    float b  = cartoon_luma(texture(u_tex, v_uv + texel * vec2( 0.0,  1.0)).rgb);
    float br = cartoon_luma(texture(u_tex, v_uv + texel * vec2( 1.0,  1.0)).rgb);

    float dhdx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
    float dhdy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);
    float edge = length(vec2(dhdx, dhdy));

    vec3 n = normalize(vec3(-dhdx * params.misc.z, -dhdy * params.misc.z, 1.0));
    vec3 ldir = normalize(params.light_dir.xyz);
    vec4 base = texture(u_tex, v_uv) * v_color;

    // Quantized diffuse: N steps over [ambient, ambient + intensity].
    float bands = max(params.toon.x, 2.0);
    float d = max(dot(n, ldir), 0.0) * params.light_dir.w;
    float shade = params.misc.x + (floor(d * bands) / bands) * (1.0 - params.misc.x);

    // One-step specular (cel highlight).
    vec3 h = normalize(ldir + vec3(0.0, 0.0, 1.0));
    float spec = step(0.85, max(dot(n, h), 0.0)) * params.misc.y;

    // Ink: darken where the print edge is strong.
    float ink = (1.0 - smoothstep(params.toon.y, params.toon.y * 2.0, edge)) * params.toon.z;

    vec3 col = base.rgb * shade + spec;
    col *= 1.0 - ink;
    frag_color = vec4(col, base.a);
}

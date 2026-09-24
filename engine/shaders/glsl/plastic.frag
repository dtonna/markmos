// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// plastic.frag — engine port of lab_plastic: glossy plastic card look.
// Wrapped soft diffuse (never fully black) + tight glossy highlight +
// fresnel-weighted clearcoat lobe + cool rim sheen.
// Second texture (normal map) binds at logical slot 2 — same Vulkan
// single-texture limitation as normal_map (Metal-ready, Vulkan-deferred).

#version 460

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(binding = 0) uniform sampler2D u_tex;
layout(binding = 1) uniform sampler2D u_norm;

layout(binding = 2) uniform PlasticParams {
    vec4 light_dir; // xyz = direction TO light, w = intensity
    vec4 misc;      // x = ambient, y = spec strength, z = unused, w = spare
    vec4 plastic;   // x = clearcoat, y = fresnel strength, z = wrap 0..1, w = shininess
} params;

void main() {
    vec3 n = normalize(texture(u_norm, v_uv).rgb * 2.0 - 1.0);

    // Card/view space == tangent space for flat XY sprites (no TBN needed).
    vec3 ldir = normalize(params.light_dir.xyz);
    vec4 base = texture(u_tex, v_uv) * v_color;
    vec3 v = vec3(0.0, 0.0, 1.0); // viewer looks down +z

    float intensity = max(params.light_dir.w, 0.0);

    // Wrapped diffuse — plastic never goes fully black in shadow.
    float wrap = clamp(params.plastic.z, 0.0, 1.0);
    float d = clamp((dot(n, ldir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    d *= intensity;

    // Glossy highlight (broad) + clearcoat lobe (tight, top layer).
    vec3 h = normalize(ldir + v);
    float ndh = max(dot(n, h), 0.0);
    float shine = max(params.plastic.w, 8.0);
    float gloss = pow(ndh, shine) * params.misc.y * intensity;
    float coat = pow(ndh, shine * 5.0) * params.plastic.x * intensity;

    // Stylized Fresnel rim — cool sheen at grazing angles.
    float fres = pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 rim_color = vec3(0.65, 0.8, 1.0);
    vec3 rim = rim_color * fres * params.plastic.y * (0.3 + 0.7 * coat);

    vec3 col = base.rgb * (params.misc.x + d) + (gloss + coat) + rim;
    frag_color = vec4(col, base.a);
}

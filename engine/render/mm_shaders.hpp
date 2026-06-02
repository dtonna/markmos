// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstddef>

namespace shader {

// ─── Sprite Vertex ───────────────────────────────────────────────
static constexpr const char* sprite_vertex_msl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct SpriteVertIn {
    half2  position [[attribute(0)]];
    half2  uv       [[attribute(1)]];
    float4 color    [[attribute(2)]];
};

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct CameraUBO {
    float4x4 view_proj;
};

vertex VSOutput sprite_vertex_main(
    SpriteVertIn vert [[stage_in]],
    constant CameraUBO& camera [[buffer(1)]]
) {
    VSOutput out;
    float4 pos = float4(float2(vert.position), 0.0, 1.0);
    out.position = camera.view_proj * pos;
    out.uv = float2(vert.uv);
    out.color = vert.color;
    return out;
}
)msl";

static constexpr size_t sprite_vertex_msl_size = sizeof(sprite_vertex_msl);

// ─── Sprite Fragment ─────────────────────────────────────────────
static constexpr const char* sprite_fragment_msl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

fragment float4 sprite_fragment_main(
    VSOutput input [[stage_in]],
    texture2d<float> tex [[texture(0)]],
    sampler samp [[sampler(0)]]
) {
    float4 sampled = tex.sample(samp, input.uv);
    return sampled * input.color;
}
)msl";

static constexpr size_t sprite_fragment_msl_size = sizeof(sprite_fragment_msl);

// ─── SDF Vertex ──────────────────────────────────────────────────
static constexpr const char* sdf_vertex_msl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VertIn {
    half2  position [[attribute(0)]];
    half2  uv       [[attribute(1)]];
    float4 color    [[attribute(2)]];
};

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float  alpha_scale;
};

struct CameraUBO {
    float4x4 view_proj;
};

vertex VSOutput sdf_vertex_main(
    VertIn vert [[stage_in]],
    constant CameraUBO& camera [[buffer(1)]]
) {
    VSOutput out;
    float4 pos = float4(float2(vert.position), 0.0, 1.0);
    out.position = camera.view_proj * pos;
    out.uv = float2(vert.uv);
    out.color = float4(1.0);
    out.alpha_scale = 1.0;
    return out;
}
)msl";

static constexpr size_t sdf_vertex_msl_size = sizeof(sdf_vertex_msl);

// ─── SDF Fragment ────────────────────────────────────────────────
static constexpr const char* sdf_fragment_msl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float  alpha_scale;
};

constant float SDF_SMOOTH = 0.125;
constant float SDF_CENTER = 0.5;

fragment float4 sdf_fragment_main(
    VSOutput input [[stage_in]],
    texture2d<float> sdf_tex [[texture(0)]],
    sampler samp [[sampler(0)]]
) {
    float dist = sdf_tex.sample(samp, input.uv).r;
    float alpha = smoothstep(SDF_CENTER - SDF_SMOOTH, SDF_CENTER + SDF_SMOOTH, dist);
    alpha *= input.alpha_scale;
    return float4(input.color.rgb, input.color.a * alpha);
}
)msl";

static constexpr size_t sdf_fragment_msl_size = sizeof(sdf_fragment_msl);

// ─── Particle Vertex ─────────────────────────────────────────────
static constexpr const char* particle_vertex_msl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct CameraUBO {
    float4x4 view_proj;
};

struct AtlasInfo {
    float2 tile_size;
    float2 atlas_size;
};

constant float2 CORNERS[4] = {
    float2(-0.5, -0.5),
    float2( 0.5, -0.5),
    float2(-0.5,  0.5),
    float2( 0.5,  0.5),
};

constant float2 UVS[4] = {
    float2(0.0, 0.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 1.0),
};

vertex VSOutput particle_vertex_main(
    const device float4* instances [[buffer(1)]],
    constant CameraUBO& camera [[buffer(2)]],
    constant AtlasInfo& atlas [[buffer(3)]],
    uint vid [[vertex_id]],
    uint iid [[instance_id]]
) {
    float4 pos_scale = instances[iid * 4 + 0];
    float4 color_atlas = instances[iid * 4 + 1];
    float2 extra = instances[iid * 4 + 2].xy;
    float rotation = extra.x;
    float alpha = extra.y;

    float c = cos(rotation);
    float s = sin(rotation);
    float2x2 rot = float2x2(c, -s, s, c);

    float2 local = CORNERS[vid] * pos_scale.w;
    float2 world = rot * local + pos_scale.xy;

    float4 pos = float4(world, 0.0, 1.0);
    VSOutput out;
    out.position = camera.view_proj * pos;

    float atlas_idx = color_atlas.a;
    float base_u = atlas.tile_size.x * floor(fmod(atlas_idx, atlas.atlas_size.x / atlas.tile_size.x));
    float base_v = atlas.tile_size.y * floor(atlas_idx * atlas.tile_size.x / atlas.atlas_size.x);
    out.uv = float2(base_u, base_v) + UVS[vid] * atlas.tile_size;

    out.color = float4(color_atlas.rgb, alpha);
    return out;
}
)msl";

static constexpr size_t particle_vertex_msl_size = sizeof(particle_vertex_msl);

// ─── Particle Fragment ───────────────────────────────────────────
static constexpr const char* particle_fragment_msl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

fragment float4 particle_fragment_main(
    VSOutput input [[stage_in]],
    texture2d<float> tex [[texture(0)]],
    sampler samp [[sampler(0)]]
) {
    float4 sampled = tex.sample(samp, input.uv);
    return sampled * input.color;
}
)msl";

static constexpr size_t particle_fragment_msl_size = sizeof(particle_fragment_msl);

// ─── Cube Vertex ─────────────────────────────────────────────────
static constexpr const char* cube_vertex_msl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float4 color;
};

struct Uniforms {
    float4x4 mvp;
};

vertex VSOutput cube_vertex_main(
    const device float3* pos  [[buffer(0)]],
    const device uchar4* col  [[buffer(1)]],
    constant Uniforms&   u    [[buffer(2)]],
    uint vid [[vertex_id]]
) {
    VSOutput out;
    out.position = u.mvp * float4(pos[vid], 1.0);
    float4 c = float4(col[vid]);
    out.color = float4(c.rgb / 255.0, 1.0);
    return out;
}
)msl";

static constexpr size_t cube_vertex_msl_size = sizeof(cube_vertex_msl);

// ─── Cube Fragment ───────────────────────────────────────────────
static constexpr const char* cube_fragment_msl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float4 color;
};

fragment float4 cube_fragment_main(VSOutput input [[stage_in]]) {
    float3 light = normalize(float3(1.0, 1.0, 0.5));
    float diff = max(0.2, dot(normalize(input.color.xyz), light));
    return float4(input.color.rgb * diff, 1.0);
}
)msl";

static constexpr size_t cube_fragment_msl_size = sizeof(cube_fragment_msl);

} // namespace shader

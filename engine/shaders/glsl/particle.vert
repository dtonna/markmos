// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(set = 0, binding = 2) uniform CameraUBO
{
    mat4 view_proj;
} camera;

layout(set = 0, binding = 3) uniform AtlasInfo
{
    vec2 tile_size;
    vec2 atlas_size;
} atlas;

/*
    ParticleInstance

    float4 pos_scale
    float4 color_atlas
    float4 extra
*/

layout(location = 0) in vec4 in_pos_scale;
layout(location = 1) in vec4 in_color_atlas;
layout(location = 2) in vec4 in_extra;

const vec2 CORNERS[4] = vec2[](
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2(-0.5,  0.5),
    vec2( 0.5,  0.5)
);

const vec2 UVS[4] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0)
);

void main()
{
    uint vid = uint(gl_VertexIndex) & 3u;

    float rotation = in_extra.x;
    float alpha = in_extra.y;

    float c = cos(rotation);
    float s = sin(rotation);
    mat2 rot = mat2(c, s, -s, c);

    vec2 local_pos = CORNERS[vid] * in_pos_scale.w;
    vec2 world_pos = (rot * local_pos) + in_pos_scale.xy;

    gl_Position = camera.view_proj * vec4(world_pos, 0.0, 1.0);

    float atlas_idx = in_color_atlas.a;
    float tiles_per_row = atlas.atlas_size.x / atlas.tile_size.x;
    float base_u = atlas.tile_size.x * floor(mod(atlas_idx, tiles_per_row));
    float base_v = atlas.tile_size.y * floor(atlas_idx / tiles_per_row);

    v_uv = vec2(base_u, base_v) + UVS[vid] * atlas.tile_size;
    v_color = vec4(in_color_atlas.rgb, alpha);
}

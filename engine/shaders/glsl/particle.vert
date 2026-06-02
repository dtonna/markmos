// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#version 460

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(binding = 1) uniform CameraUBO {
    mat4 view_proj;
} camera;

layout(binding = 2) uniform AtlasInfo {
    vec2 tile_size;
    vec2 atlas_size;
} atlas;

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

void main() {
    // instance data: quad index packed in gl_VertexIndex
    int vi = gl_VertexIndex / 4;
    int ci = gl_VertexIndex % 4;

    // vec4 pos_scale from instance buffer
    // We use gl_VertexIndex to access instance data via buffer
    // (In the engine, instance data is a buffer at binding 1)
    // For now, simple quad per-vertex path:
    vec2 local = CORNERS[ci];
    vec2 world = local;
    vec4 pos = vec4(world, 0.0, 1.0);
    gl_Position = camera.view_proj * pos;
    v_uv = UVS[ci];
    v_color = vec4(1.0);
}

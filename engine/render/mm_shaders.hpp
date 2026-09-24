// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstddef>

namespace shader {

// ─── Sprite Vertex ───────────────────────────────────────────────
static constexpr char   sprite_vertex_msl[]              = R"msl(
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

static constexpr size_t sprite_vertex_msl_size           = sizeof(sprite_vertex_msl);

// ─── Sprite Fragment ─────────────────────────────────────────────
static constexpr char   sprite_fragment_msl[]            = R"msl(
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

static constexpr size_t sprite_fragment_msl_size         = sizeof(sprite_fragment_msl);

// ─── SDF Vertex ──────────────────────────────────────────────────
static constexpr char   sdf_vertex_msl[]                 = R"msl(
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
    out.color = vert.color;
    out.alpha_scale = 1.0;
    return out;
}
)msl";

static constexpr size_t sdf_vertex_msl_size              = sizeof(sdf_vertex_msl);

// ─── SDF Fragment ────────────────────────────────────────────────
static constexpr char   sdf_fragment_msl[]               = R"msl(
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

static constexpr size_t sdf_fragment_msl_size            = sizeof(sdf_fragment_msl);

// ─── Particle Vertex ─────────────────────────────────────────────
static constexpr char   particle_vertex_msl[]            = R"msl(
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

static constexpr size_t particle_vertex_msl_size         = sizeof(particle_vertex_msl);

// ─── Particle Fragment ───────────────────────────────────────────
static constexpr char   particle_fragment_msl[]          = R"msl(
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

static constexpr size_t particle_fragment_msl_size       = sizeof(particle_fragment_msl);

// ─── Cube Vertex ─────────────────────────────────────────────────
static constexpr char   cube_vertex_msl[]                = R"msl(
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

static constexpr size_t cube_vertex_msl_size             = sizeof(cube_vertex_msl);

// ─── Cube Fragment ───────────────────────────────────────────────
static constexpr char   cube_fragment_msl[]              = R"msl(
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

static constexpr size_t cube_fragment_msl_size           = sizeof(cube_fragment_msl);

// ─── Rounded Sprite Vertex ─────────────────────────────────────────
static constexpr char   rounded_sprite_vertex_msl[]      = R"msl(
#include <metal_stdlib>
using namespace metal;

struct RoundedVertIn {
    half2  position     [[attribute(0)]];
    half2  uv           [[attribute(1)]];
    float4 color        [[attribute(2)]];
    half2  local        [[attribute(3)]];
    half2  radius       [[attribute(4)]];
    float4 border_color [[attribute(5)]];
};

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float2 local;
    float  radius;
    float  border_w;
    float4 border_color;
};

struct CameraUBO {
    float4x4 view_proj;
};

vertex VSOutput rounded_sprite_vertex_main(
    RoundedVertIn        vert   [[stage_in]],
    constant CameraUBO&  camera [[buffer(1)]]
) {
    VSOutput out;
    out.position    = camera.view_proj * float4(float2(vert.position), 0.0, 1.0);
    out.uv          = float2(vert.uv);
    out.color       = vert.color;
    out.local       = float2(vert.local);
    out.radius      = float(vert.radius.x);
    out.border_w    = float(vert.radius.y);
    out.border_color = vert.border_color;
    return out;
}
)msl";

static constexpr size_t rounded_sprite_vertex_msl_size   = sizeof(rounded_sprite_vertex_msl);

// ─── Rounded Sprite Fragment ───────────────────────────────────────
static constexpr char   rounded_sprite_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float2 local;
    float  radius;
    float  border_w;
    float4 border_color;
};

struct RoundedParams {
    float time;
    float glow_intensity;
    float glow_width;
    float glow_pulse_freq;
    float sdf_aa_scale;   // default 1.0 (like button.frag); lower = softer
    float _pad0;
    float _pad1;
    float _pad2;
    float4 glow_color;
};

// Function constants for pipeline specialization
constant bool HAS_GLOW     [[function_constant(0)]];

float sdf_rounded_box(float2 p, float r) {
    float2 q = abs(p) - 0.5 + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
fragment float4 rounded_sprite_fragment_main(
    VSOutput         input [[stage_in]],
    texture2d<float> tex   [[texture(0)]],
    sampler          samp  [[sampler(0)]],
    constant RoundedParams& params [[buffer(2)]]
) {
    float dist = sdf_rounded_box(input.local, input.radius);
    float aa_raw = fwidth(dist);
    float aa     = max(aa_raw * params.sdf_aa_scale, 0.001);  // avoid zero derivative

    // Procedural SDF rounded box has edge at dist=0 (not 0.5 like baked SDF textures)
    float outer_mask =
        1.0 - smoothstep(-aa, aa, dist);

    float4 fill =
        tex.sample(samp, input.uv) * input.color;

    // Border from per-vertex only (no UBO override — avoids batch-wide border)
    float bw = input.border_w;
    float border_mask = 0.0;
    if (bw > 0.0) {
        border_mask =
        1.0 - smoothstep(
            -bw - aa,
            -bw + aa,
            dist
        );
        border_mask =
        max(outer_mask - border_mask, 0.0);
    }

    //-----------------------------------------
    // Composite (straight-alpha premul over)
    //-----------------------------------------

    float4 result = fill * outer_mask;

    // Apply border if border width > 0 (premul over)
    if (bw > 0.0) {
        float bm = border_mask * input.border_color.a;
        result.rgb = mix(result.rgb, input.border_color.rgb, bm);
        result.a   = max(result.a, bm);
    }

    if (HAS_GLOW) {
        float gw = params.glow_width;
        float pulse = 0.5 + 0.5 * sin(params.time * params.glow_pulse_freq);
        float border_w = max(bw, 0.001);
        float glow_mask = smoothstep(-border_w, 0.0, dist) * (1.0 - smoothstep(0.0, gw, dist));
        float glow_alpha = params.glow_color.a * params.glow_intensity * pulse;
        float3 glow_rgb  = params.glow_color.rgb * glow_alpha * glow_mask;
        result.rgb = clamp(result.rgb + glow_rgb, 0.0, 1.0);
        result.a   = max(result.a, glow_alpha * glow_mask);
    }

    return result;
}
)msl";

static constexpr size_t rounded_sprite_fragment_msl_size = sizeof(rounded_sprite_fragment_msl);

// ─── Screen Quad Vertex (shared base for all post-process) ────────
static constexpr char   screen_quad_vertex_msl[]         = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
};

// Fullscreen triangle — no VBO needed, call draw(3, 1, 0, 0)
vertex VSOutput screen_quad_vertex_main(uint vid [[vertex_id]]) {
    VSOutput out;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    out.uv       = float2(uv.x, 1.0 - uv.y);  // flip Y for Metal NDC
    return out;
}
)msl";

static constexpr size_t screen_quad_vertex_msl_size      = sizeof(screen_quad_vertex_msl);

// ─── Blur Fragment (Gaussian two-pass) ───────────────────────────
static constexpr char   blur_fragment_msl[]              = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
};

struct BlurParams {
    float direction_x;   // 1.0 = horizontal, 0.0 = vertical
    float direction_y;   // 0.0 = horizontal, 1.0 = vertical
    float radius;        // blur strength in pixels
    float weights[7];    // Gaussian weights (7 taps = 15 samples with center)
    float _pad[2];
};

fragment float4 blur_fragment_main(
    VSOutput          input  [[stage_in]],
    texture2d<float>  tex    [[texture(0)]],
    sampler           samp   [[sampler(0)]],
    constant BlurParams& p   [[buffer(2)]]
) {
    float2 texel  = float2(p.direction_x, p.direction_y) * p.radius / float2(tex.get_width(), tex.get_height());
    float4 result = tex.sample(samp, input.uv) * p.weights[0];

    for (int i = 1; i < 7; ++i) {
        float2 offset = texel * float(i);
        result += tex.sample(samp, input.uv + offset) * p.weights[i];
        result += tex.sample(samp, input.uv - offset) * p.weights[i];
    }
    return result;
}
)msl";

static constexpr size_t blur_fragment_msl_size           = sizeof(blur_fragment_msl);

// ─── Color Grade Fragment ─────────────────────────────────────────
static constexpr char   color_grade_fragment_msl[]       = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
};

struct ColorGrade {
    float brightness;   // default 1.0
    float contrast;     // default 1.0
    float saturation;   // default 1.0
    float3x3 hue_matrix; // precomputed hue rotation matrix (identity if no shift)
};

fragment float4 color_grade_fragment_main(
    VSOutput            input [[stage_in]],
    texture2d<float>    tex   [[texture(0)]],
    sampler             samp  [[sampler(0)]],
    constant ColorGrade& g    [[buffer(2)]]
) {
    float4 c = tex.sample(samp, input.uv);

    // Hue shift (precomputed matrix)
    c.rgb = g.hue_matrix * c.rgb;

    // Saturation
    float lum = dot(c.rgb, float3(0.299, 0.587, 0.114));
    c.rgb = mix(float3(lum), c.rgb, g.saturation);

    // Contrast + Brightness
    c.rgb = (c.rgb - 0.5) * g.contrast + 0.5;
    c.rgb *= g.brightness;

    return float4(clamp(c.rgb, 0.0, 1.0), c.a);
}
)msl";

static constexpr size_t color_grade_fragment_msl_size    = sizeof(color_grade_fragment_msl);

// ─── Sprite Outline Fragment ──────────────────────────────────────
static constexpr char   sprite_outline_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct OutlineParams {
    float4 outline_color;
    float  thickness;    // in UV space, e.g. 0.005
    float  _pad[3];
};

fragment float4 sprite_outline_fragment_main(
    VSOutput              input [[stage_in]],
    texture2d<float>      tex   [[texture(0)]],
    sampler               samp  [[sampler(0)]],
    constant OutlineParams& p   [[buffer(2)]]
) {
    float4 base = tex.sample(samp, input.uv) * input.color;
    float  a    = base.a;

    float2 t = float2(p.thickness);
    float neighbors =
        tex.sample(samp, input.uv + float2( t.x,  0.0)).a +
        tex.sample(samp, input.uv + float2(-t.x,  0.0)).a +
        tex.sample(samp, input.uv + float2( 0.0,  t.y)).a +
        tex.sample(samp, input.uv + float2( 0.0, -t.y)).a +
        tex.sample(samp, input.uv + float2( t.x,  t.y)).a * 0.707 +
        tex.sample(samp, input.uv + float2(-t.x,  t.y)).a * 0.707 +
        tex.sample(samp, input.uv + float2( t.x, -t.y)).a * 0.707 +
        tex.sample(samp, input.uv + float2(-t.x, -t.y)).a * 0.707;

    // Edge = neighbor has alpha but current pixel doesn't
    float outline = clamp(neighbors, 0.0, 1.0) * (1.0 - a);
    return mix(base, float4(p.outline_color.rgb, outline * p.outline_color.a), outline);
}
)msl";

static constexpr size_t sprite_outline_fragment_msl_size = sizeof(sprite_outline_fragment_msl);

// ─── Clip Rect Fragment ───────────────────────────────────────────
static constexpr char   clip_rect_fragment_msl[]         = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct ClipRect {
    float2 origin;  // screen space x, y
    float2 size;    // w, h
};

fragment float4 clip_rect_fragment_main(
    VSOutput          input [[stage_in]],
    texture2d<float>  tex   [[texture(0)]],
    sampler           samp  [[sampler(0)]],
    constant ClipRect& r    [[buffer(2)]]
) {
    float2 pos = input.position.xy;
    if (pos.x < r.origin.x || pos.x > r.origin.x + r.size.x ||
        pos.y < r.origin.y || pos.y > r.origin.y + r.size.y)
        discard_fragment();

    return tex.sample(samp, input.uv) * input.color;
}
)msl";

static constexpr size_t clip_rect_fragment_msl_size      = sizeof(clip_rect_fragment_msl);

// ─── Dissolve Fragment ────────────────────────────────────────────
static constexpr char   dissolve_fragment_msl[]          = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct DissolveParams {
    float  progress;    // 0.0 = fully visible, 1.0 = fully dissolved
    float  edge_width;  // glow edge width, e.g. 0.05
    float4 edge_color;  // glow color (e.g. orange for fire)
};

// Hash-based noise (no texture needed)
float hash(float2 p) {
    p = fract(p * float2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

float noise(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i + float2(0,0)), hash(i + float2(1,0)), u.x),
        mix(hash(i + float2(0,1)), hash(i + float2(1,1)), u.x),
        u.y
    );
}

fragment float4 dissolve_fragment_main(
    VSOutput               input [[stage_in]],
    texture2d<float>       tex   [[texture(0)]],
    sampler                samp  [[sampler(0)]],
    constant DissolveParams& p   [[buffer(2)]]
) {
    float4 base = tex.sample(samp, input.uv) * input.color;

    float n = noise(input.uv * 8.0);  // dissolve pattern scale

    if (n < p.progress) discard_fragment();

    // Edge glow
    float edge = smoothstep(p.progress, p.progress + p.edge_width, n);
    float3 col = mix(p.edge_color.rgb, base.rgb, edge);
    float  a   = mix(p.edge_color.a,  base.a,   edge);

    return float4(col, a);
}
)msl";

static constexpr size_t dissolve_fragment_msl_size       = sizeof(dissolve_fragment_msl);

// ─── Grayscale Fragment ───────────────────────────────────────────
static constexpr char   grayscale_fragment_msl[]         = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct GrayscaleParams {
    float amount;   // 0.0 = full color, 1.0 = full gray
    float _pad[3];
};

fragment float4 grayscale_fragment_main(
    VSOutput               input [[stage_in]],
    texture2d<float>       tex   [[texture(0)]],
    sampler                samp  [[sampler(0)]],
    constant GrayscaleParams& p  [[buffer(2)]]
) {
    float4 c   = tex.sample(samp, input.uv) * input.color;
    float  lum = dot(c.rgb, float3(0.299, 0.587, 0.114));
    c.rgb = mix(c.rgb, float3(lum), p.amount);
    return c;
}
)msl";

static constexpr size_t grayscale_fragment_msl_size      = sizeof(grayscale_fragment_msl);

// ─── Button Fragment ──────────────────────────────────────────────
static constexpr char   button_fragment_msl[]            = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float2 local;
    float  radius;
};

struct ButtonParams {
    float4 border_color;
    float4 icon_color;
    float2 aspect;       // width/height
    float  border_width; // normalized 0..1
    float  has_texture;  // 0 = solid color, 1 = sample texture
};

float sdf_rounded_box_aspect(float2 p, float2 half_size, float r) {
    float2 q = abs(p * half_size * 2.0) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

fragment float4 button_fragment_main(
    VSOutput             input [[stage_in]],
    texture2d<float>     tex   [[texture(0)]],
    sampler              samp  [[sampler(0)]],
    constant ButtonParams& p   [[buffer(2)]]
) {
    float2 half_size = float2(0.5 * p.aspect.x, 0.5);
    float  r_scaled  = input.radius * min(half_size.x, half_size.y);
    float  dist      = sdf_rounded_box_aspect(input.local, half_size, r_scaled);
    float  aa        = fwidth(dist); // abs(dfdx(dist)) + abs(dfdy(dist));
    float  mask      = 1.0 - smoothstep(-aa, aa, dist);

    float4 fill;
    if (p.has_texture > 0.5) {
        fill = tex.sample(samp, input.uv) * p.icon_color;
    } else {
        fill = input.color;
    }

    float4 col;
    if (p.border_width > 0.0 && p.border_color.a > 0.0) {
        float2 inner_half = half_size - p.border_width;
        float  inner_r    = max(r_scaled - p.border_width, 0.0);
        float  inner_dist = sdf_rounded_box_aspect(input.local, inner_half, inner_r);
        float  border     = smoothstep(-aa, aa, inner_dist);
        col = mix(p.border_color, fill, border);
    } else {
        col = fill;
    }

    return float4(col.rgb, col.a * mask);
}
)msl";

static constexpr size_t button_fragment_msl_size         = sizeof(button_fragment_msl);

// ─── Normal Derive Fragment (Sobel normal from albedo + Blinn-Phong) ─
// Engine port of lab_derive. No second texture needed.
static constexpr char   normal_derive_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct NormalParams {
    float4 light_dir;  // xyz = direction TO light, w = intensity
    float4 misc;       // x = ambient, y = spec strength, z = height_scale, w = spare
};

float lab_luma(float3 c) {
    return dot(c, float3(0.299, 0.587, 0.114));
}

fragment float4 normal_derive_fragment_main(
    VSOutput                input [[stage_in]],
    texture2d<float>        tex   [[texture(0)]],
    sampler                 samp  [[sampler(0)]],
    constant NormalParams&  p     [[buffer(2)]]
) {
    float2 texel = 1.0 / float2(tex.get_width(), tex.get_height());

    float tl = lab_luma(tex.sample(samp, input.uv + texel * float2(-1, -1)).rgb);
    float t  = lab_luma(tex.sample(samp, input.uv + texel * float2( 0, -1)).rgb);
    float tr = lab_luma(tex.sample(samp, input.uv + texel * float2( 1, -1)).rgb);
    float l  = lab_luma(tex.sample(samp, input.uv + texel * float2(-1,  0)).rgb);
    float r  = lab_luma(tex.sample(samp, input.uv + texel * float2( 1,  0)).rgb);
    float bl = lab_luma(tex.sample(samp, input.uv + texel * float2(-1,  1)).rgb);
    float b  = lab_luma(tex.sample(samp, input.uv + texel * float2( 0,  1)).rgb);
    float br = lab_luma(tex.sample(samp, input.uv + texel * float2( 1,  1)).rgb);

    float dhdx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
    float dhdy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);
    float3 n = normalize(float3(-dhdx * p.misc.z, -dhdy * p.misc.z, 1.0));

    float3 ldir = normalize(p.light_dir.xyz);
    float4 base = tex.sample(samp, input.uv) * input.color;

    float diff = max(dot(n, ldir), 0.0) * p.light_dir.w;

    float3 h = normalize(ldir + float3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, h), 0.0), 32.0) * p.misc.y;

    float3 col = base.rgb * (p.misc.x + diff) + spec;
    return float4(col, base.a);
}
)msl";

static constexpr size_t normal_derive_fragment_msl_size = sizeof(normal_derive_fragment_msl);

// ─── Normal Map Fragment (tangent-space normal texture + Blinn-Phong) ─
// Engine port of lab_proc/lab_png (identical lighting; the normal source
// differs only host-side). Second texture binds at logical slot 2
// (Metal quirk: index 1 aliases slot 0).
static constexpr char   normal_map_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct NormalParams {
    float4 light_dir;  // xyz = direction TO light, w = intensity
    float4 misc;       // x = ambient, y = spec strength, z = unused, w = spare
};

fragment float4 normal_map_fragment_main(
    VSOutput                input [[stage_in]],
    texture2d<float>        albedo [[texture(0)]],
    texture2d<float>        nrm    [[texture(1)]],
    sampler                 samp  [[sampler(0)]],
    sampler                 samp1 [[sampler(1)]],
    constant NormalParams&  p     [[buffer(2)]]
) {
    float3 n = normalize(nrm.sample(samp1, input.uv).rgb * 2.0 - 1.0);

    float3 ldir = normalize(p.light_dir.xyz);
    float4 base = albedo.sample(samp, input.uv) * input.color;

    float diff = max(dot(n, ldir), 0.0) * p.light_dir.w;

    float3 h = normalize(ldir + float3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, h), 0.0), 32.0) * p.misc.y;

    float3 col = base.rgb * (p.misc.x + diff) + spec;
    return float4(col, base.a);
}
)msl";

static constexpr size_t normal_map_fragment_msl_size = sizeof(normal_map_fragment_msl);

// ─── Cartoon Fragment (toon bands + ink edges) ───────────────────────
// Engine port of lab_cartoon.
static constexpr char   cartoon_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct CartoonParams {
    float4 light_dir;  // xyz = direction TO light, w = intensity
    float4 misc;       // x = ambient, y = spec on/off, z = height_scale, w = spare
    float4 toon;       // x = bands (2..5), y = ink threshold, z = ink strength, w = spare
};

float cartoon_luma(float3 c) {
    return dot(c, float3(0.299, 0.587, 0.114));
}

fragment float4 cartoon_fragment_main(
    VSOutput                 input [[stage_in]],
    texture2d<float>         tex   [[texture(0)]],
    sampler                  samp  [[sampler(0)]],
    constant CartoonParams&  p     [[buffer(2)]]
) {
    float2 texel = 1.0 / float2(tex.get_width(), tex.get_height());

    float tl = cartoon_luma(tex.sample(samp, input.uv + texel * float2(-1, -1)).rgb);
    float t  = cartoon_luma(tex.sample(samp, input.uv + texel * float2( 0, -1)).rgb);
    float tr = cartoon_luma(tex.sample(samp, input.uv + texel * float2( 1, -1)).rgb);
    float l  = cartoon_luma(tex.sample(samp, input.uv + texel * float2(-1,  0)).rgb);
    float r  = cartoon_luma(tex.sample(samp, input.uv + texel * float2( 1,  0)).rgb);
    float bl = cartoon_luma(tex.sample(samp, input.uv + texel * float2(-1,  1)).rgb);
    float b  = cartoon_luma(tex.sample(samp, input.uv + texel * float2( 0,  1)).rgb);
    float br = cartoon_luma(tex.sample(samp, input.uv + texel * float2( 1,  1)).rgb);

    float dhdx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
    float dhdy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);
    float edge = length(float2(dhdx, dhdy));

    float3 n = normalize(float3(-dhdx * p.misc.z, -dhdy * p.misc.z, 1.0));
    float3 ldir = normalize(p.light_dir.xyz);
    float4 base = tex.sample(samp, input.uv) * input.color;

    float bands = max(p.toon.x, 2.0);
    float d = max(dot(n, ldir), 0.0) * p.light_dir.w;
    float shade = p.misc.x + (floor(d * bands) / bands) * (1.0 - p.misc.x);

    float3 h = normalize(ldir + float3(0.0, 0.0, 1.0));
    float spec = step(0.85, max(dot(n, h), 0.0)) * p.misc.y;

    float ink = (1.0 - smoothstep(p.toon.y, p.toon.y * 2.0, edge)) * p.toon.z;

    float3 col = base.rgb * shade + spec;
    col *= 1.0 - ink;
    return float4(col, base.a);
}
)msl";

static constexpr size_t cartoon_fragment_msl_size = sizeof(cartoon_fragment_msl);

// ─── Plastic Fragment (gloss + clearcoat + fresnel rim) ──────────────
// Engine port of lab_plastic. Second texture (normal map) binds at
// logical slot 2 — same Vulkan single-texture limitation as normal_map.
static constexpr char   plastic_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct PlasticParams {
    float4 light_dir;  // xyz = direction TO light, w = intensity
    float4 misc;       // x = ambient, y = spec strength, z = unused, w = spare
    float4 plastic;    // x = clearcoat, y = fresnel strength, z = wrap 0..1, w = shininess
};

fragment float4 plastic_fragment_main(
    VSOutput                  input [[stage_in]],
    texture2d<float>          albedo [[texture(0)]],
    texture2d<float>          nrm    [[texture(1)]],
    sampler                   samp  [[sampler(0)]],
    sampler                   samp1 [[sampler(1)]],
    constant PlasticParams&   p     [[buffer(2)]]
) {
    float3 n = normalize(nrm.sample(samp1, input.uv).rgb * 2.0 - 1.0);

    // Card/view space == tangent space for flat XY sprites (no TBN needed).
    float3 ldir = normalize(p.light_dir.xyz);
    float4 base = albedo.sample(samp, input.uv) * input.color;
    float3 v = float3(0.0, 0.0, 1.0); // viewer looks down +z

    float intensity = max(p.light_dir.w, 0.0);

    // Wrapped diffuse — plastic never goes fully black in shadow.
    float wrap = clamp(p.plastic.z, 0.0, 1.0);
    float d = clamp((dot(n, ldir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    d *= intensity;

    // Glossy highlight (broad) + clearcoat lobe (tight, top layer).
    float3 h = normalize(ldir + v);
    float ndh = max(dot(n, h), 0.0);
    float shine = max(p.plastic.w, 8.0);
    float gloss = pow(ndh, shine) * p.misc.y * intensity;
    float coat = pow(ndh, shine * 5.0) * p.plastic.x * intensity;

    // Stylized Fresnel rim — cool sheen at grazing angles.
    float fres = pow(1.0 - max(dot(n, v), 0.0), 5.0);
    float3 rim_color = float3(0.65, 0.8, 1.0);
    float3 rim = rim_color * fres * p.plastic.y * (0.3 + 0.7 * coat);

    float3 col = base.rgb * (p.misc.x + d) + (gloss + coat) + rim;
    return float4(col, base.a);
}
)msl";

static constexpr size_t plastic_fragment_msl_size = sizeof(plastic_fragment_msl);

// ─── Glow Pulse Fragment (expanding + fading ring) ───────────────────
// Engine port of lab_glow. Host draws a quad k× larger than the card so
// the ring has margin to travel in (see misc.z).
static constexpr char   glow_pulse_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct GlowParams {
    float4 timing; // semantics per effect (see below)
    float4 glow;   // rgba glow color
    float4 misc;   // x = card aspect (w/h), y = intensity, z = quad scale k, w = spare
};

float sd_round_box(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

fragment float4 glow_pulse_fragment_main(
    VSOutput              input [[stage_in]],
    texture2d<float>      albedo [[texture(0)]],
    sampler               samp  [[sampler(0)]],
    constant GlowParams&  p     [[buffer(2)]]
) {
    // timing: x = time (s), y = duration (s), z = max expand (uv), w = ring width
    float k = max(p.misc.z, 1.0);
    float2 card_uv = (input.uv - 0.5) * k + 0.5;
    float4 base = albedo.sample(samp, card_uv) * input.color;
    float2 inside2 = step(float2(0.0), card_uv) * step(card_uv, float2(1.0));
    base *= inside2.x * inside2.y;

    float aspect = max(p.misc.x, 1e-3);
    float2 pc = (card_uv - 0.5) * float2(aspect, 1.0);
    float2 half_size = float2(aspect * 0.5, 0.5) - 0.03;
    float dist = sd_round_box(pc, half_size, 0.03);

    float aa = max(fwidth(dist), 1e-3);
    float body = 1.0 - smoothstep(-aa, aa, dist);

    float dur = max(p.timing.y, 1e-3);
    float phase = fract(p.timing.x / dur);
    float center = phase * p.timing.z;
    float ring = 1.0 - smoothstep(0.0, max(p.timing.w, 1e-4), abs(dist - center));
    float outside = smoothstep(-aa, aa, dist); // free (outer) glow only
    float a = ring * outside * (1.0 - phase) * p.glow.a * p.misc.y;

    float3 col = base.rgb * body + p.glow.rgb * a;
    return float4(col, max(base.a * body, a));
}
)msl";

static constexpr size_t glow_pulse_fragment_msl_size = sizeof(glow_pulse_fragment_msl);

// ─── Gold Border Fragment (static band + breathe) ────────────────────
// Engine port of lab_gold. timing: x = time (s), y = pulse speed (Hz),
// z = band width (card-uv). Persistent base (0.35) + full pulse overlay.
static constexpr char   gold_border_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct GlowParams {
    float4 timing; // x = time (s), y = pulse speed (Hz), z = band width, w = spare
    float4 glow;   // rgba glow color
    float4 misc;   // x = card aspect (w/h), y = intensity, z = quad scale k, w = spare
};

float sd_round_box(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

fragment float4 gold_border_fragment_main(
    VSOutput              input [[stage_in]],
    texture2d<float>      albedo [[texture(0)]],
    sampler               samp  [[sampler(0)]],
    constant GlowParams&  p     [[buffer(2)]]
) {
    float k = max(p.misc.z, 1.0);
    float2 card_uv = (input.uv - 0.5) * k + 0.5;
    float4 base = albedo.sample(samp, card_uv) * input.color;
    float2 inside2 = step(float2(0.0), card_uv) * step(card_uv, float2(1.0));
    base *= inside2.x * inside2.y;

    float aspect = max(p.misc.x, 1e-3);
    float2 pc = (card_uv - 0.5) * float2(aspect, 1.0);
    float2 half_size = float2(aspect * 0.5, 0.5) - 0.03;
    float dist = sd_round_box(pc, half_size, 0.03);

    float aa = max(fwidth(dist), 1e-3);
    float body = 1.0 - smoothstep(-aa, aa, dist);

    float pulse = 0.5 + 0.5 * sin(p.timing.x * max(p.timing.y, 1e-3) * 6.2831853);
    pulse = pulse * pulse * (3.0 - 2.0 * pulse); // eased breathe
    float w = max(p.timing.z, 1e-4);
    float band = 1.0 - smoothstep(0.0, w, abs(dist));
    float outside = smoothstep(-aa, aa, dist); // outer glow only
    float base_level = 0.35;   // persistent border — always visible
    float pulse_level = pulse; // pulse overlay — fully fades in/out
    float a = band * outside * (base_level + pulse_level) * p.glow.a * p.misc.y;

    float3 col = base.rgb * body + p.glow.rgb * a;
    return float4(col, max(base.a * body, a));
}
)msl";

static constexpr size_t gold_border_fragment_msl_size = sizeof(gold_border_fragment_msl);

// ─── Gold Stay Fragment (persistent base + gentle pulse) ─────────────
// Engine port of lab_stay. timing: x = time (s), y = pulse speed (Hz),
// z = band width (card-uv). Base 0.70 + 0.30 pulse — never disappears.
static constexpr char   gold_stay_fragment_msl[]    = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct GlowParams {
    float4 timing; // x = time (s), y = pulse speed (Hz), z = band width, w = spare
    float4 glow;   // rgba glow color
    float4 misc;   // x = card aspect (w/h), y = intensity, z = quad scale k, w = spare
};

float sd_round_box(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

fragment float4 gold_stay_fragment_main(
    VSOutput              input [[stage_in]],
    texture2d<float>      albedo [[texture(0)]],
    sampler               samp  [[sampler(0)]],
    constant GlowParams&  p     [[buffer(2)]]
) {
    float k = max(p.misc.z, 1.0);
    float2 card_uv = (input.uv - 0.5) * k + 0.5;
    float4 base = albedo.sample(samp, card_uv) * input.color;
    float2 inside2 = step(float2(0.0), card_uv) * step(card_uv, float2(1.0));
    base *= inside2.x * inside2.y;

    float aspect = max(p.misc.x, 1e-3);
    float2 pc = (card_uv - 0.5) * float2(aspect, 1.0);
    float2 half_size = float2(aspect * 0.5, 0.5) - 0.03;
    float dist = sd_round_box(pc, half_size, 0.03);

    float aa = max(fwidth(dist), 1e-3);
    float body = 1.0 - smoothstep(-aa, aa, dist);

    float pulse = 0.5 + 0.5 * sin(p.timing.x * max(p.timing.y, 1e-3) * 6.2831853);
    pulse = pulse * pulse * (3.0 - 2.0 * pulse); // eased breathe
    float w = max(p.timing.z, 1e-4);
    float band = 1.0 - smoothstep(0.0, w, abs(dist));
    float outside = smoothstep(-aa, aa, dist); // outer glow only
    float base_level = 0.70; // stays on
    float pulse_amp = 0.30;  // gentle breathe on top
    float a = band * outside * (base_level + pulse_amp * pulse) * p.glow.a * p.misc.y;

    float3 col = base.rgb * body + p.glow.rgb * a;
    return float4(col, max(base.a * body, a));
}
)msl";

static constexpr size_t gold_stay_fragment_msl_size = sizeof(gold_stay_fragment_msl);

} // namespace shader

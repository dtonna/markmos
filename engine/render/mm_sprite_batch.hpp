// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../rhi/mm_rhi_concept.hpp"
#include "../core/mm_handle.hpp"
#include "../core/mm_arena.hpp"
#include "mm_sort_key.hpp"
#include "mm_sprite.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>

// Sprite Batch — SoA instanced 2D quad renderer
// Cache reason:
//   - Single vertex buffer update (memcpy of packed vertex data)
//   - Single instanced draw call per batch group
//   - SoA metadata for CPU culling before GPU submission
// Design:
//   - MAX_SPRITES = 16384 (matches 10k target with headroom)
//   - Sprites grouped by texture (atlas) and shader
//   - Vertex format: position(2×f16) + uv(2×f16) + color(4×u8) = 12 bytes/vert
//   - Auto-flush on texture change or batch full

static constexpr uint16_t MAX_SPRITES     = 16384;
static constexpr uint16_t VERTS_PER_QUAD  = 6;   // two triangles
static constexpr uint32_t MAX_VERTS       = static_cast<uint32_t>(MAX_SPRITES) * VERTS_PER_QUAD;

// Packed vertex — 12 bytes per vertex (tightly packed)
struct SpriteVertex {
    uint16_t x, y;     // f16 position (relative to camera)
    uint16_t u, v;     // f16 UV
    uint32_t color;    // RGBA8
};

static_assert(sizeof(SpriteVertex) == 12, "SpriteVertex must be 12 bytes");

// Per-sprite data for CPU culling
struct SpriteMetadata {
    float    world_x, world_y;     // world position
    float    scale_x, scale_y;     // scale
    float    rotation;             // radians
    uint32_t color;                // RGBA tint
    uint16_t atlas_x, atlas_y;     // atlas tile offset (if atlas mode)
    uint16_t atlas_w, atlas_h;     // tile size in atlas
    uint16_t tex_handle_id;        // texture ID (low bits)
    uint8_t  layer;                // render layer
    uint8_t  active;               // visible this frame
};

// SoA sprite batch — hot/cold split
// Hot: transform data for culling + batching
// Cold: metadata for render setup (atlas UV, texture)
struct SpriteBatch {
    // Hot metadata — read every frame for culling
    alignas(64) float  world_x     [MAX_SPRITES];
    alignas(64) float  world_y     [MAX_SPRITES];
    alignas(64) float  scale_x     [MAX_SPRITES];
    alignas(64) float  scale_y     [MAX_SPRITES];
    alignas(64) float  rotation    [MAX_SPRITES];
    alignas(64) uint8_t layer      [MAX_SPRITES];
    alignas(64) uint8_t active     [MAX_SPRITES];

    // Cold metadata — accessed during vertex generation
    uint32_t color      [MAX_SPRITES];
    uint16_t atlas_x    [MAX_SPRITES];
    uint16_t atlas_y    [MAX_SPRITES];
    uint16_t atlas_w    [MAX_SPRITES];
    uint16_t atlas_h    [MAX_SPRITES];
    uint16_t tex_id     [MAX_SPRITES];  // TextureHandle.id

    uint16_t count;

    // Atlas / tile dimensions
    uint16_t atlas_tex_w, atlas_tex_h;    // texture dimensions from add_frame
    uint16_t atlas_tile_w, atlas_tile_h;  // per-tile dimensions for add_tile

    // Texture registration for per-sprite texture support
    static constexpr uint16_t MAX_REGISTERED_TEX = 64;
    TextureHandle registered_tex[MAX_REGISTERED_TEX];
    uint16_t registered_tex_count;

    void init() noexcept { reset(); }

    void reset() noexcept {
        memset(active, 0, sizeof(active));
        count = 0;
        atlas_tex_w = 0; atlas_tex_h = 0;
        atlas_tile_w = 0; atlas_tile_h = 0;
        registered_tex_count = 0;
    }

    // Register a texture for per-sprite use. tex_id[i] in add()/add_frame()
    // will resolve to this handle during flush. Texture 0 = fallback.
    void register_texture(TextureHandle tex) noexcept {
        uint16_t id16 = static_cast<uint16_t>(tex.handle.id);
        for (uint16_t i = 0; i < registered_tex_count; ++i) {
            if ((registered_tex[i].handle.id & 0xFFFF) == id16) return;
        }
        if (registered_tex_count < MAX_REGISTERED_TEX)
            registered_tex[registered_tex_count++] = tex;
    }

    // Resolve a tex_id to TextureHandle. Returns invalid handle if not found.
    TextureHandle lookup_texture(uint16_t id) const noexcept {
        for (uint16_t i = 0; i < registered_tex_count; ++i) {
            if ((registered_tex[i].handle.id & 0xFFFF) == static_cast<uint32_t>(id))
                return registered_tex[i];
        }
        return TextureHandle{};
    }

    // Add a sprite — O(1), no alloc
    void add(float x, float y, float sx, float sy, float rot,
             uint32_t col, uint8_t layer_id, uint16_t tex = 0) noexcept {
        if (count >= MAX_SPRITES) return;
        uint16_t i = count++;
        world_x[i]  = x;
        world_y[i]  = y;
        scale_x[i]  = sx;
        scale_y[i]  = sy;
        rotation[i] = rot;
        color[i]    = col;
        layer[i]    = layer_id;
        active[i]   = 1;
        tex_id[i]   = tex;

        // Default atlas UV: full texture
        atlas_x[i] = 0; atlas_y[i] = 0;
        atlas_w[i] = 0; atlas_h[i] = 0;
    }

    // Add sprite from a SpriteFrame
    void add_frame(float x, float y, float sx, float sy, float rot,
                   uint32_t col, uint8_t layer_id,
                   const SpriteFrame& frame) noexcept {
        if (count >= MAX_SPRITES) return;
        uint16_t i = count++;
        world_x[i]  = x;
        world_y[i]  = y;
        scale_x[i]  = sx;
        scale_y[i]  = sy;
        rotation[i] = rot;
        color[i]    = col;
        layer[i]    = layer_id;
        active[i]   = 1;
        tex_id[i]   = static_cast<uint16_t>(frame.texture.handle.id);

        atlas_x[i] = frame.x;
        atlas_y[i] = frame.y;
        atlas_w[i] = frame.w;
        atlas_h[i] = frame.h;
        atlas_tex_w = frame.tex_w;
        atlas_tex_h = frame.tex_h;
    }

    // Add sprite with atlas tile
    void add_tile(float x, float y, float sx, float sy, float rot,
                  uint32_t col, uint8_t layer_id,
                  uint16_t tile_col, uint16_t tile_row) noexcept {
        if (count >= MAX_SPRITES) return;
        uint16_t i = count++;
        world_x[i]  = x;
        world_y[i]  = y;
        scale_x[i]  = sx;
        scale_y[i]  = sy;
        rotation[i] = rot;
        color[i]    = col;
        layer[i]    = layer_id;
        active[i]   = 1;
        tex_id[i]   = 0;   // atlas mode

        atlas_x[i] = static_cast<uint16_t>(tile_col * atlas_tile_w);
        atlas_y[i] = static_cast<uint16_t>(tile_row * atlas_tile_h);
        atlas_w[i] = atlas_tile_w;
        atlas_h[i] = atlas_tile_h;
    }

    // Generate vertices into output buffer — returns vertex count
    // Camera: view-projection transform
    uint32_t generate_vertices(SpriteVertex* out_verts, uint32_t max_verts,
                               float cam_x, float cam_y, float cam_scale) const noexcept {
        uint32_t vert_count = 0;

        for (uint16_t i = 0; i < count && vert_count + VERTS_PER_QUAD <= max_verts; ++i) {
            if (!active[i]) continue;

            float wx = world_x[i];
            float wy = world_y[i];
            float sx = scale_x[i] * cam_scale;
            float sy = scale_y[i] * cam_scale;
            float rot = rotation[i];
            float c = std::cos(rot);
            float s = std::sin(rot);

            // Half-size for quad corners
            float hx = sx * 0.5f;
            float hy = sy * 0.5f;

            // Screen position
            float sx_pos = wx - cam_x;
            float sy_pos = wy - cam_y;

            // UV coordinates (default: full texture)
            float u0 = 0.0f, v0 = 0.0f;
            float u1 = 1.0f, v1 = 1.0f;

            if (atlas_w[i] > 0 && atlas_h[i] > 0 && atlas_tex_w > 0 && atlas_tex_h > 0) {
                u0 = static_cast<float>(atlas_x[i]) / atlas_tex_w;
                v0 = static_cast<float>(atlas_y[i]) / atlas_tex_h;
                u1 = static_cast<float>(atlas_x[i] + atlas_w[i]) / atlas_tex_w;
                v1 = static_cast<float>(atlas_y[i] + atlas_h[i]) / atlas_tex_h;
            }

            uint32_t col = color[i];

            // Six vertices (two triangles) with rotation
            auto emit_vert = [&](float lx, float ly, float u, float v) {
                // Rotate
                float rx = lx * c - ly * s;
                float ry = lx * s + ly * c;
                SpriteVertex& vert = out_verts[vert_count++];
                vert.x = float_to_f16(sx_pos + rx);
                vert.y = float_to_f16(sy_pos + ry);
                vert.u = float_to_f16(u);
                vert.v = float_to_f16(v);
                vert.color = col;
            };

            // Two triangles: bl→br→tl and br→tr→tl
            emit_vert(-hx, -hy, u0, v0);
            emit_vert( hx, -hy, u1, v0);
            emit_vert(-hx,  hy, u0, v1);
            emit_vert( hx, -hy, u1, v0);
            emit_vert( hx,  hy, u1, v1);
            emit_vert(-hx,  hy, u0, v1);
        }

        return vert_count;
    }

    // Flush all active sprites to GPU in a single draw call
    // @param vb          GPU vertex buffer (cpu_visible, sized for MAX_VERTS * sizeof(SpriteVertex))
    // @param ub          Uniform buffer for CameraUBO (float4x4 view_proj, cpu_visible) — user updates before call
    // @param arena       Frame arena for temporary vertex data
    // @param cam_x/y/scl Camera world offset & zoom (applied by generate_vertices)
    // Note: CameraUBO (buffer 1 in vertex shader) must be populated before calling flush().
    template<RHI_Backend B>
    void flush(B& backend, BufferHandle vb, BufferHandle ub, PipelineHandle pipeline,
               TextureHandle texture, SamplerHandle sampler, FrameArena& arena,
               float cam_x = 0.0f, float cam_y = 0.0f, float cam_scale = 1.0f) noexcept {
        uint32_t max = static_cast<uint32_t>(count) * VERTS_PER_QUAD;
        if (max == 0) return;

        auto* verts = arena.alloc_array<SpriteVertex>(max);
        if (!verts) return;

        uint32_t vert_count = generate_vertices(verts, max, cam_x, cam_y, cam_scale);
        if (vert_count == 0) return;

        backend.update_buffer(vb, verts, 0, vert_count * sizeof(SpriteVertex));
        backend.bind_pipeline(pipeline);
        BufferHandle bufs[2] = {vb, ub};
        backend.bind_vertex_buffers(bufs, 2, nullptr, nullptr);
        backend.bind_fragment_texture(texture, 0);
        backend.bind_fragment_sampler(sampler, 0);
        backend.draw(vert_count, 1, 0, 0);
    }

    // Cull sprites outside view (optional, call before generate)
    // Returns visible count
    uint32_t cull(float view_left, float view_top, float view_right, float view_bottom) noexcept {
        uint32_t visible = 0;
        for (uint16_t i = 0; i < count; ++i) {
            if (!active[i]) continue;
            float wx = world_x[i];
            float wy = world_y[i];
            float hs = fmaxf(scale_x[i], scale_y[i]) * 0.5f;
            if (wx + hs < view_left || wx - hs > view_right ||
                wy + hs < view_top || wy - hs > view_bottom) {
                active[i] = 0;  // culled
            } else {
                ++visible;
            }
        }
        return visible;
    }

public:
    // Float to half-float (f16) conversion
    static uint16_t float_to_f16(float f) noexcept {
        // IEEE 754 f32 → f16
        uint32_t u;
        memcpy(&u, &f, sizeof(u));
        uint16_t sign = (u >> 16) & 0x8000;
        int32_t exp = static_cast<int32_t>((u >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = u & 0x7FFFFF;

        if (exp <= 0) {
            // Subnormal or zero
            if (exp < -10) return sign;
            mant = (mant | 0x800000) >> (1 - exp);
            return static_cast<uint16_t>(sign | (mant >> 13));
        }
        if (exp >= 31) {
            // Infinity or NaN
            return static_cast<uint16_t>(sign | 0x7C00 | (mant ? 0x200 : 0));
        }

        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
    }
};

static_assert(sizeof(SpriteBatch) <= 640 * 1024, "SpriteBatch < 640KB");

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_arena.hpp"
#include "core/mm_types.h"
#include "../core/mm_handle.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include "mm_sort_key.hpp"
#include "mm_sprite.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

static constexpr u16 MAX_SPRITES    = 16384;
static constexpr u16 VERTS_PER_QUAD = 6; // two triangles
static constexpr u32 MAX_VERTS      = static_cast<u32>(MAX_SPRITES) * VERTS_PER_QUAD;

// Packed vertex — 24 bytes per vertex
struct SpriteVertex {
    u16 x, y;    // f16 position (relative to camera)
    u16 u, v;    // f16 UV
    u32 color;   // RGBA8 fill color
    u16 local_x; // f16 local coord (-0.5..+0.5) for SDF rounded rect
    u16 local_y;
    u16 radius;       // f16 corner radius (normalized 0..0.5)
    u16 border_w;     // f16 border width (normalized 0..0.5, 0 = no border)
    u32 border_color; // RGBA8 border color (ignored when border_w = 0)
};

static_assert(sizeof(SpriteVertex) == 24, "SpriteVertex must be 24 bytes");

// SoA sprite batch — hot/cold split
// Hot: transform data for culling + batching
// Cold: metadata for render setup (atlas UV, texture)
struct SpriteBatch {
    // Hot metadata — read every frame for culling
    alignas(64) f32 world_x[MAX_SPRITES];
    alignas(64) f32 world_y[MAX_SPRITES];
    alignas(64) f32 scale_x[MAX_SPRITES];
    alignas(64) f32 scale_y[MAX_SPRITES];
    alignas(64) f32 rotation[MAX_SPRITES];
    alignas(64) u8 layer[MAX_SPRITES];
    alignas(64) u8 active[MAX_SPRITES];

    // Cold metadata — accessed during vertex generation
    u32                  color[MAX_SPRITES];
    u16                  atlas_x[MAX_SPRITES];
    u16                  atlas_y[MAX_SPRITES];
    u16                  atlas_w[MAX_SPRITES];
    u16                  atlas_h[MAX_SPRITES];
    u16                  tex_id[MAX_SPRITES]; // TextureHandle.id

    f32                     corner_r[MAX_SPRITES];     // corner radius (normalized 0..0.5)
    f32                     border_w[MAX_SPRITES];     // border width (normalized, 0 = no border)
    u32                  border_color[MAX_SPRITES]; // border RGBA8

    u16                  count;

    // Overflows per batch: how many add()/add_frame() calls were silently
    // dropped because the batch was full. Without this the overflow read as a
    // missing card or a missing label with no counter - text has
    // text_dropped_calls, commands have TRACK_POOL_OVERFLOW(), this is the
    // sprite-side one. Cleared by reset().
    u32                  dropped = 0;
    // tex_id registrations past the 64-slot cap were dropped (same silence class as `dropped`).
    u32                  dropped_tex_ids = 0;

    // Atlas / tile dimensions
    u16                  atlas_tex_w, atlas_tex_h;   // texture dimensions from add_frame
    u16                  atlas_tile_w, atlas_tile_h; // per-tile dimensions for add_tile

    // Texture registration for per-sprite texture support
    static constexpr u16 MAX_REGISTERED_TEX = 64;
    TextureHandle             registered_tex[MAX_REGISTERED_TEX];
    u16                  registered_tex_count;

    // Device-pixel snap (polish): layers in snap_layer_mask render with
    // their screen position rounded to 1/snap_scale points (snap_scale =
    // device px per point). Static layers (board cards, slots) go crisp;
    // animated layers (drag, tweens, particles) stay fractional. Mask 0 =
    // off. Persistent config: reset() does NOT clear it.
    u8                   snap_layer_mask = 0;
    f32                     snap_scale      = 1.0f;

    void                      init() noexcept { reset(); }

    void                      reset() noexcept {
        memset(active, 0, sizeof(active));
        count                = 0;
        dropped              = 0;
        dropped_tex_ids      = 0;
        atlas_tex_w          = 0;
        atlas_tex_h          = 0;
        atlas_tile_w         = 0;
        atlas_tile_h         = 0;
        registered_tex_count = 0;
        corner_r[0]          = 0.0f; // sentinel — actual per-sprite values set by add calls
    }

    // Register a texture for per-sprite use. tex_id[i] in add()/add_frame()
    // will resolve to this handle during flush. Texture 0 = fallback.
    void register_texture(TextureHandle tex) noexcept {
        u16 id16 = static_cast<u16>(tex.handle.id);
        for (u16 i = 0; i < registered_tex_count; ++i) {
            if ((registered_tex[i].handle.id & 0xFFFF) == id16) {
                return;
            }
        }
        if (registered_tex_count < MAX_REGISTERED_TEX) {
            registered_tex[registered_tex_count++] = tex;
        } else {
            // Silent before: a tex_id that never resolves falls back to the plain
            // white tile (flush_sprites_impl), which reads as "missing card art".
            ++dropped_tex_ids;
        }
    }

    // Resolve a tex_id to TextureHandle. Returns invalid handle if not found.
    TextureHandle lookup_texture(u16 id) const noexcept {
        for (u16 i = 0; i < registered_tex_count; ++i) {
            if ((registered_tex[i].handle.id & 0xFFFF) == static_cast<u32>(id)) {
                return registered_tex[i];
            }
        }
        return TextureHandle{};
    }

    // Add a sprite — O(1), no alloc
    void add(f32 x, f32 y, f32 sx, f32 sy, f32 rot, u32 col, u8 layer_id, u16 tex = 0, f32 radius = 0.0f, f32 bw = 0.0f,
             u32 bcol = 0) noexcept {
        if (count >= MAX_SPRITES) {
            ++dropped;
            return;
        }
        u16 i      = count++;
        world_x[i]      = x;
        world_y[i]      = y;
        scale_x[i]      = sx;
        scale_y[i]      = sy;
        rotation[i]     = rot;
        color[i]        = col;
        layer[i]        = layer_id;
        active[i]       = 1;
        tex_id[i]       = tex;
        corner_r[i]     = radius;
        border_w[i]     = bw;
        border_color[i] = bcol;

        atlas_x[i]      = 0;
        atlas_y[i]      = 0;
        atlas_w[i]      = 0;
        atlas_h[i]      = 0;
    }

    // Add sprite from a SpriteFrame
    void add_frame(f32 x, f32 y, f32 sx, f32 sy, f32 rot, u32 col, u8 layer_id, const SpriteFrame &frame) noexcept {
        if (count >= MAX_SPRITES) {
            ++dropped;
            return;
        }
        u16 i      = count++;
        world_x[i]      = x;
        world_y[i]      = y;
        scale_x[i]      = sx;
        scale_y[i]      = sy;
        rotation[i]     = rot;
        color[i]        = col;
        layer[i]        = layer_id;
        active[i]       = 1;
        tex_id[i]       = static_cast<u16>(frame.texture.handle.id);
        corner_r[i]     = 0.0f;
        border_w[i]     = 0.0f;
        border_color[i] = 0;

        atlas_x[i]      = frame.x;
        atlas_y[i]      = frame.y;
        atlas_w[i]      = frame.w;
        atlas_h[i]      = frame.h;
        atlas_tex_w     = frame.tex_w;
        atlas_tex_h     = frame.tex_h;
    }

    // Add sprite from a SpriteFrame with corner radius
    void add_frame(f32 x, f32 y, f32 sx, f32 sy, f32 rot, u32 col, u8 layer_id, const SpriteFrame &frame, f32 radius) noexcept {
        if (count >= MAX_SPRITES) {
            ++dropped;
            return;
        }
        u16 i      = count++;
        world_x[i]      = x;
        world_y[i]      = y;
        scale_x[i]      = sx;
        scale_y[i]      = sy;
        rotation[i]     = rot;
        color[i]        = col;
        layer[i]        = layer_id;
        active[i]       = 1;
        tex_id[i]       = static_cast<u16>(frame.texture.handle.id);
        corner_r[i]     = radius;
        border_w[i]     = 0.0f;
        border_color[i] = 0;

        atlas_x[i]      = frame.x;
        atlas_y[i]      = frame.y;
        atlas_w[i]      = frame.w;
        atlas_h[i]      = frame.h;
        atlas_tex_w     = frame.tex_w;
        atlas_tex_h     = frame.tex_h;
    }

    // Add sprite from a SpriteFrame with corner radius, border width, border color
    void add_frame(f32 x, f32 y, f32 sx, f32 sy, f32 rot, u32 col, u8 layer_id, const SpriteFrame &frame, f32 radius, f32 bw,
                   u32 bc) noexcept {
        if (count >= MAX_SPRITES) {
            ++dropped;
            return;
        }
        u16 i      = count++;
        world_x[i]      = x;
        world_y[i]      = y;
        scale_x[i]      = sx;
        scale_y[i]      = sy;
        rotation[i]     = rot;
        color[i]        = col;
        layer[i]        = layer_id;
        active[i]       = 1;
        tex_id[i]       = static_cast<u16>(frame.texture.handle.id);
        corner_r[i]     = radius;
        border_w[i]     = bw;
        border_color[i] = bc;

        atlas_x[i]      = frame.x;
        atlas_y[i]      = frame.y;
        atlas_w[i]      = frame.w;
        atlas_h[i]      = frame.h;
        atlas_tex_w     = frame.tex_w;
        atlas_tex_h     = frame.tex_h;
    }

    // Add sprite with atlas tile
    void add_tile(f32 x, f32 y, f32 sx, f32 sy, f32 rot, u32 col, u8 layer_id, u16 tile_col, u16 tile_row) noexcept {
        if (count >= MAX_SPRITES) {
            ++dropped;
            return;
        }
        u16 i      = count++;
        world_x[i]      = x;
        world_y[i]      = y;
        scale_x[i]      = sx;
        scale_y[i]      = sy;
        rotation[i]     = rot;
        color[i]        = col;
        layer[i]        = layer_id;
        active[i]       = 1;
        tex_id[i]       = 0; // atlas mode
        corner_r[i]     = 0.0f;
        border_w[i]     = 0.0f;
        border_color[i] = 0;

        atlas_x[i]      = static_cast<u16>(tile_col * atlas_tile_w);
        atlas_y[i]      = static_cast<u16>(tile_row * atlas_tile_h);
        atlas_w[i]      = atlas_tile_w;
        atlas_h[i]      = atlas_tile_h;
    }

    // Generate vertices into output buffer — returns vertex count
    // Camera: view-projection transform
    u32 generate_vertices(SpriteVertex *out_verts, u32 max_verts, f32 cam_x, f32 cam_y, f32 cam_scale) const noexcept {
        u32 vert_count = 0;

        for (u16 i = 0; i < count && vert_count + VERTS_PER_QUAD <= max_verts; ++i) {
            if (!active[i]) {
                continue;
            }

            f32 wx     = world_x[i];
            f32 wy     = world_y[i];
            f32 sx     = scale_x[i] * cam_scale;
            f32 sy     = scale_y[i] * cam_scale;
            f32 rot    = rotation[i];
            f32 c      = __builtin_cosf(rot);
            f32 s      = __builtin_sinf(rot);

            // Half-size for quad corners
            f32 hx     = sx * 0.5f;
            f32 hy     = sy * 0.5f;

            // Screen position (device-pixel snap happens per corner
            // below — snapping the center is not enough when sizes are
            // fractional, the edges would still straddle texels)
            f32 sx_pos = wx - cam_x;
            f32 sy_pos = wy - cam_y;
            bool  do_snap = ((snap_layer_mask & (1u << layer[i])) != 0) && (snap_scale > 0.0f);

            // UV coordinates (default: full texture)
            f32 u0 = 0.0f, v0 = 0.0f;
            f32 u1 = 1.0f, v1 = 1.0f;

            if (atlas_w[i] > 0 && atlas_h[i] > 0 && atlas_tex_w > 0 && atlas_tex_h > 0) {
                u0 = static_cast<f32>(atlas_x[i]) / atlas_tex_w;
                v0 = static_cast<f32>(atlas_y[i]) / atlas_tex_h;
                u1 = static_cast<f32>(atlas_x[i] + atlas_w[i]) / atlas_tex_w;
                v1 = static_cast<f32>(atlas_y[i] + atlas_h[i]) / atlas_tex_h;
            }

            u32 col       = color[i];

            f32    cr        = corner_r[i];
            f32    bw        = border_w[i];
            u32 bcol      = border_color[i];
            f32    l_scale   = (sx > 0.0f) ? 1.0f / sx : 0.0f;
            f32    l_scale_y = (sy > 0.0f) ? 1.0f / sy : 0.0f;

            // Six vertices (two triangles) with rotation + local coords
            auto     emit_vert = [&](f32 lx, f32 ly, f32 u, f32 v) {
                // Rotate
                f32         rx = lx * c - ly * s;
                f32         ry = lx * s + ly * c;
                f32         px = sx_pos + rx;
                f32         py = sy_pos + ry;
                if (do_snap) {
                    px = __builtin_roundf(px * snap_scale) / snap_scale;
                    py = __builtin_roundf(py * snap_scale) / snap_scale;
                }
                SpriteVertex &vert = out_verts[vert_count++];
                vert.x             = float_to_f16(px);
                vert.y             = float_to_f16(py);
                vert.u             = float_to_f16(u);
                vert.v             = float_to_f16(v);
                vert.color         = col;
                vert.local_x       = float_to_f16(lx * l_scale);
                vert.local_y       = float_to_f16(ly * l_scale_y);
                vert.radius        = float_to_f16(cr);
                vert.border_w      = float_to_f16(bw);
                vert.border_color  = bcol;
            };

            // Two triangles: bl→br→tl and br→tr→tl
            emit_vert(-hx, -hy, u0, v0);
            emit_vert(hx, -hy, u1, v0);
            emit_vert(-hx, hy, u0, v1);
            emit_vert(hx, -hy, u1, v0);
            emit_vert(hx, hy, u1, v1);
            emit_vert(-hx, hy, u0, v1);
        }

        return vert_count;
    }

    // Flush all active sprites to GPU in a single draw call
    // @param vb          GPU vertex buffer (cpu_visible, sized for MAX_VERTS * sizeof(SpriteVertex))
    // @param ub          Uniform buffer for CameraUBO (float4x4 view_proj, cpu_visible) — user updates before call
    // @param arena       Frame arena for temporary vertex data
    // @param cam_x/y/scl Camera world offset & zoom (applied by generate_vertices)
    // Note: CameraUBO (buffer 1 in vertex shader) must be populated before calling flush().
    template <RHI_Backend B>
    void flush(B &backend, BufferHandle vb, BufferHandle ub, PipelineHandle pipeline, TextureHandle texture, SamplerHandle sampler, FrameArena &arena,
               f32 cam_x = 0.0f, f32 cam_y = 0.0f, f32 cam_scale = 1.0f) noexcept {
        u32 max = static_cast<u32>(count) * VERTS_PER_QUAD;
        if (max == 0) {
            return;
        }

        auto *verts = arena.alloc_array<SpriteVertex>(max);
        if (!verts) {
            return;
        }

        u32 vert_count = generate_vertices(verts, max, cam_x, cam_y, cam_scale);
        if (vert_count == 0) {
            return;
        }

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
    u32 cull(f32 view_left, f32 view_top, f32 view_right, f32 view_bottom) noexcept {
        u32 visible = 0;
        for (u16 i = 0; i < count; ++i) {
            if (!active[i]) {
                continue;
            }
            f32 wx = world_x[i];
            f32 wy = world_y[i];
            f32 hs = fmaxf(scale_x[i], scale_y[i]) * 0.5f;
            if (wx + hs < view_left || wx - hs > view_right || wy + hs < view_top || wy - hs > view_bottom) {
                active[i] = 0; // culled
            } else {
                ++visible;
            }
        }
        return visible;
    }

  public:
    // Float to half-f32 (f16) conversion
    static u16 float_to_f16(f32 f) noexcept {
        // IEEE 754 f32 → f16
        u32 u;
        memcpy(&u, &f, sizeof(u));
        u16 sign = (u >> 16) & 0x8000;
        i32  exp  = static_cast<i32>((u >> 23) & 0xFF) - 127 + 15;
        u32 mant = u & 0x7FFFFF;

        if (exp <= 0) {
            // Subnormal or zero
            if (exp < -10) {
                return sign;
            }
            mant = (mant | 0x800000) >> (1 - exp);
            return static_cast<u16>(sign | (mant >> 13));
        }
        if (exp >= 31) {
            // Infinity or NaN
            return static_cast<u16>(sign | 0x7C00 | (mant ? 0x200 : 0));
        }

        return static_cast<u16>(sign | (static_cast<u32>(exp) << 10) | (mant >> 13));
    }
};

static_assert(sizeof(SpriteBatch) <= 896 * 1024, "SpriteBatch < 896KB");

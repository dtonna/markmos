// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_arena.hpp"
#include "../core/mm_expected.hpp"
#include "../core/mm_tracy.hpp"
#include "../math/mm_mat4.h"
#include "../rhi/mm_rhi_concept.hpp"
#include "mm_font_atlas.hpp"
#include "mm_font_data.hpp"
#include "mm_material.hpp"
#include "mm_render_graph.hpp"
#include "mm_shader_registry.hpp"
#include "mm_sprite_batch.hpp"
#include "mm_text_renderer.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mach/machine.h>

// Renderer — non-template class per spec v4.6
// @cache_reason Single cache line for hot frame state, SoA sprite data
// @zero_virtual No template parameter on struct — conditional alias resolves at TU level
// @fallback ActiveBackend typedef selects MetalBackend or VulkanBackend at compile time

#if defined(USE_METAL_BACKEND)
#    include "../rhi/mm_metal_backend.hpp"
using ActiveBackend = MetalBackend;
#elif defined(USE_VULKAN_BACKEND)
#    include "../rhi/mm_vulkan_backend.hpp"
using ActiveBackend = VulkanBackend;
#else
#    error "No backend selected — define USE_METAL_BACKEND or USE_VULKAN_BACKEND"
#endif

static_assert(RHI_Backend<ActiveBackend>, "ActiveBackend must satisfy RHI_Backend concept");

struct Renderer {
    ActiveBackend backend;

    DoubleArena   frame_arena;
    RenderGraph   graph;

    alignas(64) float view_proj[16];

    PipelineHandle            sprite_pipeline{};
    PipelineHandle            sprite_additive_pipeline{};
    PipelineHandle            sprite_multiply_pipeline{};
    PipelineHandle            sprite_opaque_pipeline{};
    PipelineHandle            sdf_pipeline{};
    PipelineHandle            particle_pipeline{};

    Material                  materials_[static_cast<uint8_t>(MaterialType::COUNT)];

    BufferHandle              camera_ubo{};
    BufferHandle              sprite_vb{};
    BufferHandle              sprite_ib{};
    uint32_t                  ubo_alignment{};

    TextureHandle             white_tex{};
    SamplerHandle             default_sampler{};

    TextureHandle             font_tex{};
    SamplerHandle             font_sampler{};
    BufferHandle              text_vb{};
    BufferHandle              text_ib{};
    BitmapFont                default_font{};

    static constexpr uint32_t MAX_SPRITE_VERTS      = 16384 * 4;
    static constexpr uint32_t MAX_SPRITE_IDX        = 16384 * 6;
    static constexpr uint16_t TEXT_MAX_GLYPH        = 1024;
    static constexpr uint32_t MAX_TEXT_VERTS        = 4096;
    static constexpr uint32_t MAX_TEXT_INDICES      = 6144;

    static constexpr size_t   COMMAND_STORAGE_BYTES = sizeof(Command) * MAX_COMMANDS;
    static_assert((COMMAND_STORAGE_BYTES % 64) == 0, "Command storage must be a multiple of cache-line alignment");

    uint32_t width, height;
    float    inv_width, inv_height;
    float    content_scale       = 1.0f;

    uint32_t text_vertex_count   = 0;
    uint32_t text_index_count    = 0;
    uint32_t sprite_vertex_count = 0;
    uint32_t sprite_index_count  = 0;

    // Command   *command_storage_0 = nullptr;
    // Command   *command_storage_1 = nullptr;
    alignas(64) Command command_storage_0[MAX_COMMANDS];
    alignas(64) Command command_storage_1[MAX_COMMANDS];

    FrameArena temp_arena;
    alignas(64) uint8_t temp_storage[1024 * 1024];

    Renderer() noexcept                                                        = default;
    ~Renderer() noexcept                                                       = default;
    Renderer(const Renderer &)                                                 = delete;
    Renderer &operator=(const Renderer &)                                      = delete;
    Renderer(Renderer &&)                                                      = delete;
    Renderer                                           &operator=(Renderer &&) = delete;

    template <typename Handle, typename Fn> static void destroy_safe(Handle &h, Fn &&fn) noexcept {
        if (h) {
            fn(h);
            h = {};
        }
    }

    void destroy_resources() noexcept {
        destroy_safe(sprite_pipeline, [&](auto &h) { backend.destroy_pipeline(h); });
        destroy_safe(sprite_additive_pipeline, [&](auto &h) { backend.destroy_pipeline(h); });
        destroy_safe(sprite_multiply_pipeline, [&](auto &h) { backend.destroy_pipeline(h); });
        destroy_safe(sprite_opaque_pipeline, [&](auto &h) { backend.destroy_pipeline(h); });
        destroy_safe(sdf_pipeline, [&](auto &h) { backend.destroy_pipeline(h); });
        destroy_safe(particle_pipeline, [&](auto &h) { backend.destroy_pipeline(h); });
        destroy_safe(camera_ubo, [&](auto &h) { backend.destroy_buffer(h); });
        destroy_safe(sprite_vb, [&](auto &h) { backend.destroy_buffer(h); });
        destroy_safe(sprite_ib, [&](auto &h) { backend.destroy_buffer(h); });
        destroy_safe(font_tex, [&](auto &h) { backend.destroy_texture(h); });
        destroy_safe(font_sampler, [&](auto &h) { backend.destroy_sampler(h); });
        destroy_safe(text_vb, [&](auto &h) { backend.destroy_buffer(h); });
        destroy_safe(text_ib, [&](auto &h) { backend.destroy_buffer(h); });
        destroy_safe(white_tex, [&](auto &h) { backend.destroy_texture(h); });
        destroy_safe(default_sampler, [&](auto &h) { backend.destroy_sampler(h); });
    }

    Expected<void, RHIError> init(void *window_handle, uint32_t screen_w, uint32_t screen_h) noexcept {
        (void)window_handle;
        width      = screen_w;
        height     = screen_h;
        inv_width  = screen_w ? 1.0f / static_cast<float>(screen_w) : 1.0f;
        inv_height = screen_h ? 1.0f / static_cast<float>(screen_h) : 1.0f;

        frame_arena.init(command_storage_0, COMMAND_STORAGE_BYTES, command_storage_1, COMMAND_STORAGE_BYTES);
        temp_arena.init(temp_storage, sizeof(temp_storage));

        BufferDesc ubo_desc{};
        ubo_desc.type        = BufferType::Uniform;
        ubo_desc.size        = sizeof(float) * 16;
        ubo_desc.cpu_visible = true;
        auto ubo             = backend.create_buffer(ubo_desc);
        if (!ubo) {
            return make_unexpected(ubo.error());
        }
        camera_ubo    = *ubo;
        ubo_alignment = 256;

        BufferDesc vb_desc{};
        vb_desc.type        = BufferType::Vertex;
        vb_desc.size        = MAX_SPRITE_VERTS * sizeof(SpriteVertex);
        vb_desc.stride      = sizeof(SpriteVertex);
        vb_desc.cpu_visible = true;
        auto vb             = backend.create_buffer(vb_desc);
        if (!vb) {
            return make_unexpected(vb.error());
        }
        sprite_vb = *vb;

        BufferDesc ib_desc{};
        ib_desc.type        = BufferType::Index;
        ib_desc.size        = MAX_SPRITE_IDX * sizeof(uint16_t);
        ib_desc.stride      = sizeof(uint16_t);
        ib_desc.cpu_visible = true;
        auto ib             = backend.create_buffer(ib_desc);
        if (!ib) {
            return make_unexpected(ib.error());
        }
        sprite_ib = *ib;

        // Default 1x1 white texture for sprite rendering
        TextureDesc white_tex_desc{};
        white_tex_desc.type       = TextureType::Tex2D;
        white_tex_desc.format     = PixelFormat::R8G8B8A8_UNORM;
        white_tex_desc.width      = 1;
        white_tex_desc.height     = 1;
        white_tex_desc.mip_levels = 1;
        auto wt                   = backend.create_texture(white_tex_desc);
        if (!wt) {
            return make_unexpected(wt.error());
        }
        white_tex            = *wt;
        uint32_t white_pixel = 0xFFFFFFFF;
        backend.update_texture(white_tex, &white_pixel, 0, 0, 1, 1, 0, 0);

        SamplerDesc samp_desc{};
        samp_desc.min_filter     = SamplerFilter::Linear;
        samp_desc.mag_filter     = SamplerFilter::Linear;
        samp_desc.address_u      = SamplerAddress::ClampToEdge;
        samp_desc.address_v      = SamplerAddress::ClampToEdge;
        samp_desc.max_anisotropy = 1.0f;
        auto samp                = backend.create_sampler(samp_desc);
        if (!samp) {
            return make_unexpected(samp.error());
        }
        default_sampler = *samp;

        // Font atlas init — bake Latin + Thai into one R8 texture
        FontAtlas fa;
        fa.bake(g_karla_ttf, static_cast<int>(g_karla_ttf_size), 48.0f);
        // fa.bake() clears pixels, bakes ASCII range 32..127, and stores next-y
        // We derive next_y from the last ASCII char's pixel bottom
        int ascii_next_y = 0;
        for (int i = 0; i < FONT_NUM_CHARS; ++i) {
            float y1      = fa.baked_chars[i * 4 + 3] + 1.0f;
            int   row_end = static_cast<int>(y1);
            if (row_end > ascii_next_y) {
                ascii_next_y = row_end;
            }
        }
        stbtt_bakedchar th_cd[MAX_GLYPHS_TH];
        int             th_start = 0x0E01;
        int             th_end   = 0x0E5B;
        if (fa.bake_range(g_sarabun_ttf, 48.0f, 0x0E01, MAX_GLYPHS_TH, ascii_next_y, th_cd) < 0) {
            memset(th_cd, 0, sizeof(th_cd));
            // Fallback: try to bake each character individually
            for (int cp = th_start; cp <= th_end; ++cp) {
                fa.bake_range(g_sarabun_ttf, 48.0f, cp, 1, ascii_next_y, &th_cd[cp - th_start]);
            }
        }

        TextureDesc font_tex_desc{};
        font_tex_desc.type       = TextureType::Tex2D;
        font_tex_desc.format     = PixelFormat::R8_UNORM;
        font_tex_desc.width      = FONT_ATLAS_W;
        font_tex_desc.height     = FONT_ATLAS_H;
        font_tex_desc.mip_levels = 1;
        auto ft                  = backend.create_texture(font_tex_desc);
        if (!ft) {
            return make_unexpected(ft.error());
        }
        font_tex = *ft;
        backend.update_texture(font_tex, fa.pixels, 0, 0, FONT_ATLAS_W, FONT_ATLAS_H, 0, 0);

        SamplerDesc font_samp_desc{};
        font_samp_desc.min_filter     = SamplerFilter::Linear;
        font_samp_desc.mag_filter     = SamplerFilter::Linear;
        font_samp_desc.address_u      = SamplerAddress::ClampToEdge;
        font_samp_desc.address_v      = SamplerAddress::ClampToEdge;
        font_samp_desc.max_anisotropy = 1.0f;
        auto fs                       = backend.create_sampler(font_samp_desc);
        if (!fs) {
            return make_unexpected(fs.error());
        }
        font_sampler = *fs;

        default_font.init(font_tex, 48);
        default_font.atlas_w = FONT_ATLAS_W;
        default_font.atlas_h = FONT_ATLAS_H;
        for (int i = 0; i < FONT_NUM_CHARS; ++i) {
            auto  c = static_cast<uint8_t>(FONT_FIRST_CHAR + i);
            auto &g = default_font.glyphs[c];
            float u0, v0, u1, v1, gw, gh;
            float bx = 0, by = 0;
            fa.get_quad(c, bx, by, u0, v0, u1, v1, gw, gh);
            g.u         = static_cast<uint16_t>(u0 * FONT_ATLAS_W);
            g.v         = static_cast<uint16_t>(v0 * FONT_ATLAS_H);
            g.w         = static_cast<uint16_t>(gw);
            g.h         = static_cast<uint16_t>(gh);
            g.bearing_x = static_cast<int8_t>(bx);
            g.bearing_y = static_cast<int8_t>(by);
            g.advance   = static_cast<uint8_t>(fa.xadvance[i]);
        }
        for (int i = 0; i < MAX_GLYPHS_TH; ++i) {
            auto &g     = default_font.glyphs_th[i];
            auto &bc    = th_cd[i];
            g.u         = static_cast<uint16_t>(bc.x0);
            g.v         = static_cast<uint16_t>(bc.y0);
            g.w         = static_cast<uint16_t>(bc.x1 - bc.x0);
            g.h         = static_cast<uint16_t>(bc.y1 - bc.y0);
            g.bearing_x = static_cast<int8_t>(bc.xoff);
            g.bearing_y = static_cast<int8_t>(bc.yoff);
            g.advance   = static_cast<uint8_t>(bc.xadvance);
        }

        BufferDesc text_vb_desc{};
        text_vb_desc.type        = BufferType::Vertex;
        text_vb_desc.size        = TEXT_MAX_GLYPH * 4 * sizeof(SpriteVertex);
        text_vb_desc.stride      = sizeof(SpriteVertex);
        text_vb_desc.cpu_visible = true;
        auto tvb                 = backend.create_buffer(text_vb_desc);
        if (!tvb) {
            return make_unexpected(tvb.error());
        }
        text_vb = *tvb;

        BufferDesc text_ib_desc{};
        text_ib_desc.type        = BufferType::Index;
        text_ib_desc.size        = TEXT_MAX_GLYPH * 6 * sizeof(uint16_t);
        text_ib_desc.stride      = sizeof(uint16_t);
        text_ib_desc.cpu_visible = true;
        auto tib                 = backend.create_buffer(text_ib_desc);
        if (!tib) {
            return make_unexpected(tib.error());
        }
        text_ib = *tib;

        ortho(0.0f, static_cast<float>(screen_w), static_cast<float>(screen_h), 0.0f, -1.0f, 1.0f);

        auto sp = create_default_pipelines();
        if (!sp) {
            {
                char buf[256];
                int  n = snprintf(buf, sizeof(buf), "create_default_pipelines FAILED: err=%d\n", (int)sp.error());
                write(2, buf, (size_t)n);
            }
            return make_unexpected(sp.error());
        }

        {
            char buf[256];
            int  n = snprintf(buf, sizeof(buf), "create_default_pipelines OK: sprite=%u sdf=%u\n", sprite_pipeline.handle.id, sdf_pipeline.handle.id);
            write(2, buf, (size_t)n);
        }
        return {};
    }

    void shutdown() noexcept { destroy_resources(); }

    void ortho(float left, float right, float bottom, float top, float near_, float far_) noexcept {
        mm_math::mat4 m = mm_math::mat4::ortho_mt(left, right, bottom, top, near_, far_);
        m.store_column_major(view_proj);
    }

    void                     upload_camera() noexcept { backend.update_buffer(camera_ubo, view_proj, 0, sizeof(view_proj)); }

    Expected<void, RHIError> begin_frame() noexcept {
        flush_text();
        sprite_vertex_count = 0;
        sprite_index_count  = 0;
        temp_arena.reset();
        frame_arena.swap();
        graph.init(frame_arena.current().template alloc_array<Command>(MAX_COMMANDS), MAX_COMMANDS);

        upload_camera();
        return {};
    }

    // Execute the render graph — call between begin_pass and end_pass
    void submit() noexcept {
        ZoneScoped;
        graph.sort();

#if defined(ENGINE_ENABLE_ASSERT)
        bool has_pipeline = false;
        for (uint32_t i = 0; i < graph.command_count; ++i) {
            auto &cmd = graph.commands[i];
            switch (cmd.type) {
            case CmdType::BindPipeline:
                has_pipeline = true;
                break;
            case CmdType::DrawIndexed:
            case CmdType::Draw:
                if (!has_pipeline) {
                    assert(false && "Draw without pipeline — sort key bug?");
                }
                break;
            default:
                break;
            }
        }
#endif

        for (uint32_t i = 0; i < graph.command_count; ++i) {
            auto &cmd = graph.commands[i];
            switch (cmd.type) {
            case CmdType::BindPipeline: {
                auto h = cmd.data.bind_pipeline.pipeline;
#if defined(ENGINE_ENABLE_ASSERT)
                auto *pl = backend.pipelines.get(h.handle);
                if (!pl) {
                    fprintf(stderr, "BINDPIPELINE FAIL: handle.id=%u gen=%u\n", h.handle.id, h.handle.gen);
                } else if (!pl->pipeline) {
                    fprintf(stderr, "BINDPIPELINE NIL: handle.id=%u gen=%u pl=%p\n", h.handle.id, h.handle.gen, (void *)pl);
                }
#endif
                backend.bind_pipeline(h);
                break;
            }
            case CmdType::BindVertexBuffer: {
                auto    &vb      = cmd.data.bind_vb;
                uint32_t binding = vb.binding;
                backend.bind_vertex_buffers(&vb.buffer, 1, &vb.offset, &vb.stride, &binding);
                break;
            }
            case CmdType::BindIndexBuffer: {
                auto &ib = cmd.data.bind_ib;
                backend.bind_index_buffer(ib.buffer, ib.type, ib.offset);
                break;
            }
            case CmdType::BindFragmentTexture: {
                auto &ft = cmd.data.bind_frag_tex;
                backend.bind_fragment_texture(ft.texture, ft.index);
                break;
            }
            case CmdType::BindFragmentSampler: {
                auto &fs = cmd.data.bind_frag_samp;
                backend.bind_fragment_sampler(fs.sampler, fs.index);
                break;
            }
            case CmdType::Draw: {
                auto &d = cmd.data.draw;
                backend.draw(d.vertex_count, d.instance_count, d.first_vertex, d.first_instance);
                break;
            }
            case CmdType::DrawIndexed: {
                auto &di = cmd.data.draw_indexed;
                backend.draw_indexed(di.index_count, di.instance_count, di.first_index, di.vertex_offset);
                break;
            }
            case CmdType::BeginPass:
                backend.begin_pass(cmd.data.begin_pass.pass);
                break;
            case CmdType::EndPass:
                backend.end_pass();
                break;
            case CmdType::SetViewport:
                break;
            case CmdType::SetScissor: {
                auto &s = cmd.data.set_scissor;
                backend.set_scissor(s.x, s.y, s.w, s.h);
                break;
            }
            default:
                break;
            }
        }

        FrameMark;
    }

    void end_frame() noexcept { backend.end_frame(); }

  private:
    // Internal: vertex generation + draw, parameterized by pipeline/texture/sampler
    // Groups sprites by per-sprite tex_id, resolving registered textures from the batch.
    void flush_sprites_impl(SpriteBatch &batch, PipelineHandle pipeline, TextureHandle fallback_tex, SamplerHandle sampler) noexcept {
        ZoneScoped;
        if (batch.count == 0) {
            return;
        }

        cull_sprites(batch);

        // Collect unique tex_ids from active sprites
        uint16_t unique_tex[64];
        uint8_t  unique_count = 0;
        for (uint16_t i = 0; i < batch.count; ++i) {
            if (!batch.active[i]) {
                continue;
            }
            uint16_t tid   = batch.tex_id[i];
            bool     found = false;
            for (uint8_t j = 0; j < unique_count; ++j) {
                if (unique_tex[j] == tid) {
                    found = true;
                    break;
                }
            }
            if (!found && unique_count < 64) {
                unique_tex[unique_count++] = tid;
            }
        }

        if (unique_count == 0) {
            return;
        }

        // Per-texture draw calls
        for (uint8_t ti = 0; ti < unique_count; ++ti) {
            uint16_t      tid = unique_tex[ti];

            // Resolve texture handle
            TextureHandle tex = fallback_tex;
            if (tid != 0) {
                TextureHandle rt = batch.lookup_texture(tid);
                if (rt.handle.id != 0) {
                    tex = rt;
                }
            }

            // Count sprites with this tex_id
            uint16_t sprite_count = 0;
            for (uint16_t i = 0; i < batch.count; ++i) {
                if (batch.active[i] && batch.tex_id[i] == tid) {
                    ++sprite_count;
                }
            }
            if (sprite_count == 0) {
                continue;
            }

            auto *verts   = temp_arena.alloc_array<SpriteVertex>(sprite_count * 4);
            auto *indices = temp_arena.alloc_array<uint16_t>(sprite_count * 6);
            if (!verts || !indices) {
                continue;
            }

            uint32_t vert_count = 0;
            uint32_t idx_count  = 0;

            for (uint16_t i = 0; i < batch.count; ++i) {
                if (!batch.active[i]) {
                    continue;
                }
                if (batch.tex_id[i] != tid) {
                    continue;
                }

                float    sx  = batch.world_x[i];
                float    sy  = batch.world_y[i];
                float    scx = batch.scale_x[i] * 0.5f;
                float    scy = batch.scale_y[i] * 0.5f;
                float    rot = batch.rotation[i];
                uint32_t col = batch.color[i];

                float    u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                if (batch.atlas_w[i] > 0 && batch.atlas_h[i] > 0 && batch.atlas_tex_w > 0 && batch.atlas_tex_h > 0) {
                    u0 = static_cast<float>(batch.atlas_x[i]) / batch.atlas_tex_w;
                    v0 = static_cast<float>(batch.atlas_y[i]) / batch.atlas_tex_h;
                    u1 = static_cast<float>(batch.atlas_x[i] + batch.atlas_w[i]) / batch.atlas_tex_w;
                    v1 = static_cast<float>(batch.atlas_y[i] + batch.atlas_h[i]) / batch.atlas_tex_h;
                }

                float    c       = std::cos(rot);
                float    s       = std::sin(rot);

                float    cx[4]   = {-scx, scx, -scx, scx};
                float    cy[4]   = {-scy, -scy, scy, scy};
                float    uv_u[4] = {u0, u1, u0, u1};
                float    uv_v[4] = {v0, v0, v1, v1};

                uint32_t base    = vert_count;
                for (int j = 0; j < 4; ++j) {
                    float rx = cx[j] * c - cy[j] * s;
                    float ry = cx[j] * s + cy[j] * c;
                    auto &v  = verts[vert_count++];
                    v.x      = SpriteBatch::float_to_f16(sx + rx);
                    v.y      = SpriteBatch::float_to_f16(sy + ry);
                    v.u      = SpriteBatch::float_to_f16(uv_u[j]);
                    v.v      = SpriteBatch::float_to_f16(uv_v[j]);
                    v.color  = col;
                }

                indices[idx_count++] = static_cast<uint16_t>(base);
                indices[idx_count++] = static_cast<uint16_t>(base + 1);
                indices[idx_count++] = static_cast<uint16_t>(base + 2);
                indices[idx_count++] = static_cast<uint16_t>(base + 1);
                indices[idx_count++] = static_cast<uint16_t>(base + 3);
                indices[idx_count++] = static_cast<uint16_t>(base + 2);
            }

            if (vert_count == 0) {
                continue;
            }

            uint32_t vb_byte_offset = sprite_vertex_count * sizeof(SpriteVertex);
            uint32_t ib_byte_offset = sprite_index_count * sizeof(uint16_t);

            backend.update_buffer(sprite_vb, verts, vb_byte_offset, vert_count * sizeof(SpriteVertex));
            backend.update_buffer(sprite_ib, indices, ib_byte_offset, idx_count * sizeof(uint16_t));

            SortKey key{0, 0, 0, 1.0f};
            graph.bind_pipeline(pipeline, key);
            graph.bind_vertex_buffer(sprite_vb, 0, vb_byte_offset, sizeof(SpriteVertex), key);
            graph.bind_vertex_buffer(camera_ubo, 1, 0, sizeof(float) * 16, key);
            graph.bind_index_buffer(sprite_ib, IndexType::Uint16, ib_byte_offset, key);
            graph.bind_fragment_texture(tex, 0, key);
            graph.bind_fragment_sampler(sampler, 0, key);
            graph.draw_indexed(key, idx_count, 1, 0, 0);

            sprite_vertex_count += vert_count;
            sprite_index_count  += idx_count;
        }
    }

  public:
    // Default flush with sprite pipeline + white texture
    void flush_sprites(SpriteBatch &batch) noexcept { flush_sprites_impl(batch, sprite_pipeline, white_tex, default_sampler); }

    // Flush with a specific texture (e.g. from a sprite atlas)
    void flush_sprites(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler) noexcept {
        flush_sprites_impl(batch, sprite_pipeline, texture, sampler);
    }

    // Flush with a Material (pipeline + texture + sampler)
    void flush_sprites(SpriteBatch &batch, const Material &mat) noexcept { flush_sprites_impl(batch, mat.pipeline, mat.texture, mat.sampler); }

    // Flush with a Technique (uses the first pass)
    void flush_sprites(SpriteBatch &batch, const Technique &tech) noexcept { flush_sprites(batch, tech.current_pass()); }

    void set_scissor(int16_t x, int16_t y, uint16_t w, uint16_t h) noexcept {
        float s = content_scale;
        graph.set_scissor(static_cast<int16_t>(static_cast<float>(x) * s), static_cast<int16_t>(static_cast<float>(y) * s),
                          static_cast<uint16_t>(static_cast<float>(w) * s), static_cast<uint16_t>(static_cast<float>(h) * s));
    }

    void cull_sprites(SpriteBatch &batch) noexcept {
        const float view_left   = 0.0f;
        const auto  view_right  = static_cast<float>(width);
        const float view_top    = 0.0f;
        const auto  view_bottom = static_cast<float>(height);
        for (uint16_t i = 0; i < batch.count; ++i) {
            if (!batch.active[i]) {
                continue;
            }
            const float hw     = batch.scale_x[i] * 0.5f; // true half-width
            const float hh     = batch.scale_y[i] * 0.5f; // true half-height
            const float radius = std::sqrt(hw * hw + hh * hh);

            const float sx     = batch.world_x[i]; // sprite centre x
            const float sy     = batch.world_y[i]; // sprite centre y

            // Separating-axis test: cull only when the bounding circle is
            // entirely outside one of the four view edges.
            if (sx + radius < view_left || sx - radius > view_right || sy + radius < view_top || sy - radius > view_bottom) {
                batch.active[i] = 0;
            }
        }
    }

    void                     bind_pipeline(PipelineHandle pipeline) noexcept { graph.bind_pipeline(pipeline); }

    Expected<void, RHIError> create_default_pipelines() noexcept {
        PipelineDesc desc{};
        desc.prim_type              = PrimitiveType::Triangle;
        desc.cull_mode              = CullMode::None;
        desc.src_blend              = BlendFactor::SrcAlpha;
        desc.dst_blend              = BlendFactor::OneMinusSrcAlpha;
        desc.blend_op               = BlendOp::Add;
        desc.color_count            = 1;
        desc.color_formats[0]       = PixelFormat::B8G8R8A8_SRGB;
        desc.depth_format           = static_cast<PixelFormat>(0);
        desc.depth_test             = false;
        desc.depth_write            = false;

        desc.vertex_attr_count      = 3;
        desc.vertex_attrs[0]        = {0, PixelFormat::R16G16_FLOAT, 0, 12};
        desc.vertex_attrs[1]        = {1, PixelFormat::R16G16_FLOAT, 4, 12};
        desc.vertex_attrs[2]        = {2, PixelFormat::R8G8B8A8_UNORM, 8, 12};

        desc.descriptor_count       = 2;
        desc.descriptor_bindings[0] = {0, DescriptorType::UniformBuffer, 1, 1};
        desc.descriptor_bindings[1] = {1, DescriptorType::CombinedImageSampler, 2, 1};

        {
            char buf[256];
            int  n = snprintf(buf, sizeof(buf), "create_default: about to create sprite_pipeline\n");
            write(2, buf, (size_t)n);
        }
        // Sprite pipeline
        desc.vertex_shader   = shader::sprite_vertex();
        desc.fragment_shader = shader::sprite_fragment();

        auto res             = backend.create_pipeline(desc);
        if (!res) {
            return make_unexpected(res.error());
        }
        sprite_pipeline       = *res;

        // Additive blend sprite pipeline
        PipelineDesc add_desc = desc;
        add_desc.src_blend    = BlendFactor::SrcAlpha;
        add_desc.dst_blend    = BlendFactor::One;
        auto add_res          = backend.create_pipeline(add_desc);
        if (!add_res) {
            return make_unexpected(add_res.error());
        }
        sprite_additive_pipeline = *add_res;

        // Multiply blend sprite pipeline
        PipelineDesc mul_desc    = desc;
        mul_desc.src_blend       = BlendFactor::Zero;
        mul_desc.dst_blend       = BlendFactor::SrcColor;
        auto mul_res             = backend.create_pipeline(mul_desc);
        if (!mul_res) {
            return make_unexpected(mul_res.error());
        }
        sprite_multiply_pipeline = *mul_res;

        // Opaque sprite pipeline
        PipelineDesc opq_desc    = desc;
        opq_desc.src_blend       = BlendFactor::One;
        opq_desc.dst_blend       = BlendFactor::Zero;
        auto opq_res             = backend.create_pipeline(opq_desc);
        if (!opq_res) {
            return make_unexpected(opq_res.error());
        }
        sprite_opaque_pipeline     = *opq_res;

        // SDF pipeline (same 12-byte SpriteVertex format as sprites)
        PipelineDesc sdf_desc      = desc;
        sdf_desc.vertex_shader     = shader::sdf_vertex();
        sdf_desc.fragment_shader   = shader::sdf_fragment();
        sdf_desc.vertex_attr_count = 3;
        sdf_desc.vertex_attrs[0]   = {0, PixelFormat::R16G16_FLOAT, 0, 12};
        sdf_desc.vertex_attrs[1]   = {1, PixelFormat::R16G16_FLOAT, 4, 12};
        sdf_desc.vertex_attrs[2]   = {2, PixelFormat::R8G8B8A8_UNORM, 8, 12};

        auto res2                  = backend.create_pipeline(sdf_desc);
        if (!res2) {
            return make_unexpected(res2.error());
        }
        sdf_pipeline                    = *res2;

        // Particle pipeline
        PipelineDesc particle_desc      = desc;
        particle_desc.vertex_shader     = shader::particle_vertex();
        particle_desc.fragment_shader   = shader::particle_fragment();
        particle_desc.vertex_attr_count = 0;

        auto res3                       = backend.create_pipeline(particle_desc);
        if (!res3) {
            return make_unexpected(res3.error());
        }
        particle_pipeline                                              = *res3;

        // Build built-in materials
        materials_[static_cast<uint8_t>(MaterialType::SpriteAlpha)]    = {sprite_pipeline, white_tex, default_sampler};
        materials_[static_cast<uint8_t>(MaterialType::SpriteAdditive)] = {sprite_additive_pipeline, white_tex, default_sampler};
        materials_[static_cast<uint8_t>(MaterialType::SpriteMultiply)] = {sprite_multiply_pipeline, white_tex, default_sampler};
        materials_[static_cast<uint8_t>(MaterialType::SpriteOpaque)]   = {sprite_opaque_pipeline, white_tex, default_sampler};
        materials_[static_cast<uint8_t>(MaterialType::SDF)]            = {sdf_pipeline, font_tex, font_sampler};
        materials_[static_cast<uint8_t>(MaterialType::Particle)]       = {particle_pipeline, white_tex, default_sampler};

        return {};
    }

    // ─── Material accessors ─────────────────────────────────────
    const Material &material(MaterialType type) const noexcept { return materials_[static_cast<uint8_t>(type)]; }

    Material        make_material(PipelineHandle pipeline, TextureHandle texture, SamplerHandle sampler) const noexcept { return {pipeline, texture, sampler}; }

    // แทนที่ draw_text method เดิมด้วยอันนี้
    void            draw_text(BitmapFont &font, const char *text, float x, float y, uint32_t color = 0xFFFFFFFF, float scale = 1.0f) noexcept {
        auto len = static_cast<uint32_t>(std::strlen(text));
        if (len == 0) {
            return;
        }

        uint32_t max_glyphs = len > TEXT_MAX_GLYPH ? TEXT_MAX_GLYPH : len;
        auto    *verts      = temp_arena.alloc_array<SpriteVertex>(max_glyphs * 4);
        if (!verts) {
            return;
        }
        auto *indices = temp_arena.alloc_array<uint16_t>(max_glyphs * 6);
        if (!indices) {
            return;
        }

        Utf8Decoder dec(text, len);
        float       cursor_x         = x;
        float       cursor_y         = y;
        uint32_t    vert_count       = 0;
        uint32_t    idx_count        = 0;

        // Thai combining character state
        uint32_t    prev_consonant   = 0;
        float       prev_consonant_x = 0;
        float       prev_consonant_y = 0;

        for (uint16_t g = 0; g < max_glyphs; ++g) {
            uint32_t cp = dec.next();
            if (cp == 0) {
                break;
            }
            // {
            //     char buf[256];
            //     int  n = snprintf(buf, sizeof(buf), "draw_text: U+%04X\n", cp);
            //     write(2, buf, (size_t)n);
            // }

            // Helper for Thai detection
            auto is_thai_consonant = [](uint32_t c) -> bool { return (c >= 0x0E01 && c <= 0x0E2F) || (c == 0x0E30) || (c == 0x0E32) || (c == 0x0E33); };
            // auto is_thai_vowel_rear  = [](uint32_t c) -> bool { return (c == 0x0E30) || (c == 0x0E32) || (c == 0x0E33); };
            auto is_thai_vowel_above = [](uint32_t c) -> bool { return (c == 0x0E31) || (c >= 0x0E34 && c <= 0x0E37) || (c == 0x0E4D); };
            auto is_thai_vowel_below = [](uint32_t c) -> bool { return (c >= 0x0E38 && c <= 0x0E39); };
            auto is_thai_vowel_front = [](uint32_t c) -> bool { return (c >= 0x0E40 && c <= 0x0E44); };
            auto is_thai_tone_mark   = [](uint32_t c) -> bool { return (c >= 0x0E48 && c <= 0x0E4B); };
            auto is_thai_combining   = [&](uint32_t c) -> bool { return is_thai_vowel_above(c) || is_thai_vowel_below(c) || is_thai_tone_mark(c); };

            if (cp == ' ') {
                const GlyphInfo *space_glyph = font.get_glyph(' ');
                if (space_glyph) {
                    cursor_x += static_cast<float>(space_glyph->advance) * scale;
                } else {
                    cursor_x += 16.0f * scale;
                }
                prev_consonant = 0;
                continue;
            }

            if (cp == '\n') {
                cursor_x        = x;
                cursor_y       += font.line_height * scale;
                prev_consonant  = 0;
                continue;
            }

            const GlyphInfo *glyph = nullptr;

            // Get glyph (supports Thai range)
            if (cp >= 0x0E01 && cp <= 0x0E5B) {
                int thai_idx = cp - 0x0E01;
                if (thai_idx >= 0 && thai_idx < MAX_GLYPHS_TH) {
                    static GlyphInfo thai_glyph;
                    auto            &th_g = font.glyphs_th[thai_idx];
                    if (th_g.w > 0) {
                        thai_glyph.u         = th_g.u;
                        thai_glyph.v         = th_g.v;
                        thai_glyph.w         = th_g.w;
                        thai_glyph.h         = th_g.h;
                        thai_glyph.bearing_x = th_g.bearing_x;
                        thai_glyph.bearing_y = th_g.bearing_y;
                        thai_glyph.advance   = th_g.advance;
                        glyph                = &thai_glyph;
                    }
                }
            } else {
                glyph = font.get_glyph(cp);
            }

            if (!glyph || glyph->w == 0 || glyph->h == 0) {
                glyph = font.get_glyph('?');
                if (!glyph) {
                    continue;
                }
            }

            float       gx, gy;
            static bool warned_sara_u  = false;
            static bool warned_sara_ue = false;
            if ((cp == 0x0E38 || cp == 0x0E39) && !warned_sara_u) {
                warned_sara_u = true;
                {
                    char buf[256];
                    int  n = snprintf(buf, sizeof(buf), "draw_text: Sara UU or U U+%04X\n", cp);
                    write(2, buf, (size_t)n);
                }
            }
            if (cp == 0x0E36 && !warned_sara_ue) {
                warned_sara_ue = true;
                {
                    char buf[256];
                    int  n = snprintf(buf, sizeof(buf), "draw_text: Sara UE U+%04X\n", cp);
                    write(2, buf, (size_t)n);
                }
            }
            // Front vowels (เ แ โ ใ ไ)
            if (is_thai_vowel_front(cp)) {
                gx = cursor_x + static_cast<float>(glyph->bearing_x) * scale;
                gy = cursor_y + static_cast<float>(glyph->bearing_y) * scale;
                // Draw front vowel at current cursor position
                // (will advance cursor after)
            }
            // Combining characters (vowels above/below, tone marks)
            else if (is_thai_combining(cp) && prev_consonant != 0) {
                gx = cursor_x + static_cast<float>(glyph->bearing_x) * scale;
                gy = cursor_y + static_cast<float>(glyph->bearing_y) * scale;

                if (is_thai_vowel_above(cp)) {
                    prev_consonant  = cp;
                    gy             -= static_cast<float>(font.line_height) * 0.06f * scale;
                } else if (is_thai_vowel_below(cp)) {
                    gy += static_cast<float>(font.line_height) * 0.06f * scale;
                } else if (is_thai_tone_mark(cp)) {
                    if (is_thai_vowel_above(prev_consonant)) {
                        gy -= static_cast<float>(font.line_height) * 0.25f * scale;
                    } else {
                        gy -= static_cast<float>(font.line_height) * 0.06f * scale;
                    }
                }

                // Don't advance cursor for combining marks
                goto add_quad;
            }
            // Base character
            else {
                gx = cursor_x + static_cast<float>(glyph->bearing_x) * scale;
                gy = cursor_y + static_cast<float>(glyph->bearing_y) * scale;

                // Store for potential combining marks
                if (is_thai_consonant(cp)) {
                    prev_consonant   = cp;
                    prev_consonant_x = cursor_x;
                    prev_consonant_y = cursor_y;
                } else {
                    prev_consonant = 0;
                }
            }

            // Advance cursor for base characters and front vowels
            cursor_x += static_cast<float>(glyph->advance) * scale;

        add_quad:
            float    gw       = static_cast<float>(glyph->w) * scale;
            float    gh       = static_cast<float>(glyph->h) * scale;

            float    u0       = static_cast<float>(glyph->u) / font.atlas_w;
            float    v0       = static_cast<float>(glyph->v) / font.atlas_h;
            float    u1       = static_cast<float>(glyph->u + glyph->w) / font.atlas_w;
            float    v1       = static_cast<float>(glyph->v + glyph->h) / font.atlas_h;

            uint32_t base     = vert_count;
            auto     add_vert = [&](float px, float py, float pu, float pv) {
                auto &v = verts[vert_count++];
                v.x     = SpriteBatch::float_to_f16(px);
                v.y     = SpriteBatch::float_to_f16(py);
                v.u     = SpriteBatch::float_to_f16(pu);
                v.v     = SpriteBatch::float_to_f16(pv);
                v.color = color;
            };

            add_vert(gx, gy, u0, v0);
            add_vert(gx + gw, gy, u1, v0);
            add_vert(gx, gy + gh, u0, v1);
            add_vert(gx + gw, gy + gh, u1, v1);

            indices[idx_count++] = static_cast<uint16_t>(base);
            indices[idx_count++] = static_cast<uint16_t>(base + 1);
            indices[idx_count++] = static_cast<uint16_t>(base + 2);
            indices[idx_count++] = static_cast<uint16_t>(base + 1);
            indices[idx_count++] = static_cast<uint16_t>(base + 3);
            indices[idx_count++] = static_cast<uint16_t>(base + 2);
        }

        if (vert_count == 0) {
            return;
        }

        if (text_vertex_count + vert_count > MAX_TEXT_VERTS || text_index_count + idx_count > MAX_TEXT_INDICES) {
            return;
        }

        uint32_t vb_byte_offset = text_vertex_count * sizeof(SpriteVertex);
        uint32_t ib_byte_offset = text_index_count * sizeof(uint16_t);

        backend.update_buffer(text_vb, verts, vb_byte_offset, vert_count * sizeof(SpriteVertex));
        backend.update_buffer(text_ib, indices, ib_byte_offset, idx_count * sizeof(uint16_t));

        SortKey key{1, 0, 0, 1.0f};
        graph.bind_pipeline(sdf_pipeline, key);
        graph.bind_vertex_buffer(text_vb, 0, vb_byte_offset, sizeof(SpriteVertex), key);
        graph.bind_vertex_buffer(camera_ubo, 1, 0, sizeof(float) * 16, key);
        graph.bind_index_buffer(text_ib, IndexType::Uint16, ib_byte_offset, key);
        graph.bind_fragment_texture(font_tex, 0, key);
        graph.bind_fragment_sampler(font_sampler, 0, key);
        graph.draw_indexed(key, idx_count, 1, 0, 0);

        text_vertex_count += vert_count;
        text_index_count  += idx_count;
    }

    void flush_text() noexcept {
        text_vertex_count = 0;
        text_index_count  = 0;

        // if (text_index_count == 0) {
        //     return;
        // }

        // (void)backend.update_buffer(text_vb, text_vertex_cache, 0, text_vertex_count * sizeof(SpriteVertex));
        // (void)backend.update_buffer(text_ib, text_index_cache, 0, text_index_count * sizeof(uint16_t));

        // SortKey key{1, 0, 0, 1.0f};
        // graph.bind_pipeline(sdf_pipeline, key);
        // graph.bind_vertex_buffer(text_vb, 0, 0, sizeof(SpriteVertex), key);
        // graph.bind_vertex_buffer(camera_ubo, 1, 0, sizeof(float) * 16, key);
        // graph.bind_index_buffer(text_ib, IndexType::Uint16, 0, key);
        // graph.bind_fragment_texture(font_tex, 0, key);
        // graph.draw_indexed(key, text_index_count, 1);
        // text_index_count  = 0;
        // text_vertex_count = 0;
    }

    void resize(uint32_t w, uint32_t h) noexcept {
        width      = w;
        height     = h;
        inv_width  = 1.0f / static_cast<float>(w);
        inv_height = 1.0f / static_cast<float>(h);
        ortho(0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, -1.0f, 1.0f);
    }
};

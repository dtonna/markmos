// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_arena.hpp"
#include "../core/mm_expected.hpp"
#include "../core/mm_log.hpp"
#include "../core/mm_tracy.hpp"
#include "../game/mm_particle_pool.hpp"
#include "../math/mm_color.h"
#include "../math/mm_math.h"
#include "../math/mm_mat4.h"
#include "../rhi/mm_rhi_concept.hpp"
#include "mm_font_atlas.hpp"
#include "mm_font_data.hpp"
#include "mm_font_sym_data.hpp"
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
#include <unistd.h>
#if defined(__APPLE__)
#    include <mach/machine.h>
#endif

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
    ActiveBackend *backend = nullptr;

    DoubleArena    frame_arena;
    RenderGraph    graph;

    mm_math::mat4 view_proj{};

    PipelineHandle            sprite_pipeline{};
    PipelineHandle            sprite_additive_pipeline{};
    PipelineHandle            sprite_multiply_pipeline{};
    PipelineHandle            sprite_opaque_pipeline{};
    PipelineHandle            sdf_pipeline{};
    PipelineHandle            particle_pipeline{};
    PipelineHandle            rounded_sprite_pipeline{};
    PipelineHandle            rounded_sprite_glow_pipeline{};
    PipelineHandle            rounded_sprite_border_pipeline{};
    PipelineHandle            sprite_outline_pipeline{};
    PipelineHandle            clip_rect_pipeline{};
    PipelineHandle            dissolve_pipeline{};
    PipelineHandle            grayscale_pipeline{};
    PipelineHandle            blur_pipeline{};
    PipelineHandle            color_grade_pipeline{};
    PipelineHandle            normal_derive_pipeline{};
    PipelineHandle            normal_map_pipeline{};
    PipelineHandle            cartoon_pipeline{};
    PipelineHandle            plastic_pipeline{};
    PipelineHandle            glow_pulse_pipeline{};
    PipelineHandle            gold_border_pipeline{};
    PipelineHandle            gold_stay_pipeline{};
    PipelineHandle            ring_pipeline{}; // analytic progress ring (mm_ui_wprogress)

    BufferHandle              rounded_params_ubo{};
    BufferHandle              rounded_params_glow_ubo{};
    BufferHandle              blur_params_ubo{};
    BufferHandle              color_grade_ubo{};
    // Lab-port UBOs: one buffer PER FLUSH FUNCTION, not per struct.
    // update_buffer() memcpys immediately while draws execute later at
    // submit(), so sharing one buffer across flushes would let the last
    // update win for every draw in the frame.
    BufferHandle              normal_derive_ubo{};
    BufferHandle              normal_map_ubo{};
    BufferHandle              cartoon_ubo{};
    BufferHandle              plastic_ubo{};
    BufferHandle              glow_pulse_ubo{};
    BufferHandle              gold_border_ubo{};
    BufferHandle              gold_stay_ubo{};
    // Per-flush one-slot RingParams pool. A single shared buffer gives the
    // SAME frame-global collision the sweep field used to have: every flush_ring
    // overwrites it and every recorded draw executes at submit() with the LAST
    // written values. With a small pool, up to K ring parameter sets per frame
    // each bind their own buffer - enough for every progressbar on one page.
    // Past K it degrades to slot K-1 (old semantics) with a one-line log.
    BufferHandle              ring_params_ubos[8]{};
    u32                       ring_param_slot = 0;
    bool                      ring_param_overflow = false;

    Material                  materials[static_cast<u8>(e_material_type::COUNT)];

    BufferHandle              camera_ubo{};
    BufferHandle              sprite_vb{};
    BufferHandle              sprite_ib{};
    BufferHandle              particle_ib{};
    BufferHandle              particle_atlas_ubo{};
    u32                  ubo_alignment{};

    TextureHandle             white_tex{};
    SamplerHandle             default_sampler{};

    TextureHandle             font_tex{};
    SamplerHandle             font_sampler{};
    BufferHandle              text_vb{};
    BufferHandle              text_ib{};
    BitmapFont                default_font{};

    static constexpr u32 MAX_SPRITE_VERTS      = 16384 * 4;
    static constexpr u32 MAX_SPRITE_IDX        = 16384 * 6;
    static constexpr u16 TEXT_MAX_GLYPH        = 1024;
    static constexpr u32 MAX_TEXT_VERTS        = 4096;
    static constexpr u32 MAX_TEXT_INDICES      = 6144;

    static constexpr size_t   COMMAND_STORAGE_BYTES = sizeof(Command) * MAX_COMMANDS;
    static_assert((COMMAND_STORAGE_BYTES % 64) == 0, "Command storage must be a multiple of cache-line alignment");

    struct RoundedParams {
        f32 time;             // render time in seconds
        f32 glow_intensity;   // 0 = off, 1.5 = selected, 2.5 = hint
        f32 glow_width;       // SDF distance units (0.05–0.15)
        f32 glow_pulse_freq;  // rad/s (precomputed: speed_Hz * 2π), 0 = static
        f32 sdf_aa_scale;     // AA scale factor (default 1.0, like button.frag)
        f32 _pad0;
        f32 _pad1;
        f32 _pad2;
        f32 glow_color[4];    // RGBA
    };
    static_assert(sizeof(RoundedParams) == sizeof(f32) * 12, "RoundedParams must be three float4");

    // z is unused padding: the sweep is NOT here. Draw commands execute at
    // submit(), so a UBO's content is global to the frame and every ring would
    // render the last ring's sweep. The sweep travels in the vertex stream
    // (SpriteVertex::radius, which this pipeline reads as a sweep).
    struct RingParams {
        f32 p[4]; // x=outer_r, y=thickness (local units), z=unused, w=aa_scale
    };
    static_assert(sizeof(RingParams) == sizeof(f32) * 4, "RingParams must be one float4");

    struct BlurParams {
        f32 direction_x;   // 1.0 = horizontal, 0.0 = vertical
        f32 direction_y;   // 0.0 = horizontal, 1.0 = vertical
        f32 radius;        // blur strength in pixels
        f32 weights[7];    // Gaussian weights (7 taps = 15 samples with center)
        f32 _pad[2];       // pad to 48 bytes (3 float4)
    };
    static_assert(sizeof(BlurParams) == sizeof(f32) * 12, "BlurParams must be three float4");

    struct ColorGrade {
        f32 brightness;   // 0..2, default 1.0
        f32 contrast;     // 0..2, default 1.0
        f32 saturation;   // 0..2, default 1.0
        f32 hue_matrix[9]; // 3x3 column-major hue rotation matrix
        f32 _pad[4];      // pad to 64 bytes (4 float4)
    };
    static_assert(sizeof(ColorGrade) == sizeof(f32) * 16, "ColorGrade must be four float4");

    // ─── Lab-port param blocks (all float4 members: Metal 16-byte packing) ──
    struct NormalParams {
        f32 light_dir[4]; // xyz = direction TO light, w = intensity
        f32 misc[4];      // x = ambient, y = spec strength, z = height_scale, w = spare
    };
    static_assert(sizeof(NormalParams) == sizeof(f32) * 8, "NormalParams must be two float4");

    struct CartoonParams {
        f32 light_dir[4]; // xyz = direction TO light, w = intensity
        f32 misc[4];      // x = ambient, y = spec on/off, z = height_scale, w = spare
        f32 toon[4];      // x = bands (2..5), y = ink threshold, z = ink strength, w = spare
    };
    static_assert(sizeof(CartoonParams) == sizeof(f32) * 12, "CartoonParams must be three float4");

    struct PlasticParams {
        f32 light_dir[4]; // xyz = direction TO light, w = intensity
        f32 misc[4];      // x = ambient, y = spec strength, z = unused, w = spare
        f32 plastic[4];   // x = clearcoat, y = fresnel strength, z = wrap 0..1, w = shininess
    };
    static_assert(sizeof(PlasticParams) == sizeof(f32) * 12, "PlasticParams must be three float4");

    struct GlowParams {
        f32 timing[4]; // x = time (s), y = duration/speed, z = expand/width, w = ring width/spare
        f32 glow[4];   // rgba glow color
        f32 misc[4];   // x = card aspect (w/h), y = intensity, z = quad scale k, w = spare
    };
    static_assert(sizeof(GlowParams) == sizeof(f32) * 12, "GlowParams must be three float4");

    f32      render_time = 0.0f;

    u32   width, height;
    f32      inv_width, inv_height;
    f32      content_scale           = 1.0f;

    u32   text_vertex_count       = 0;
    u32   text_index_count        = 0;
    // draw_text calls DROPPED this frame because the shared text budget
    // (MAX_TEXT_VERTS / 4 = 1024 glyphs, ~1024 indices worth) was full. The drop
    // is per-CALL, so an over-labelled page loses whichever strings happen to
    // come last in pool order - and the HUD, which is drawn last, is the usual
    // casualty. That is how a page's prose can silently blind the one surface
    // that would have told you the page was over budget, so it is counted here
    // and read out by mm_07's HUD. Reset with the other per-frame counters.
    u32   text_dropped_calls      = 0;
    u32   sprite_vertex_count     = 0;
    u32   sprite_index_count      = 0;
    u32   particle_instance_count = 0;

    Command   *command_storage_0       = nullptr;
    Command   *command_storage_1       = nullptr;
    // alignas(64) Command command_storage_0[MAX_COMMANDS];
    // alignas(64) Command command_storage_1[MAX_COMMANDS];

    FrameArena temp_arena;
    // 2MB: a full 16384-sprite batch needs ~1.7MB of staging
    // (4 verts × 24B + 6 indices × 2B per sprite ≈ 108B), so the
    // old 1MB cap silently dropped any batch over ~9700 sprites —
    // mm_06's 10k ember pool hit this every frame.
    alignas(64) u8 temp_storage[2 * 1024 * 1024];

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
        if (!backend) {
            return;
        }
        destroy_safe(sprite_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(sprite_additive_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(sprite_multiply_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(sprite_opaque_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(sdf_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(particle_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(rounded_sprite_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(rounded_sprite_glow_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(rounded_sprite_border_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(sprite_outline_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(clip_rect_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(dissolve_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(grayscale_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(blur_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(color_grade_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(normal_derive_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(normal_map_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(cartoon_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(plastic_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(glow_pulse_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(gold_border_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(gold_stay_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(ring_pipeline, [&](auto &h) { backend->destroy_pipeline(h); });
        destroy_safe(camera_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(sprite_vb, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(sprite_ib, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(particle_ib, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(particle_atlas_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(font_tex, [&](auto &h) { backend->destroy_texture(h); });
        destroy_safe(font_sampler, [&](auto &h) { backend->destroy_sampler(h); });
        destroy_safe(text_vb, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(text_ib, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(white_tex, [&](auto &h) { backend->destroy_texture(h); });
        destroy_safe(default_sampler, [&](auto &h) { backend->destroy_sampler(h); });
        destroy_safe(rounded_params_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(rounded_params_glow_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(blur_params_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(color_grade_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(normal_derive_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(normal_map_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(cartoon_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(plastic_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(glow_pulse_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(gold_border_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        destroy_safe(gold_stay_ubo, [&](auto &h) { backend->destroy_buffer(h); });
        for (u32 i = 0; i < 8; ++i) {
            destroy_safe(ring_params_ubos[i], [&](auto &h) { backend->destroy_buffer(h); });
        }

        delete[] command_storage_0;
        delete[] command_storage_1;
    }

    Expected<void, RHIError> init(ActiveBackend *backend_ptr, void *window_handle, u32 screen_w, u32 screen_h) noexcept {
        backend = backend_ptr;
        (void)window_handle;
        width             = screen_w;
        height            = screen_h;
        inv_width         = screen_w ? 1.0f / static_cast<f32>(screen_w) : 1.0f;
        inv_height        = screen_h ? 1.0f / static_cast<f32>(screen_h) : 1.0f;

        command_storage_0 = new Command[MAX_COMMANDS];
        command_storage_1 = new Command[MAX_COMMANDS];

        frame_arena.init(command_storage_0, COMMAND_STORAGE_BYTES, command_storage_1, COMMAND_STORAGE_BYTES);
        temp_arena.init(temp_storage, sizeof(temp_storage));

        BufferDesc ubo_desc{};
        ubo_desc.type        = BufferType::Uniform;
        ubo_desc.size        = sizeof(f32) * 16;
        ubo_desc.cpu_visible = true;
        auto ubo             = backend->create_buffer(ubo_desc);
        if (!ubo) {
            MM_ERROR("Renderer::init() - Failed to create UBO");
            destroy_resources();
            return make_unexpected(ubo.error());
        }
        camera_ubo    = *ubo;
        ubo_alignment = 256;

        BufferDesc rounded_params_desc{};
        rounded_params_desc.type        = BufferType::Uniform;
        rounded_params_desc.size        = sizeof(RoundedParams);
        rounded_params_desc.cpu_visible = true;
        auto rounded_params             = backend->create_buffer(rounded_params_desc);
        if (!rounded_params) {
            MM_ERROR("Renderer::init() - Failed to create RoundedParams UBO");
            destroy_resources();
            return make_unexpected(rounded_params.error());
        }
        rounded_params_ubo                = *rounded_params;
        RoundedParams rounded_params_data = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f, 0.0f}};
        (void)backend->update_buffer(rounded_params_ubo, &rounded_params_data, 0, sizeof(rounded_params_data));

        // Glow UBO (same struct, separate buffer for separate pipeline)
        BufferDesc glow_params_desc{};
        glow_params_desc.type        = BufferType::Uniform;
        glow_params_desc.size        = sizeof(RoundedParams);
        glow_params_desc.cpu_visible = true;
        auto glow_params             = backend->create_buffer(glow_params_desc);
        if (!glow_params) {
            MM_ERROR("Renderer::init() - Failed to create RoundedParams glow UBO");
            destroy_resources();
            return make_unexpected(glow_params.error());
        }
        rounded_params_glow_ubo = *glow_params;
        (void)backend->update_buffer(rounded_params_glow_ubo, &rounded_params_data, 0, sizeof(rounded_params_data));

        // Blur UBO
        BufferDesc blur_params_desc{};
        blur_params_desc.type        = BufferType::Uniform;
        blur_params_desc.size        = sizeof(BlurParams);
        blur_params_desc.cpu_visible = true;
        auto blur_params             = backend->create_buffer(blur_params_desc);
        if (!blur_params) {
            MM_ERROR("Renderer::init() - Failed to create BlurParams UBO");
            destroy_resources();
            return make_unexpected(blur_params.error());
        }
        blur_params_ubo = *blur_params;
        // 15-tap Gaussian weights (sigma=2.5, normalized)
        BlurParams blur_params_data = {1.0f, 0.0f, 4.0f,
            {0.132981f, 0.114226f, 0.087775f, 0.059634f, 0.035841f, 0.018954f, 0.008829f},
            {0.0f, 0.0f}};
        (void)backend->update_buffer(blur_params_ubo, &blur_params_data, 0, sizeof(blur_params_data));

        // Color Grade UBO
        BufferDesc cg_params_desc{};
        cg_params_desc.type        = BufferType::Uniform;
        cg_params_desc.size        = sizeof(ColorGrade);
        cg_params_desc.cpu_visible = true;
        auto cg_params             = backend->create_buffer(cg_params_desc);
        if (!cg_params) {
            MM_ERROR("Renderer::init() - Failed to create ColorGrade UBO");
            destroy_resources();
            return make_unexpected(cg_params.error());
        }
        color_grade_ubo = *cg_params;
        // Identity matrix for no hue shift
        ColorGrade cg_params_data = {1.0f, 1.0f, 1.0f,
            {1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 0.0f, 0.0f}};
        (void)backend->update_buffer(color_grade_ubo, &cg_params_data, 0, sizeof(cg_params_data));

        // Lab-port UBOs: one buffer per flush function (see member comment).
        auto create_param_ubo = [&](size_t size, const char *name, BufferHandle &out, const void *init_data) noexcept -> bool {
            BufferDesc d{};
            d.type        = BufferType::Uniform;
            d.size        = static_cast<u32>(size);
            d.cpu_visible = true;
            auto res      = backend->create_buffer(d);
            if (!res) {
                MM_ERROR("Renderer::init() - Failed to create %s UBO", name);
                destroy_resources();
                return false;
            }
            out = *res;
            (void)backend->update_buffer(out, init_data, 0, static_cast<u32>(size));
            return true;
        };
        NormalParams  lab_nd0 = {{0.0f, 0.0f, 1.0f, 1.0f}, {0.25f, 0.5f, 2.0f, 0.0f}};
        CartoonParams lab_ct0 = {{0.0f, 0.0f, 1.0f, 1.0f}, {0.25f, 0.5f, 2.0f, 0.0f}, {3.0f, 0.35f, 0.85f, 0.0f}};
        PlasticParams lab_pl0 = {{0.0f, 0.0f, 1.0f, 1.0f}, {0.25f, 0.5f, 0.0f, 0.0f}, {0.6f, 0.5f, 0.4f, 120.0f}};
        GlowParams    lab_gl0 = {{0.0f, 1.0f, 0.12f, 0.025f}, {0.0f, 0.8f, 0.82f, 0.9f}, {0.6887f, 1.5f, 1.5f, 0.0f}};
        bool lab_ubos_ok = true;
        lab_ubos_ok      = create_param_ubo(sizeof(NormalParams), "NormalParams(derive)", normal_derive_ubo, &lab_nd0) && lab_ubos_ok;
        lab_ubos_ok      = create_param_ubo(sizeof(NormalParams), "NormalParams(map)", normal_map_ubo, &lab_nd0) && lab_ubos_ok;
        lab_ubos_ok      = create_param_ubo(sizeof(CartoonParams), "CartoonParams", cartoon_ubo, &lab_ct0) && lab_ubos_ok;
        lab_ubos_ok      = create_param_ubo(sizeof(PlasticParams), "PlasticParams", plastic_ubo, &lab_pl0) && lab_ubos_ok;
        lab_ubos_ok      = create_param_ubo(sizeof(GlowParams), "GlowParams(pulse)", glow_pulse_ubo, &lab_gl0) && lab_ubos_ok;
        lab_ubos_ok      = create_param_ubo(sizeof(GlowParams), "GlowParams(border)", gold_border_ubo, &lab_gl0) && lab_ubos_ok;
        lab_ubos_ok      = create_param_ubo(sizeof(GlowParams), "GlowParams(stay)", gold_stay_ubo, &lab_gl0) && lab_ubos_ok;
        RingParams ring0 = {{0.5f, 0.14f, mm_math::MM_TWO_PI, 1.0f}};
        for (u32 i = 0; i < 8; ++i) {
            lab_ubos_ok = create_param_ubo(sizeof(RingParams), "RingParams", ring_params_ubos[i], &ring0) && lab_ubos_ok;
        }
        if (!lab_ubos_ok) {
            return make_unexpected(RHIError::OutOfMemory);
        }

        BufferDesc vb_desc{};
        vb_desc.type        = BufferType::Vertex;
        vb_desc.size        = MAX_SPRITE_VERTS * sizeof(SpriteVertex);
        vb_desc.stride      = sizeof(SpriteVertex);
        vb_desc.cpu_visible = true;
        auto vb             = backend->create_buffer(vb_desc);
        if (!vb) {
            MM_ERROR("Renderer::init() - Failed to create Sprite VB");
            destroy_resources();
            return make_unexpected(vb.error());
        }
        sprite_vb = *vb;

        BufferDesc ib_desc{};
        ib_desc.type        = BufferType::Index;
        ib_desc.size        = MAX_SPRITE_IDX * sizeof(u16);
        ib_desc.stride      = sizeof(u16);
        ib_desc.cpu_visible = true;
        auto ib             = backend->create_buffer(ib_desc);
        if (!ib) {
            MM_ERROR("Renderer::init() - Failed to create Sprite IB");
            destroy_resources();
            return make_unexpected(ib.error());
        }
        sprite_ib = *ib;

        // Particle instance buffer — holds packed instance data for instanced draw
        BufferDesc pib_desc{};
        pib_desc.type        = BufferType::Vertex;
        pib_desc.size        = MAX_PARTICLES * 64; // 4 × float4 per particle
        pib_desc.cpu_visible = true;
        auto pib             = backend->create_buffer(pib_desc);
        if (!pib) {
            MM_ERROR("Renderer::init() - Failed to create Particle IB");
            destroy_resources();
            return make_unexpected(pib.error());
        }
        particle_ib = *pib;

        // Particle atlas UBO — identity mapping (tile_size = atlas_size = 1)
        BufferDesc atlas_ubo_desc{};
        atlas_ubo_desc.type        = BufferType::Uniform;
        atlas_ubo_desc.size        = sizeof(f32) * 4;
        atlas_ubo_desc.cpu_visible = true;
        auto atlas_ubo             = backend->create_buffer(atlas_ubo_desc);
        if (!atlas_ubo) {
            MM_ERROR("Renderer::init() - Failed to create Particle Atlas UBO");
            destroy_resources();
            return make_unexpected(atlas_ubo.error());
        }
        particle_atlas_ubo = *atlas_ubo;
        {
            f32 atlas_data[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            (void)backend->update_buffer(particle_atlas_ubo, atlas_data, 0, sizeof(atlas_data));
        }

        // Default 1x1 white texture for sprite rendering
        TextureDesc white_tex_desc{};
        white_tex_desc.type         = TextureType::Tex2D;
        white_tex_desc.format       = PixelFormat::R8G8B8A8_UNORM;
        white_tex_desc.width        = 1;
        white_tex_desc.height       = 1;
        white_tex_desc.mip_levels   = 1;
        white_tex_desc.array_layers = 1;
        auto wt                     = backend->create_texture(white_tex_desc);
        if (!wt) {
            MM_ERROR("Renderer::init() - Failed to create white texture");
            destroy_resources();
            return make_unexpected(wt.error());
        }
        white_tex            = *wt;
        u32 white_pixel = 0xFFFFFFFF;
        (void)backend->update_texture(white_tex, &white_pixel, 0, 0, 1, 1, 0, 0);

        SamplerDesc samp_desc{};
        samp_desc.min_filter     = SamplerFilter::Linear;
        samp_desc.mag_filter     = SamplerFilter::Linear;
        samp_desc.mip_filter     = SamplerFilter::Linear; // card-atlas mip chain (1-level textures ignore it)
        samp_desc.address_u      = SamplerAddress::ClampToEdge;
        samp_desc.address_v      = SamplerAddress::ClampToEdge;
        samp_desc.max_anisotropy = 4.0f; // minified-atlas cleanup (needs mip chain to matter)
        auto samp                = backend->create_sampler(samp_desc);
        if (!samp) {
            MM_ERROR("Renderer::init() - Failed to create default sampler");
            destroy_resources();
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
            f32 y1      = fa.baked_chars[i * 4 + 3] + 1.0f;
            int   row_end = static_cast<int>(y1);
            if (row_end > ascii_next_y) {
                ascii_next_y = row_end;
            }
        }
        stbtt_bakedchar th_cd[MAX_GLYPHS_TH];
        memset(th_cd, 0, sizeof(th_cd));
        int th_start = 0x0E01;
        int th_end   = 0x0E5B;
        if (fa.bake_range(g_sarabun_ttf, 48.0f, 0x0E01, MAX_GLYPHS_TH, ascii_next_y, th_cd) < 0) {
            // Fallback: try to bake each character individually
            for (int cp = th_start; cp <= th_end; ++cp) {
                (void)fa.bake_range(g_sarabun_ttf, 48.0f, cp, 1, ascii_next_y, &th_cd[cp - th_start]);
            }
        }

        // ── Suit symbols (♠ U+2660, ♥ U+2665, ♦ U+2666, ♣ U+2663) ──────────
        // Bake BEFORE texture upload so pixel data reaches GPU
        stbtt_bakedchar sym_cd[MAX_GLYPHS_SYM];
        memset(sym_cd, 0, sizeof(sym_cd));
        int sym_next_y = ascii_next_y;
        if (g_noto_symbols_ttf_len > 0) {
            constexpr int K_SUIT_START = 0x2660;
            constexpr int K_SUIT_COUNT = 8;
            int           ret          = fa.bake_range(g_noto_symbols_ttf, 48.0f, K_SUIT_START, K_SUIT_COUNT, sym_next_y, sym_cd);
            if (ret < 0) {
                for (int cp = K_SUIT_START; cp < K_SUIT_START + K_SUIT_COUNT; ++cp) {
                    int r = fa.bake_range(g_noto_symbols_ttf, 48.0f, cp, 1, sym_next_y, &sym_cd[cp - K_SUIT_START]);
                    if (r > 0) {
                        sym_next_y = r;
                    }
                }
            } else {
                sym_next_y = ret;
            }
            // Bake ⎌ (U+238C, undo symbol) at slot 8 from Noto Sans Symbols (v1)
            if (g_noto_symbols1_ttf_len > 0) {
                (void)fa.bake_range(g_noto_symbols1_ttf, 48.0f, 0x238C, 1, sym_next_y, &sym_cd[8]);
            }
        }

        // ── Create GPU texture (contains ASCII + Thai + suits) ──────────────
        TextureDesc font_tex_desc{};
        font_tex_desc.type         = TextureType::Tex2D;
        font_tex_desc.format       = PixelFormat::R8_UNORM;
        font_tex_desc.width        = FONT_ATLAS_W;
        font_tex_desc.height       = FONT_ATLAS_H;
        font_tex_desc.mip_levels   = 1;
        font_tex_desc.array_layers = 1;
        auto ft                    = backend->create_texture(font_tex_desc);
        if (!ft) {
            destroy_resources();
            return make_unexpected(ft.error());
        }
        font_tex = *ft;
        (void)backend->update_texture(font_tex, fa.pixels, 0, 0, FONT_ATLAS_W, FONT_ATLAS_H, 0, 0);

        SamplerDesc font_samp_desc{};
        font_samp_desc.min_filter     = SamplerFilter::Linear;
        font_samp_desc.mag_filter     = SamplerFilter::Linear;
        font_samp_desc.mip_filter     = SamplerFilter::Linear; // harmless: font atlas has 1 level
        font_samp_desc.address_u      = SamplerAddress::ClampToEdge;
        font_samp_desc.address_v      = SamplerAddress::ClampToEdge;
        font_samp_desc.max_anisotropy = 1.0f; // text stays unfiltered-crisp
        auto fs                       = backend->create_sampler(font_samp_desc);
        if (!fs) {
            destroy_resources();
            return make_unexpected(fs.error());
        }
        font_sampler = *fs;

        default_font.init(font_tex, 48);
        default_font.atlas_w = FONT_ATLAS_W;
        default_font.atlas_h = FONT_ATLAS_H;
        for (int i = 0; i < FONT_NUM_CHARS; ++i) {
            auto  c = static_cast<u8>(FONT_FIRST_CHAR + i);
            auto &g = default_font.glyphs[c];
            f32 u0, v0, u1, v1, gw, gh;
            f32 bx = 0, by = 0;
            fa.get_quad(c, bx, by, u0, v0, u1, v1, gw, gh);
            g.u         = static_cast<u16>(u0 * FONT_ATLAS_W);
            g.v         = static_cast<u16>(v0 * FONT_ATLAS_H);
            g.w         = static_cast<u16>(gw);
            g.h         = static_cast<u16>(gh);
            g.bearing_x = static_cast<i8>(bx);
            g.bearing_y = static_cast<i8>(by);
            g.advance   = static_cast<u8>(fa.xadvance[i]);
        }
        for (int i = 0; i < MAX_GLYPHS_TH; ++i) {
            auto &g     = default_font.glyphs_th[i];
            auto &bc    = th_cd[i];
            g.u         = static_cast<u16>(bc.x0);
            g.v         = static_cast<u16>(bc.y0);
            g.w         = static_cast<u16>(bc.x1 - bc.x0);
            g.h         = static_cast<u16>(bc.y1 - bc.y0);
            g.bearing_x = static_cast<i8>(bc.xoff);
            g.bearing_y = static_cast<i8>(bc.yoff);
            g.advance   = static_cast<u8>(bc.xadvance);
        }
        for (int i = 0; i < MAX_GLYPHS_SYM; ++i) {
            auto &g     = default_font.glyphs_sym[i];
            auto &bc    = sym_cd[i];
            g.u         = static_cast<u16>(bc.x0);
            g.v         = static_cast<u16>(bc.y0);
            g.w         = static_cast<u16>(bc.x1 - bc.x0);
            g.h         = static_cast<u16>(bc.y1 - bc.y0);
            g.bearing_x = static_cast<i8>(bc.xoff);
            g.bearing_y = static_cast<i8>(bc.yoff);
            g.advance   = static_cast<u8>(bc.xadvance);
        }
        //        int thai_valid = 0, thai_invalid = 0;
        //        for (int i = 0; i < MAX_GLYPHS_TH; ++i) {
        //            const GlyphInfo* gi = &default_font.glyphs_th[i];
        //            bool ok = (gi->w > 0 && gi->h > 0 &&
        //                       gi->u < default_font.atlas_w && gi->v < default_font.atlas_h &&
        //                       gi->u + gi->w <= default_font.atlas_w &&
        //                       gi->v + gi->h <= default_font.atlas_h);
        //            if (ok) {
        //                ++thai_valid;
        //            } else {
        //                ++thai_invalid;
        //                u32 cp = 0x0E01 + i;
        //                char buf[256];
        //                int n = snprintf(buf, sizeof(buf),
        //                                 "[Font] INVALID Thai glyph U+%04X u=%u v=%u w=%u h=%u atlas=%ux%u\n",
        //                                 cp, gi->u, gi->v, gi->w, gi->h, default_font.atlas_w, default_font.atlas_h);
        //                write(2, buf, (size_t)n);
        //            }
        //        }
        //        {
        //            char buf[128];
        //            int n = snprintf(buf, sizeof(buf), "[Font] Thai glyphs valid=%d invalid=%d\n", thai_valid, thai_invalid);
        //            write(2, buf, (size_t)n);
        //        }

        BufferDesc text_vb_desc{};
        text_vb_desc.type        = BufferType::Vertex;
        text_vb_desc.size        = TEXT_MAX_GLYPH * 4 * sizeof(SpriteVertex);
        text_vb_desc.stride      = sizeof(SpriteVertex);
        text_vb_desc.cpu_visible = true;
        auto tvb                 = backend->create_buffer(text_vb_desc);
        if (!tvb) {
            destroy_resources();
            return make_unexpected(tvb.error());
        }
        text_vb = *tvb;

        BufferDesc text_ib_desc{};
        text_ib_desc.type        = BufferType::Index;
        text_ib_desc.size        = TEXT_MAX_GLYPH * 6 * sizeof(u16);
        text_ib_desc.stride      = sizeof(u16);
        text_ib_desc.cpu_visible = true;
        auto tib                 = backend->create_buffer(text_ib_desc);
        if (!tib) {
            destroy_resources();
            return make_unexpected(tib.error());
        }
        text_ib = *tib;

        ortho(0.0f, static_cast<f32>(screen_w), static_cast<f32>(screen_h), 0.0f, -1.0f, 1.0f);

        auto sp = create_default_pipelines();
        if (!sp) {
            {
                char buf[256];
                int  n = snprintf(buf, sizeof(buf), "create_default_pipelines FAILED: err=%d\n", (int)sp.error());
                write(2, buf, (size_t)n);
            }
            destroy_resources();
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

    void ortho(f32 left, f32 right, f32 bottom, f32 top, f32 near_, f32 far_) noexcept {
        mm_math::mat4 m = mm_math::mat4::ortho(left, right, bottom, top, near_, far_);
        m.store_column_major(view_proj.data());
    }

    void                     upload_camera() noexcept { (void)backend->update_buffer(camera_ubo, view_proj.data(), 0, sizeof(mm_math::mat4)); }
    void                     advance_time(f32 dt) noexcept { render_time += dt; }
    void                     set_time(f32 t) noexcept { render_time = t; }

    Expected<void, RHIError> begin_frame() noexcept {
        flush_text();
        sprite_vertex_count     = 0;
        sprite_index_count      = 0;
        particle_instance_count = 0;
        temp_arena.reset();
        frame_arena.swap();
        graph.init(frame_arena.current().template alloc_array<Command>(MAX_COMMANDS), MAX_COMMANDS);
        ring_param_slot       = 0;
        ring_param_overflow   = false;

        upload_camera();
        return {};
    }

    // Execute the render graph — call between begin_pass and end_pass
    void submit() noexcept {
        ZoneScoped;
        graph.sort();
#if defined(ENGINE_ENABLE_ASSERT)
        bool has_pipeline = false;
        // The render graph is a SINGLE pass by construction (begin_pass sorts to
        // SortKey::min(), end_pass to SortKey::max(), so everything in between is
        // one flat command list). Two passes in one submit would abort the Metal
        // encoder and corrupt state on Vulkan, so make that a compile-out-of-debug
        // invariant rather than a convention the host is asked to respect.
        u32 begin_passes = 0;
        u32 end_passes   = 0;
        bool past_end    = false;
        for (u32 i = 0; i < graph.command_count; ++i) {
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
            case CmdType::BeginPass:
                assert(begin_passes == 0 && end_passes == 0 && "Multiple BeginPass in one submit");
                ++begin_passes;
                break;
            case CmdType::EndPass:
                assert(end_passes == 0 && !past_end && "Multiple EndPass in one submit");
                ++end_passes;
                past_end = true;
                break;
            default:
                break;
            }
        }
        assert(begin_passes == end_passes && "Mismatched BeginPass/EndPass - cannot submit");
#endif

        // State tracking for redundant-bind suppression (Metal validation)
        PipelineHandle last_pipeline{};
        BufferHandle   last_vb{};
        u32       last_vb_off = UINT32_MAX;
        u32       last_vb_str = UINT32_MAX;
        BufferHandle   last_ib{};
        IndexType      last_ib_type = static_cast<IndexType>(0xFF);
        u32       last_ib_off  = UINT32_MAX;
        TextureHandle  last_tex[4]  = {};
        SamplerHandle  last_sam[4]  = {};
        BufferHandle   last_ubo[4]  = {};
        i16        last_sx = -1, last_sy = -1;
        u16       last_sw = 0, last_sh = 0;

        for (u32 i = 0; i < graph.command_count; ++i) {
            auto &cmd = graph.commands[i];
            switch (cmd.type) {
            case CmdType::BindPipeline: {
                auto h = cmd.data.bind_pipeline.pipeline;
                // MM_LOG("SUBMIT: BindPipeline handle.id=%u gen=%u", h.handle.id, h.handle.gen);
                if (h.handle.id == last_pipeline.handle.id && h.handle.gen == last_pipeline.handle.gen) {
                    break;
                }
                last_pipeline = h;
#if defined(ENGINE_ENABLE_ASSERT)
                auto *pl = backend->pipelines.get(h.handle);
                if (!pl) {
                    fprintf(stderr, "BINDPIPELINE FAIL: handle.id=%u gen=%u\n", h.handle.id, h.handle.gen);
                } else if (!pl->pipeline) {
                    fprintf(stderr, "BINDPIPELINE NIL: handle.id=%u gen=%u pl=%p\n", h.handle.id, h.handle.gen, (void *)pl);
                }
#endif
                (void)backend->bind_pipeline(h);
                break;
            }
            case CmdType::BindVertexBuffer: {
                auto    &vb      = cmd.data.bind_vb;
                u32 binding = vb.binding;
                if (vb.buffer.handle.id == last_vb.handle.id && vb.buffer.handle.gen == last_vb.handle.gen && vb.offset == last_vb_off &&
                    vb.stride == last_vb_str) {
                    break;
                }
                last_vb     = vb.buffer;
                last_vb_off = (u32)vb.offset;
                last_vb_str = (u32)vb.stride;
                (void)backend->bind_vertex_buffers(&vb.buffer, 1, &vb.offset, &vb.stride, &binding);
                break;
            }
            case CmdType::BindIndexBuffer: {
                auto &ib = cmd.data.bind_ib;
                if (ib.buffer.handle.id == last_ib.handle.id && ib.buffer.handle.gen == last_ib.handle.gen && ib.offset == last_ib_off &&
                    ib.type == last_ib_type) {
                    break;
                }
                last_ib      = ib.buffer;
                last_ib_type = ib.type;
                last_ib_off  = (u32)ib.offset;
                (void)backend->bind_index_buffer(ib.buffer, ib.type, ib.offset);
                break;
            }
            case CmdType::BindFragmentTexture: {
                auto &ft = cmd.data.bind_frag_tex;
                if (ft.index < 4 && ft.texture.handle.id == last_tex[ft.index].handle.id && ft.texture.handle.gen == last_tex[ft.index].handle.gen) {
                    break;
                }
                if (ft.index < 4) {
                    last_tex[ft.index] = ft.texture;
                }
                (void)backend->bind_fragment_texture(ft.texture, ft.index);
                break;
            }
            case CmdType::BindFragmentSampler: {
                auto &fs = cmd.data.bind_frag_samp;
                if (fs.index < 4 && fs.sampler.handle.id == last_sam[fs.index].handle.id && fs.sampler.handle.gen == last_sam[fs.index].handle.gen) {
                    break;
                }
                if (fs.index < 4) {
                    last_sam[fs.index] = fs.sampler;
                }
                (void)backend->bind_fragment_sampler(fs.sampler, fs.index);
                break;
            }
            case CmdType::BindUniformBuffer: {
                auto &ubo = cmd.data.bind_ubo;
                if (ubo.binding < 4 && ubo.buffer.handle.id == last_ubo[ubo.binding].handle.id && ubo.buffer.handle.gen == last_ubo[ubo.binding].handle.gen) {
                    break;
                }
                if (ubo.binding < 4) {
                    last_ubo[ubo.binding] = ubo.buffer;
                }
                (void)backend->bind_uniform_buffer(ubo.buffer, ubo.binding);
                break;
            }
            case CmdType::Draw: {
                auto &d = cmd.data.draw;
                (void)backend->draw(d.vertex_count, d.instance_count, d.first_vertex, d.first_instance);
                break;
            }
            case CmdType::DrawIndexed: {
                auto &di = cmd.data.draw_indexed;
                (void)backend->draw_indexed(di.index_count, di.instance_count, di.first_index, di.vertex_offset);
                break;
            }
            case CmdType::BeginPass: {
                // Reset redundant-bind tracking at pass boundary
                last_pipeline = {};
                last_vb       = {};
                last_vb_off   = UINT32_MAX;
                last_vb_str   = UINT32_MAX;
                last_ib       = {};
                last_ib_type  = static_cast<IndexType>(0xFF);
                last_ib_off   = UINT32_MAX;
                for (auto &t : last_tex) {
                    t = {};
                }
                for (auto &s : last_sam) {
                    s = {};
                }
                for (auto &u : last_ubo) {
                    u = {};
                }
                last_sx = -1;
                last_sy = -1;
                last_sw = 0;
                last_sh = 0;
                (void)backend->begin_pass(cmd.data.begin_pass.pass);
                break;
            }
            case CmdType::EndPass:
                (void)backend->end_pass();
                break;
            case CmdType::SetScissor: {
                auto &s = cmd.data.set_scissor;
                if (s.x == last_sx && s.y == last_sy && s.w == last_sw && s.h == last_sh) {
                    break;
                }
                last_sx = s.x;
                last_sy = s.y;
                last_sw = s.w;
                last_sh = s.h;
                (void)backend->set_scissor(s.x, s.y, s.w, s.h);
                break;
            }
            default:
                break;
            }
        }

        FrameMark;
    }

    void end_frame() noexcept {
        // backend->end_frame();
        // Cleared HERE, not in begin_frame: text_dropped_counts the frame that was
        // just submitted, and a host can only report it on a frame that still has
        // room to draw the report. The frame that overflowed is precisely the one
        // whose tail - including a report drawn after the overflow - never appears.
        text_dropped_calls = 0;
    }

  private:
    // Internal: vertex generation + draw, parameterized by pipeline/texture/sampler
    // Groups sprites by per-sprite tex_id, resolving registered textures from the batch.
    void flush_sprites_impl(SpriteBatch &batch, PipelineHandle pipeline, TextureHandle fallback_tex, SamplerHandle sampler,
                            BufferHandle params_ubo = {}, SortKey key = SortKey{0, 0, 0, 1.0f}) noexcept {
        ZoneScoped;
        if (batch.count == 0) {
            return;
        }

        cull_sprites(batch);

        // Single pass: collect unique tex_ids AND count sprites per group.
        // A batch can reference at most MAX_REGISTERED_TEX distinct
        // textures; a sprite whose texture is the (MAX+1)th is dropped
        // and counted - without the counter it would vanish silently, the
        // same silence class as SpriteBatch::dropped.
        struct TexGroup {
            u16 tex_id;
            u16 count;
        };
        TexGroup groups[SpriteBatch::MAX_REGISTERED_TEX];
        u8  group_count = 0;
        u32 dropped_sprites = 0;
        for (u16 i = 0; i < batch.count; ++i) {
            if (!batch.active[i]) {
                continue;
            }
            u16 tid = batch.tex_id[i];
            TexGroup *g = nullptr;
            for (u8 j = 0; j < group_count; ++j) {
                if (groups[j].tex_id == tid) {
                    g = &groups[j];
                    break;
                }
            }
            if (g == nullptr) {
                if (group_count < SpriteBatch::MAX_REGISTERED_TEX) {
                    g = &groups[group_count++];
                    g->tex_id = tid;
                    g->count  = 0;
                } else {
                    ++dropped_sprites;
                    continue;
                }
            }
            ++g->count;
        }

        if (group_count == 0) {
            return;
        }
        if (dropped_sprites > 0) {
            MM_ERROR("Renderer::flush_sprites_impl - %u sprites dropped: batch references more than %u distinct textures",
                     dropped_sprites, (u32)SpriteBatch::MAX_REGISTERED_TEX);
        }

        // Per-texture draw calls. Staging is rewound after each group:
        // update_buffer copies verts/indices into the GPU buffer, so the
        // arena copy is dead the moment the draw is recorded. Without the
        // rewind, a frame with many texture groups accumulates staging and
        // can exhaust temp_arena.
        for (u8 ti = 0; ti < group_count; ++ti) {
            u16 tid         = groups[ti].tex_id;
            u16 sprite_count = groups[ti].count;

            // Resolve texture handle
            TextureHandle tex = fallback_tex;
            if (tid != 0) {
                TextureHandle rt = batch.lookup_texture(tid);
                if (rt.handle.id != 0) {
                    tex = rt;
                }
            }

            auto group_marker = temp_arena.marker();
            auto *verts       = temp_arena.alloc_array<SpriteVertex>(sprite_count * 4);
            auto *indices     = temp_arena.alloc_array<u16>(sprite_count * 6);
            if (!verts || !indices) {
                temp_arena.rewind(group_marker);
                continue;
            }

            u32 vert_count = 0;
            u32 idx_count  = 0;

            for (u16 i = 0; i < batch.count; ++i) {
                if (!batch.active[i]) {
                    continue;
                }
                if (batch.tex_id[i] != tid) {
                    continue;
                }

                f32    sx         = batch.world_x[i];
                f32    sy         = batch.world_y[i];
                f32    scx        = batch.scale_x[i] * 0.5f;
                f32    scy        = batch.scale_y[i] * 0.5f;
                f32    rot        = batch.rotation[i];
                f32    bw         = batch.border_w[i];
                u32 col        = batch.color[i];
                u32 border_col = batch.border_color[i];

                f32    u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                if (batch.atlas_w[i] > 0 && batch.atlas_h[i] > 0 && batch.atlas_tex_w > 0 && batch.atlas_tex_h > 0) {
                    u0 = static_cast<f32>(batch.atlas_x[i]) / batch.atlas_tex_w;
                    v0 = static_cast<f32>(batch.atlas_y[i]) / batch.atlas_tex_h;
                    u1 = static_cast<f32>(batch.atlas_x[i] + batch.atlas_w[i]) / batch.atlas_tex_w;
                    v1 = static_cast<f32>(batch.atlas_y[i] + batch.atlas_h[i]) / batch.atlas_tex_h;
                }

                f32    c         = __builtin_cosf(rot);
                f32    s         = __builtin_sinf(rot);

                f32    cx[4]     = {-scx, scx, -scx, scx};
                f32    cy[4]     = {-scy, -scy, scy, scy};
                f32    uv_u[4]   = {u0, u1, u0, u1};
                f32    uv_v[4]   = {v0, v0, v1, v1};

                u32 base      = vert_count;
                f32    cr        = batch.corner_r[i];
                f32    l_scale   = (scx > 0.0f) ? 0.5f / scx : 0.0f;
                f32    l_scale_y = (scy > 0.0f) ? 0.5f / scy : 0.0f;
                for (int j = 0; j < 4; ++j) {
                    f32 rx       = cx[j] * c - cy[j] * s;
                    f32 ry       = cx[j] * s + cy[j] * c;
                    auto &v        = verts[vert_count++];
                    v.x            = SpriteBatch::float_to_f16(sx + rx);
                    v.y            = SpriteBatch::float_to_f16(sy + ry);
                    v.u            = SpriteBatch::float_to_f16(uv_u[j]);
                    v.v            = SpriteBatch::float_to_f16(uv_v[j]);
                    v.color        = col;
                    v.local_x      = SpriteBatch::float_to_f16(cx[j] * l_scale);
                    v.local_y      = SpriteBatch::float_to_f16(cy[j] * l_scale_y);
                    v.radius       = SpriteBatch::float_to_f16(cr);
                    v.border_w     = SpriteBatch::float_to_f16(bw);
                    v.border_color = border_col;
                }

                indices[idx_count++] = static_cast<u16>(base);
                indices[idx_count++] = static_cast<u16>(base + 1);
                indices[idx_count++] = static_cast<u16>(base + 2);
                indices[idx_count++] = static_cast<u16>(base + 1);
                indices[idx_count++] = static_cast<u16>(base + 3);
                indices[idx_count++] = static_cast<u16>(base + 2);
            }

            if (vert_count == 0) {
                temp_arena.rewind(group_marker);
                continue;
            }

            u32 vb_byte_offset = sprite_vertex_count * sizeof(SpriteVertex);
            u32 ib_byte_offset = sprite_index_count * sizeof(u16);

            (void)backend->update_buffer(sprite_vb, verts, vb_byte_offset, vert_count * sizeof(SpriteVertex));
            (void)backend->update_buffer(sprite_ib, indices, ib_byte_offset, idx_count * sizeof(u16));

            // key defaults to the sprite layer; callers (UI overlay popups)
            // may lift a batch into the text layer so it covers text below.
            // Invariant state: bind once at offset 0, use draw params for per-group offset
            graph.bind_pipeline(pipeline, key);
            graph.bind_vertex_buffer(sprite_vb, 0, 0, sizeof(SpriteVertex), key);
            graph.bind_uniform_buffer(camera_ubo, 0, key);
            graph.bind_index_buffer(sprite_ib, IndexType::Uint16, 0, key);
            graph.bind_fragment_sampler(sampler, 0, key);
            graph.bind_fragment_texture(tex, 0, key);
            if (params_ubo.is_valid()) {
                graph.bind_uniform_buffer(params_ubo, 1, key);
            }
            graph.draw_indexed(key, idx_count, 1, sprite_index_count, static_cast<i32>(sprite_vertex_count));

            sprite_vertex_count += vert_count;
            sprite_index_count  += idx_count;

            temp_arena.rewind(group_marker);
        }
    }

  public:
    // Text layer for draw_text(). The render graph is single-pass: begin_pass
    // sorts to SortKey::min() and end_pass to max(), so ALL commands share one
    // pass and graph.sort() orders them purely by key. draw_text used to
    // hardcode key {1,0,0,1.0f}, which put every string in the frame in one
    // bucket that always lands after every sprite - so a scene's labels
    // painted on top of an overlay panel no matter the submission order.
    //
    // Raise this around overlay rendering (0 = scene, 1 = overlay) so overlay
    // text sorts after scene text while staying in submission order within
    // its own layer. Overlay SPRITES need no change: they share the default
    // key with scene text and stable submission order already puts them on
    // top.
    u8       text_layer = 0;

    // Default flush with sprite pipeline + white texture
    void flush_sprites(SpriteBatch &batch) noexcept { flush_sprites_impl(batch, sprite_pipeline, white_tex, default_sampler); }

    // Flush with rounded sprite pipeline
    void flush_rounded_sprites(SpriteBatch &batch) noexcept {
        if (rounded_params_ubo) {
            RoundedParams p = {render_time, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f, 0.0f}};
            (void)backend->update_buffer(rounded_params_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, rounded_sprite_pipeline, white_tex, default_sampler, rounded_params_ubo);
    }

    // Rounded (SDF) flush with an explicit texture: keeps the style corner
    // radius + border/ring (the plain sprite shader ignores both) while
    // sampling a real texture instead of white_tex. Sprites added with
    // tex == 0 take it as their fallback; per-sprite tex_ids still win.
    // ─── Gradient fill texture ────────────────────────────────────
    // A w x h linear gradient (RGBA8), created ONCE and handed to
    // WidgetStyle::bg_tex. That is the whole implementation: the rounded SDF
    // path samples a style texture as the widget's FILL, so corner radius,
    // per-side borders and edge AA all keep working — no new shader, no new
    // pipeline, and the 896KB SpriteBatch cap untouched.
    //
    // WHY A TEXTURE AND NOT A UBO (measured, not assumed): draw commands are
    // RECORDED and executed at submit(), so a uniform buffer's CONTENT is
    // global to the frame — every gradient draw would read whatever the last
    // write left there. That is the same reason per-call parameter UBOs cannot be
    // overwritten mid-frame. A texture bind, by
    // contrast, is recorded per draw and the pixels are already resident, so
    // N distinct gradients are N correct draws.
    // `vertical` = ramp top->bottom (the common case); horizontal is
    // left->right; `angular` = along the ring's sweep (see
    // make_sweep_gradient_texture). Small sizes are fine: the sampler is
    // linear and the gradient is axis-aligned, so there is nothing to alias.
    // Where a point sits along the ring's sweep, 0..1. THE convention both the
    // ring fragment shader and make_sweep_gradient_texture() use: local units
    // (-0.5..+0.5, y DOWN), 0 at 12 o'clock, growing CLOCKWISE. Pure so a test
    // can pin the convention instead of trusting that two files agree.
    static f32 sweep_t(f32 local_x, f32 local_y) noexcept {
        f32 ang = __builtin_atan2f(local_x, -local_y);
        if (ang < 0.0f) {
            ang += mm_math::MM_TWO_PI;
        }
        return ang / mm_math::MM_TWO_PI;
    }

    // Angular ("sweep") gradient for the analytic ring: an n x n texture whose
    // colour follows the RING ANGLE rather than the quad's bounding box, so the
    // ramp tracks the sweep head. The ring shader already samples v_uv across
    // its quad, so this needs no shader change at all - only a texture whose
    // pixels are laid out in the same angle space the shader sweeps.
    TextureHandle make_sweep_gradient_texture(u16 n, u32 c_start, u32 c_tip) noexcept {
        if (n < 4) {
            n = 4;
        }
        return make_gradient_texture(n, n, c_start, c_tip, false, /*angular=*/true);
    }

    TextureHandle make_gradient_texture(u16 w, u16 h, u32 c0, u32 c1, bool vertical,
                                          bool angular = false) noexcept {
        if (w == 0 || h == 0) {
            return TextureHandle::invalid();
        }
        TextureDesc td{};
        td.type         = TextureType::Tex2D;
        td.format       = PixelFormat::R8G8B8A8_UNORM;
        td.width        = w;
        td.height       = h;
        td.mip_levels   = 1; // a smooth axis-aligned ramp has nothing to mip
        td.array_layers = 1;
        auto tr = backend->create_texture(td);
        if (!tr) {
            return TextureHandle::invalid();
        }
        const TextureHandle tex = *tr;
        // Packed colours are 0xAARRGGBB; interpolate per channel in f32 so
        // the midpoint does not round to a band edge.
        const f32 r0 = static_cast<f32>((c0 >> 16) & 0xFFu), g0 = static_cast<f32>((c0 >> 8) & 0xFFu),
                    b0 = static_cast<f32>(c0 & 0xFFu), a0 = static_cast<f32>((c0 >> 24) & 0xFFu);
        const f32 r1 = static_cast<f32>((c1 >> 16) & 0xFFu), g1 = static_cast<f32>((c1 >> 8) & 0xFFu),
                    b1 = static_cast<f32>(c1 & 0xFFu), a1 = static_cast<f32>((c1 >> 24) & 0xFFu);
        const u32 total = static_cast<u32>(w) * static_cast<u32>(h);
        // 256 bytes of stack would be too small for a 8x64 ramp; a fixed cap
        // keeps this allocation-free (the codebase is no-heap on this path).
        static constexpr u32 K_MAX_PIXELS = 4096;
        if (total > K_MAX_PIXELS) {
            return TextureHandle::invalid();
        }
        u32 pixels[K_MAX_PIXELS];
        for (u32 y = 0; y < h; ++y) {
            for (u32 x = 0; x < w; ++x) {
                f32 t = 0.0f;
                if (angular) {
                    // Pixel -> local -> angle, matching sweep_t() and the ring
                    // shader exactly. v is the quad's vertical UV, so the same
                    // formula has to be used here or the ramp runs backwards
                    // around the circle.
                    const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(w);
                    const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(h);
                    t             = sweep_t(u - 0.5f, v - 0.5f);
                } else {
                    t = (vertical ? static_cast<f32>(y) / static_cast<f32>(h - 1 < 1 ? 1 : h - 1)
                                  : static_cast<f32>(x) / static_cast<f32>(w - 1 < 1 ? 1 : w - 1));
                }
                const f32 r = r0 + (r1 - r0) * t, g = g0 + (g1 - g0) * t;
                const f32 b = b0 + (b1 - b0) * t, a = a0 + (a1 - a0) * t;
                // Texture bytes are RGBA with R FIRST (byte0 = R), which is the
                // OPPOSITE of the packed 0xAARRGGBB convention: byte0 is the
                // LSB, so R goes in the low byte. Writing the packed value
                // straight through swaps R and B - a "blue" gradient renders
                // salmon. (The old to/from_u32_bgra helpers were removed for
                // being exactly this trap.)
                pixels[y * w + x] = (static_cast<u32>(a + 0.5f) << 24) | (static_cast<u32>(b + 0.5f) << 16) |
                                    (static_cast<u32>(g + 0.5f) << 8) | static_cast<u32>(r + 0.5f);
            }
        }
        (void)backend->update_texture(tex, pixels, 0, 0, w, h, 0, 0);
        return tex;
    }

    void flush_rounded_sprites(SpriteBatch &batch, TextureHandle tex, SamplerHandle samp) noexcept {
        if (rounded_params_ubo) {
            RoundedParams p = {render_time, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f, 0.0f}};
            (void)backend->update_buffer(rounded_params_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, rounded_sprite_pipeline, tex, samp, rounded_params_ubo);
    }

    void flush_rounded_sprites_glow(SpriteBatch &batch, f32 intensity, f32 glow_w, f32 pulse_speed_hz, u32 glow_color) noexcept {
        if (rounded_params_glow_ubo) {
            mm_math::color gc = mm_math::color::from_u32_argb(glow_color); // 0xAARRGGBB
            f32         pulse_freq = pulse_speed_hz * mm_math::MM_TWO_PI;
            RoundedParams p          = {render_time, intensity, glow_w, pulse_freq, 1.0f, 0.0f, 0.0f, 0.0f, {gc.r, gc.g, gc.b, gc.a}};
            (void)backend->update_buffer(rounded_params_glow_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, rounded_sprite_glow_pipeline, white_tex, default_sampler, rounded_params_glow_ubo);
    }

    // Flush one analytic progress ring (mm_ui_wprogress): the batch holds
    // the bounding quad(s); the fragment computes band + sweep caps.
    // `tex` is the fallback texture for the ring's own fill; the ring fragment
    // samples it (and multiplies by v_color), so a texture here becomes the
    // ring's paint - which is how the sweep gradient works with no shader
    // change. Invalid = the default white, i.e. a flat theme-coloured ring.
    void flush_ring(SpriteBatch &batch, f32 outer_r, f32 thickness, f32 sweep_rad, TextureHandle tex = {}) noexcept {
        // sweep_rad is deliberately NOT written: the caller passed it in the
        // quad's radius (see RingParams). Kept in the signature so the call
        // site reads as "this ring sweeps that far".
        (void)sweep_rad;
        // A fresh slot per distinct parameter set within a frame. The draws
        // execute at submit(), and a UBO's content is global to the frame - so
        // sharing one buffer would let two rings with different thickness draw
        // with the LAST write (the exact collision that forced the sweep field
        // out of this struct; this covers what is left of it).
        u32 slot = ring_param_slot;
        if (slot < 8) {
            ++ring_param_slot;
        } else if (!ring_param_overflow) {
            ring_param_overflow = true;
            slot                = 7;
            MM_ERROR("Renderer::flush_ring - more than 8 distinct RingParams in one frame; "
                     "later rings collapsing on the last slot (the UBO-is-frame-global limitation returns)");
        } else {
            slot = 7;
        }
        if (ring_params_ubos[slot]) {
            RingParams p = {{outer_r, thickness, 0.0f, 1.0f}};
            (void)backend->update_buffer(ring_params_ubos[slot], &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, ring_pipeline, tex.is_valid() ? tex : white_tex, default_sampler, ring_params_ubos[slot]);
    }

    // ─── Lab-port flushes (additive; existing flushes untouched) ────
    // Second texture (normal map) binds at logical slot 2, which lands on
    // [[texture(1)]] (Metal backend quirk: index 1 aliases slot 0, so slot 2
    // is the only safe second slot). Same SortKey as the impl below, so
    // these binds execute first and persist (the impl never touches slot 2).
    void flush_sprites_2tex(SpriteBatch &batch, PipelineHandle pipeline, TextureHandle fallback_tex, TextureHandle second_tex,
                            SamplerHandle sampler, BufferHandle params_ubo) noexcept {
        SortKey key{0, 0, 0, 1.0f};
        graph.bind_fragment_texture(second_tex, 2, key);
        graph.bind_fragment_sampler(sampler, 2, key);
        flush_sprites_impl(batch, pipeline, fallback_tex, sampler, params_ubo);
    }

    // Sobel normal from albedo + Blinn-Phong (lab D6).
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_normal_derive(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler, f32 lx, f32 ly, f32 lz, f32 intensity,
                             f32 ambient, f32 spec, f32 hscale) noexcept {
        if (normal_derive_ubo) {
            NormalParams p = {{lx, ly, lz, intensity}, {ambient, spec, hscale, 0.0f}};
            (void)backend->update_buffer(normal_derive_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, normal_derive_pipeline, texture, sampler, normal_derive_ubo);
    }

    // Normal-mapped lighting (lab D7/D8/D9). Metal-ready; Vulkan-deferred
    // (backend binds a single fragment texture per pipeline today).
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_normal_map(SpriteBatch &batch, TextureHandle albedo_tex, TextureHandle normal_tex, SamplerHandle sampler, f32 lx, f32 ly,
                          f32 lz, f32 intensity, f32 ambient, f32 spec) noexcept {
        if (normal_map_ubo) {
            NormalParams p = {{lx, ly, lz, intensity}, {ambient, spec, 0.0f, 0.0f}};
            (void)backend->update_buffer(normal_map_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_2tex(batch, normal_map_pipeline, albedo_tex, normal_tex, sampler, normal_map_ubo);
    }

    // Toon bands + ink edges (lab C).
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_cartoon(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler, f32 lx, f32 ly, f32 lz, f32 intensity,
                       f32 ambient, f32 spec, f32 hscale, f32 bands, f32 ink_thresh, f32 ink_strength) noexcept {
        if (cartoon_ubo) {
            CartoonParams p = {{lx, ly, lz, intensity}, {ambient, spec, hscale, 0.0f}, {bands, ink_thresh, ink_strength, 0.0f}};
            (void)backend->update_buffer(cartoon_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, cartoon_pipeline, texture, sampler, cartoon_ubo);
    }

    // Glossy plastic (lab P). Same Vulkan single-texture limitation as normal_map.
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_plastic(SpriteBatch &batch, TextureHandle albedo_tex, TextureHandle normal_tex, SamplerHandle sampler, f32 lx, f32 ly, f32 lz,
                       f32 intensity, f32 ambient, f32 spec, f32 clearcoat, f32 fresnel, f32 wrap, f32 shine) noexcept {
        if (plastic_ubo) {
            PlasticParams p = {{lx, ly, lz, intensity}, {ambient, spec, 0.0f, 0.0f}, {clearcoat, fresnel, wrap, shine}};
            (void)backend->update_buffer(plastic_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_2tex(batch, plastic_pipeline, albedo_tex, normal_tex, sampler, plastic_ubo);
    }

    // Expanding + fading ring (lab G). Host draws quads k× larger than the
    // card so the ring has margin to travel in; time comes from render_time.
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_glow_pulse(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler, f32 duration, f32 expand, f32 ring_w,
                          u32 glow_color, f32 aspect, f32 intensity, f32 quad_k) noexcept {
        if (glow_pulse_ubo) {
            mm_math::color gc = mm_math::color::from_u32_argb(glow_color);
            GlowParams p = {{render_time, duration, expand, ring_w}, {gc.r, gc.g, gc.b, gc.a}, {aspect, intensity, quad_k, 0.0f}};
            (void)backend->update_buffer(glow_pulse_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, glow_pulse_pipeline, texture, sampler, glow_pulse_ubo);
    }

    // Static band + breathe (lab Y). timing.y = pulse speed in Hz.
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_gold_border(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler, f32 speed_hz, f32 band_w, u32 glow_color,
                           f32 aspect, f32 intensity, f32 quad_k) noexcept {
        if (gold_border_ubo) {
            mm_math::color gc = mm_math::color::from_u32_argb(glow_color);
            GlowParams p = {{render_time, speed_hz, band_w, 0.0f}, {gc.r, gc.g, gc.b, gc.a}, {aspect, intensity, quad_k, 0.0f}};
            (void)backend->update_buffer(gold_border_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, gold_border_pipeline, texture, sampler, gold_border_ubo);
    }

    // Persistent base + gentle pulse (lab S). Same params as gold_border.
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_gold_stay(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler, f32 speed_hz, f32 band_w, u32 glow_color,
                         f32 aspect, f32 intensity, f32 quad_k) noexcept {
        if (gold_stay_ubo) {
            mm_math::color gc = mm_math::color::from_u32_argb(glow_color);
            GlowParams p = {{render_time, speed_hz, band_w, 0.0f}, {gc.r, gc.g, gc.b, gc.a}, {aspect, intensity, quad_k, 0.0f}};
            (void)backend->update_buffer(gold_stay_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, gold_stay_pipeline, texture, sampler, gold_stay_ubo);
    }

    // Flush with a specific texture (e.g. from a sprite atlas)
    void flush_sprites(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler,
                       SortKey key = SortKey{0, 0, 0, 1.0f}) noexcept {
        flush_sprites_impl(batch, sprite_pipeline, texture, sampler, {}, key);
    }

    // Flush with a Material (pipeline + texture + sampler)
    void flush_sprites(SpriteBatch &batch, const Material &mat) noexcept { flush_sprites_impl(batch, mat.pipeline, mat.texture, mat.sampler); }

    // Flush with a Technique (uses the first pass)
    void flush_sprites(SpriteBatch &batch, const Technique &tech) noexcept { flush_sprites(batch, tech.current_pass()); }

    // Flush blur (single-pass 15-tap Gaussian; for two-pass use flush_blur_horizontal + flush_blur_vertical)
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_blur(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler, bool horizontal = true, f32 radius = 4.0f) noexcept {
        if (blur_params_ubo) {
            BlurParams p = {horizontal ? 1.0f : 0.0f, horizontal ? 0.0f : 1.0f, radius,
                {0.132981f, 0.114226f, 0.087775f, 0.059634f, 0.035841f, 0.018954f, 0.008829f},
                {0.0f, 0.0f}};
            (void)backend->update_buffer(blur_params_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, blur_pipeline, texture, sampler, blur_params_ubo);
    }

    // Flush color grade (brightness, contrast, saturation, hue shift)
    // LIMITATION: params go through ONE shared UBO per shader, written at flush time
    // but read at submit() - so two calls with DIFFERENT params in one frame
    // collapse to the LAST params (the ring fix at flush_ring is the template
    // if that ever needs to be two instances - today the cap is one per family).
    void flush_color_grade(SpriteBatch &batch, TextureHandle texture, SamplerHandle sampler,
                           f32 brightness = 1.0f, f32 contrast = 1.0f, f32 saturation = 1.0f, f32 hue_shift = 0.0f) noexcept {
        if (color_grade_ubo) {
            // Compute hue rotation matrix (column-major)
            f32 c = __builtin_cosf(hue_shift);
            f32 s = __builtin_sinf(hue_shift);
            ColorGrade p = {brightness, contrast, saturation,
                {0.299f + 0.701f*c - 0.168f*s,  0.587f - 0.587f*c - 0.330f*s,  0.114f - 0.114f*c + 0.497f*s,
                 0.299f - 0.299f*c + 0.328f*s,  0.587f + 0.413f*c + 0.035f*s,  0.114f - 0.114f*c - 0.328f*s,
                 0.299f - 0.299f*c - 0.328f*s,  0.587f - 0.587f*c + 0.035f*s,  0.114f + 0.886f*c + 0.203f*s},
                {0.0f, 0.0f, 0.0f, 0.0f}};
            (void)backend->update_buffer(color_grade_ubo, &p, 0, sizeof(p));
        }
        flush_sprites_impl(batch, color_grade_pipeline, texture, sampler, color_grade_ubo);
    }

    // Flush particles — instanced draw from ParticlePool SoA data
    // Shader expects: [[buffer(1)]] instance data, [[buffer(2)]] camera, [[buffer(3)]] atlas
    void flush_particles(ParticlePool &pool, SortKey key = {}) noexcept {
        u16 count = pool.count;
        if (count == 0) {
            return;
        }

        // Pack SoA → contiguous instance buffer (4 × float4 = 64 bytes/particle)
        struct ParticleInstance {
            f32 px, py, pad0, scale;         // offset +0: float4(pos.xy, 0, scale)
            f32 r, g, b, atlas_id;           // offset +16: float4(color.rgb, atlas)
            f32 rotation, alpha, pad1, pad2; // offset +32: float2(rot, alpha)
            f32 pad3[4];                     // offset +48: unused (4th float4)
        };
        static_assert(sizeof(ParticleInstance) == 64, "ParticleInstance must be 64 bytes");

        auto *instances = temp_arena.alloc_array<ParticleInstance>(count);
        if (!instances) {
            return;
        }

        for (u16 i = 0; i < count; ++i) {
            f32 life_ratio = pool.life_max[i] > 0.0f ? pool.life[i] / pool.life_max[i] : 1.0f;
            f32 alpha      = life_ratio < 0.0f ? 0.0f : (life_ratio > 1.0f ? 1.0f : life_ratio);

            // pool.color[i] = 0xAARRGGBB; update() writes alpha to byte 3
            mm_math::color pc = mm_math::color::from_u32_argb(pool.color[i]);
            f32 r           = pc.r;
            f32 g           = pc.g;
            f32 b           = pc.b;

            instances[i]     = {pool.px[i],
                                pool.py[i],
                                0.0f,
                                pool.scale[i] * 8.0f, // base particle size
                                r,
                                g,
                                b,
                                static_cast<f32>(pool.atlas_id[i]),
                                pool.rotation[i],
                                alpha,
                                0.0f,
                                0.0f,
                                {0, 0, 0, 0}};
        }

        // MM_LOG("FLUSH_PARTICLES: count=%u first_px=%.2f first_py=%.2f first_scale=%.2f first_r=%.2f first_g=%.2f first_b=%.2f first_alpha=%.2f", count,
        //        instances[0].px, instances[0].py, instances[0].scale, instances[0].r, instances[0].g, instances[0].b, instances[0].alpha);
        u32 byte_offset = particle_instance_count * sizeof(ParticleInstance);
        // MM_LOG("FLUSH_PARTICLES: count=%u byte_offset=%u size=%lu", count, byte_offset, sizeof(ParticleInstance));
        (void)backend->update_buffer(particle_ib, instances, byte_offset, count * sizeof(ParticleInstance));

        auto &mat = materials[static_cast<u8>(e_material_type::PARTICLE)];
        graph.bind_pipeline(mat.pipeline, key);
        graph.bind_vertex_buffer(particle_ib, 1, byte_offset, sizeof(ParticleInstance), key);
        graph.bind_uniform_buffer(camera_ubo, 1, key);         // Camera -> [[buffer(2)]] / Binding 1
        graph.bind_uniform_buffer(particle_atlas_ubo, 2, key); // Atlas -> [[buffer(3)]] / Binding 2
        graph.bind_fragment_texture(mat.texture, 1, key);      // Texture -> [[texture(0)]] / Binding 0
        graph.bind_fragment_sampler(mat.sampler, 1, key);      // Sampler -> [[sampler(0)]] / Binding 0
        graph.draw(key, 4, count, 0, 0);

        particle_instance_count += count;
    }

    // Scissor must carry the SortKey of the draws it guards: submit()
    // stable-sorts commands by key, so a default-key scissor recorded
    // before key-1 text draws executes in the key-0 block (text ignores it).
    // Pass the text key (same as draw_text's) when clipping text.
    // The key defaults to the SPRITE DRAW key, not SortKey{}.
    //
    // submit() stable-sorts every command by key, and flush_sprites_impl draws
    // at {0,0,0,1.0f} while a default-constructed SortKey packs depth 0. Every
    // sprite scissors in the frame therefore sorted BEFORE every sprite draw, so
    // only the last one in the frame was ever in effect when the draws ran - a
    // clip rect applied to one widget silently governed the whole frame. This is
    // the same failure as the text/text_layer one, mirrored: a key that is too
    // LOW lands before the thing it was meant to clip.
    //
    // A scissor that must clip TEXT has to pass ui::text_key(r) explicitly, as
    // the UI already does.
    void set_scissor(i16 x, i16 y, u16 w, u16 h, SortKey key = SortKey{0, 0, 0, 1.0f}) noexcept {
        f32 s = content_scale;
        graph.set_scissor(static_cast<i16>(static_cast<f32>(x) * s), static_cast<i16>(static_cast<f32>(y) * s),
                          static_cast<u16>(static_cast<f32>(w) * s), static_cast<u16>(static_cast<f32>(h) * s),
                          key);
    }

    void cull_sprites(SpriteBatch &batch) noexcept {
        const f32 view_left   = 0.0f;
        const auto  view_right  = static_cast<f32>(width);
        const f32 view_top    = 0.0f;
        const auto  view_bottom = static_cast<f32>(height);
        for (u16 i = 0; i < batch.count; ++i) {
            if (!batch.active[i]) {
                continue;
            }
            const f32 hw     = batch.scale_x[i] * 0.5f; // true half-width
            const f32 hh     = batch.scale_y[i] * 0.5f; // true half-height
            const f32 radius = __builtin_sqrtf(hw * hw + hh * hh);

            const f32 sx     = batch.world_x[i]; // sprite centre x
            const f32 sy     = batch.world_y[i]; // sprite centre y

            // Separating-axis test: cull only when the bounding circle is
            // entirely outside one of the four view edges.
            if (sx + radius < view_left || sx - radius > view_right || sy + radius < view_top || sy - radius > view_bottom) {
                batch.active[i] = 0;
            }
        }
    }

    void                     bind_pipeline(PipelineHandle pipeline) noexcept { graph.bind_pipeline(pipeline); }

    // Create one default pipeline from `d`, storing the result in
    // `out`. On failure: log the pipeline name, tear down the
    // half-built resource set, and propagate the error. Centralises
    // the destroy-and-return that every default-pipeline block
    // repeated inline (the first few blocks failed without even
    // naming the pipeline).
    Expected<void, RHIError> create_default_pipeline(const PipelineDesc &d, const char *name, PipelineHandle &out) noexcept {
        auto res = backend->create_pipeline(d);
        if (!res) {
            MM_ERROR("create_default_pipelines: failed to create %s", name);
            destroy_resources();
            return make_unexpected(res.error());
        }
        out = *res;
        MM_LOG("create_default_pipelines: %s=%u", name, out.handle.id);
        return {};
    }

    Expected<void, RHIError> create_default_pipelines() noexcept {
        MM_LOG("create_default_pipelines: START");
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
        desc.vertex_attrs[0]        = {0, PixelFormat::R16G16_FLOAT, 0, sizeof(SpriteVertex)};
        desc.vertex_attrs[1]        = {1, PixelFormat::R16G16_FLOAT, 4, sizeof(SpriteVertex)};
        desc.vertex_attrs[2]        = {2, PixelFormat::R8G8B8A8_UNORM, 8, sizeof(SpriteVertex)};

        desc.descriptor_count       = 2;
        desc.descriptor_bindings[0] = {1, DescriptorType::UniformBuffer, 1, 1};        // Slot 1: Camera
        desc.descriptor_bindings[1] = {0, DescriptorType::CombinedImageSampler, 2, 1}; // Slot 0: Tex/Samp

        // Sprite pipeline
        desc.vertex_shader   = shader::sprite_vertex();
        desc.fragment_shader = shader::sprite_fragment();
        if (auto e = create_default_pipeline(desc, "sprite_pipeline", sprite_pipeline); !e) {
            return e;
        }

        // Additive blend sprite pipeline
        PipelineDesc add_desc = desc;
        add_desc.src_blend    = BlendFactor::SrcAlpha;
        add_desc.dst_blend    = BlendFactor::One;
        if (auto e = create_default_pipeline(add_desc, "sprite_additive_pipeline", sprite_additive_pipeline); !e) {
            return e;
        }

        // Multiply blend sprite pipeline
        PipelineDesc mul_desc    = desc;
        mul_desc.src_blend       = BlendFactor::Zero;
        mul_desc.dst_blend       = BlendFactor::SrcColor;
        if (auto e = create_default_pipeline(mul_desc, "sprite_multiply_pipeline", sprite_multiply_pipeline); !e) {
            return e;
        }

        // Opaque sprite pipeline
        PipelineDesc opq_desc    = desc;
        opq_desc.src_blend       = BlendFactor::One;
        opq_desc.dst_blend       = BlendFactor::Zero;
        if (auto e = create_default_pipeline(opq_desc, "sprite_opaque_pipeline", sprite_opaque_pipeline); !e) {
            return e;
        }

        // SDF pipeline (same 12-byte SpriteVertex format as sprites)
        PipelineDesc sdf_desc      = desc;
        sdf_desc.vertex_shader     = shader::sdf_vertex();
        sdf_desc.fragment_shader   = shader::sdf_fragment();
        sdf_desc.vertex_attr_count = 3;
        sdf_desc.vertex_attrs[0]   = {0, PixelFormat::R16G16_FLOAT, 0, sizeof(SpriteVertex)};
        sdf_desc.vertex_attrs[1]   = {1, PixelFormat::R16G16_FLOAT, 4, sizeof(SpriteVertex)};
        sdf_desc.vertex_attrs[2]   = {2, PixelFormat::R8G8B8A8_UNORM, 8, sizeof(SpriteVertex)};
        if (auto e = create_default_pipeline(sdf_desc, "sdf_pipeline", sdf_pipeline); !e) {
            return e;
        }

        // Particle pipeline
        PipelineDesc particle_desc           = desc;
        particle_desc.is_instance            = true;
        particle_desc.vertex_shader          = shader::particle_vertex();
        particle_desc.fragment_shader        = shader::particle_fragment();
        particle_desc.descriptor_count       = 3;
        particle_desc.descriptor_bindings[0] = {0, DescriptorType::CombinedImageSampler, 2, 1}; // Texture/sampler
        particle_desc.descriptor_bindings[1] = {2, DescriptorType::UniformBuffer, 1, 1};        // Camera
        particle_desc.descriptor_bindings[2] = {3, DescriptorType::UniformBuffer, 1, 1};        // Atlas
        particle_desc.vertex_attr_count      = 3;
        particle_desc.vertex_attrs[0]        = {0, PixelFormat::R32G32B32A32_FLOAT, 0, 64};
        particle_desc.vertex_attrs[1]        = {1, PixelFormat::R32G32B32A32_FLOAT, 16, 64};
        particle_desc.vertex_attrs[2]        = {2, PixelFormat::R32G32B32A32_FLOAT, 32, 64};
        if (auto e = create_default_pipeline(particle_desc, "particle_pipeline", particle_pipeline); !e) {
            return e;
        }

        // Rounded sprite pipeline (base: no glow, no border - uses function constants)
        PipelineDesc rounded_desc           = desc;
        rounded_desc.vertex_shader          = shader::rounded_sprite_vertex();
        rounded_desc.fragment_shader        = shader::rounded_sprite_fragment();
        rounded_desc.descriptor_count       = 3;
        rounded_desc.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
        rounded_desc.vertex_attr_count      = 6;
        rounded_desc.vertex_attrs[0]        = {0, PixelFormat::R16G16_FLOAT, 0, sizeof(SpriteVertex)};
        rounded_desc.vertex_attrs[1]        = {1, PixelFormat::R16G16_FLOAT, 4, sizeof(SpriteVertex)};
        rounded_desc.vertex_attrs[2]        = {2, PixelFormat::R8G8B8A8_UNORM, 8, sizeof(SpriteVertex)};
        rounded_desc.vertex_attrs[3]        = {3, PixelFormat::R16G16_FLOAT, 12, sizeof(SpriteVertex)};
        rounded_desc.vertex_attrs[4]        = {4, PixelFormat::R16G16_FLOAT, 16, sizeof(SpriteVertex)};
        rounded_desc.vertex_attrs[5]        = {5, PixelFormat::R8G8B8A8_UNORM, 20, sizeof(SpriteVertex)};
        // Function constants: HAS_GLOW=0 (index 0), HAS_BORDER=0 (index 1)
        rounded_desc.function_constant_count = 2;
        rounded_desc.function_constants[0] = {0, false}; // HAS_GLOW
        rounded_desc.function_constants[1] = {1, false}; // HAS_BORDER
        if (auto e = create_default_pipeline(rounded_desc, "rounded_sprite_pipeline", rounded_sprite_pipeline); !e) {
            return e;
        }

        // Glow variant pipeline (HAS_GLOW=1, HAS_BORDER=0)
        {
            PipelineDesc glow_desc = rounded_desc;
            glow_desc.function_constants[0] = {0, true};  // HAS_GLOW
            glow_desc.function_constants[1] = {1, false}; // HAS_BORDER
            if (auto e = create_default_pipeline(glow_desc, "rounded_sprite_glow_pipeline", rounded_sprite_glow_pipeline); !e) {
                return e;
            }
        }

        // Border variant pipeline (HAS_GLOW=0, HAS_BORDER=1)
        {
            PipelineDesc border_desc = rounded_desc;
            border_desc.function_constants[0] = {0, false}; // HAS_GLOW
            border_desc.function_constants[1] = {1, true};  // HAS_BORDER
            if (auto e = create_default_pipeline(border_desc, "rounded_sprite_border_pipeline", rounded_sprite_border_pipeline); !e) {
                return e;
            }
        }

        // ─── Sprite-based variant pipelines (same vertex format, custom fragment)
        // Sprite outline pipeline
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::sprite_outline_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "sprite_outline_pipeline", sprite_outline_pipeline); !e) {
                return e;
            }
        }

        // Clip rect pipeline
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::clip_rect_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "clip_rect_pipeline", clip_rect_pipeline); !e) {
                return e;
            }
        }

        // Dissolve pipeline
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::dissolve_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "dissolve_pipeline", dissolve_pipeline); !e) {
                return e;
            }
        }

        // Grayscale pipeline
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::grayscale_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "grayscale_pipeline", grayscale_pipeline); !e) {
                return e;
            }
        }

        // ─── Lab-port pipelines (sprite vertex + custom fragment) ────
        // Second texture (normal map) uses descriptor binding 1, bound by
        // the host at logical slot 2 (Metal quirk: index 1 aliases slot 0).
        // NOTE: the Vulkan backend binds a single fragment texture per
        // pipeline today, so NORMAL_MAP/PLASTIC are Metal-ready and
        // Vulkan-deferred (their SPV still compiles; do not use on Vulkan).
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::normal_derive_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "normal_derive_pipeline", normal_derive_pipeline); !e) {
                return e;
            }
        }
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::normal_map_fragment();
            pd.descriptor_count       = 4;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            pd.descriptor_bindings[3] = {1, DescriptorType::CombinedImageSampler, 2, 1};
            if (auto e = create_default_pipeline(pd, "normal_map_pipeline", normal_map_pipeline); !e) {
                return e;
            }
        }
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::cartoon_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "cartoon_pipeline", cartoon_pipeline); !e) {
                return e;
            }
        }
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::plastic_fragment();
            pd.descriptor_count       = 4;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            pd.descriptor_bindings[3] = {1, DescriptorType::CombinedImageSampler, 2, 1};
            if (auto e = create_default_pipeline(pd, "plastic_pipeline", plastic_pipeline); !e) {
                return e;
            }
        }
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::glow_pulse_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "glow_pulse_pipeline", glow_pulse_pipeline); !e) {
                return e;
            }
        }
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::gold_border_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "gold_border_pipeline", gold_border_pipeline); !e) {
                return e;
            }
        }
        {
            PipelineDesc pd           = desc;
            pd.vertex_shader          = shader::sprite_vertex();
            pd.fragment_shader        = shader::gold_stay_fragment();
            pd.descriptor_count       = 3;
            pd.descriptor_bindings[2] = {2, DescriptorType::UniformBuffer, 2, 1};
            if (auto e = create_default_pipeline(pd, "gold_stay_pipeline", gold_stay_pipeline); !e) {
                return e;
            }
        }
        {
            // Analytic ring pipeline: rounded vertex format (v_local drives
            // the ring), ring fragment, no function constants.
            PipelineDesc ring_desc           = rounded_desc;
            ring_desc.vertex_shader          = shader::rounded_sprite_vertex();
            ring_desc.fragment_shader        = shader::ring_fragment();
            ring_desc.function_constant_count = 0;
            if (auto e = create_default_pipeline(ring_desc, "ring_pipeline", ring_pipeline); !e) {
                return e;
            }
        }

        // ─── Post-process pipelines (screen_quad vertex, no vertex attributes)
        {
            PipelineDesc ppd{};
            ppd.prim_type              = PrimitiveType::Triangle;
            ppd.cull_mode              = CullMode::None;
            ppd.src_blend              = BlendFactor::One;
            ppd.dst_blend              = BlendFactor::Zero;
            ppd.blend_op               = BlendOp::Add;
            ppd.color_count            = 1;
            ppd.color_formats[0]       = PixelFormat::B8G8R8A8_SRGB;
            ppd.depth_format           = static_cast<PixelFormat>(0);
            ppd.depth_test             = false;
            ppd.depth_write            = false;
            ppd.vertex_attr_count      = 0;
            ppd.descriptor_count       = 2;
            ppd.descriptor_bindings[0] = {0, DescriptorType::CombinedImageSampler, 2, 1};
            ppd.descriptor_bindings[1] = {2, DescriptorType::UniformBuffer, 2, 1};

            ppd.vertex_shader          = shader::screen_quad_vertex();
            ppd.fragment_shader        = shader::blur_fragment();
            if (auto e = create_default_pipeline(ppd, "blur_pipeline", blur_pipeline); !e) {
                return e;
            }

            ppd.fragment_shader = shader::color_grade_fragment();
            if (auto e = create_default_pipeline(ppd, "color_grade_pipeline", color_grade_pipeline); !e) {
                return e;
            }
        }

        // Build built-in materials
        materials[static_cast<u8>(e_material_type::SPRITEALPHA)]    = {sprite_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::SPRITEADDITIVE)] = {sprite_additive_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::SPRITEMULTIPLY)] = {sprite_multiply_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::SPRITEOPAQUE)]   = {sprite_opaque_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::SDF)]            = {sdf_pipeline, font_tex, font_sampler};
        materials[static_cast<u8>(e_material_type::PARTICLE)]       = {particle_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::ROUNDEDSPRITE)]  = {rounded_sprite_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::SPRITEOUTLINE)]  = {sprite_outline_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::CLIPRECT)]       = {clip_rect_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::DISSOLVE)]       = {dissolve_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::GRAYSCALE)]      = {grayscale_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::NORMALDERIVE)]   = {normal_derive_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::NORMALMAP)]      = {normal_map_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::CARTOON)]        = {cartoon_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::PLASTIC)]        = {plastic_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::GLOWPULSE)]      = {glow_pulse_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::GOLDBORDER)]     = {gold_border_pipeline, white_tex, default_sampler};
        materials[static_cast<u8>(e_material_type::GOLDSTAY)]       = {gold_stay_pipeline, white_tex, default_sampler};

        return {};
    }

    // ─── Material accessors ─────────────────────────────────────
    const Material &material(e_material_type type) const noexcept { return materials[static_cast<u8>(type)]; }

    Material        make_material(PipelineHandle pipeline, TextureHandle texture, SamplerHandle sampler) const noexcept { return {pipeline, texture, sampler}; }

    void            measure_text(BitmapFont &font, const char *text, f32 scale, f32 &out_w, f32 &out_h) noexcept {
        auto len = static_cast<u32>(std::strlen(text));
        if (len == 0) {
            out_w = 0.0f;
            out_h = 0.0f;
            return;
        }

        Utf8Decoder dec(text, len);
        f32       cursor_x            = 0.0f;
        f32       max_x               = 0.0f;
        f32       y                   = 0.0f;
        u32    prev_consonant      = 0;

        // The Thai classifiers are the globals in mm_text_renderer.hpp. These
        // used to be lambda copies here and in draw_text, and the copies had
        // already drifted from each other (one knew U+0E4D, one did not).

        for (;;) {
            u32 cp = dec.next();
            if (cp == 0) {
                break;
            }

            if (cp == '\n') {
                if (cursor_x > max_x) {
                    max_x = cursor_x;
                }
                cursor_x        = 0.0f;
                y              += font.line_height * scale;
                prev_consonant  = 0;
                continue;
            }

            if (cp == ' ') {
                const GlyphInfo *sg  = font.get_glyph(' ');
                cursor_x            += sg ? sg->advance * scale : 16.0f * scale;
                prev_consonant       = 0;
                continue;
            }

            const GlyphInfo *g = nullptr;
            if (cp >= 0x0E01 && cp <= 0x0E5B) {
                int idx = (int)(cp - 0x0E01);
                if (idx >= 0 && idx < MAX_GLYPHS_TH) {
                    auto &th_g = font.glyphs_th[idx];
                    if (th_g.w > 0) {
                        g = &th_g;
                    }
                }
            } else {
                g = resolve_glyph(font, cp);
            }

            if (g == nullptr) {
                continue;
            }

            if (is_thai_combining(cp) && prev_consonant != 0) {
                // Combining marks don't advance cursor
            } else {
                cursor_x += g->advance * scale;
                if (is_thai_consonant(cp)) {
                    prev_consonant = cp;
                } else {
                    prev_consonant = 0;
                }
            }
        }
        if (cursor_x > max_x) {
            max_x = cursor_x;
        }
        out_w = max_x;
        out_h = y + font.line_height * scale;
    }

    // แทนที่ draw_text method เดิมด้วยอันนี้
    void draw_text(BitmapFont &font, const char *text, f32 x, f32 y, u32 color = 0xFFFFFFFF, f32 scale = 1.0f,
                   bool snap = true) noexcept {
        auto len = static_cast<u32>(std::strlen(text));
        if (len == 0) {
            return;
        }

        u32 max_glyphs = len > TEXT_MAX_GLYPH ? TEXT_MAX_GLYPH : len;
        auto    *verts      = temp_arena.alloc_array<SpriteVertex>(max_glyphs * 4);
        if (!verts) {
            MM_LOG("problem at allocate verts");
            return;
        }
        auto *indices = temp_arena.alloc_array<u16>(max_glyphs * 6);
        if (!indices) {
            MM_LOG("problem at allocate indices");
            return;
        }

        Utf8Decoder dec(text, len);
        f32       cursor_x       = x;
        f32       cursor_y       = y;
        u32    vert_count     = 0;
        u32    idx_count      = 0;

        // Thai combining character state
        u32    prev_consonant = 0;
        // f32       prev_consonant_x = 0;
        // f32       prev_consonant_y = 0;

        for (u16 g = 0; g < max_glyphs; ++g) {
            u32 cp = dec.next();
            if (cp == 0) {
                break;
            }

            // Thai classification comes from the globals in mm_text_renderer.hpp
            // (they used to be rebuilt as lambdas inside this loop, per glyph).

            if (cp == ' ') {
                const GlyphInfo *space_glyph = font.get_glyph(' ');
                if (space_glyph) {
                    cursor_x += static_cast<f32>(space_glyph->advance) * scale;
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
                auto thai_idx = cp - 0x0E01;
                if (thai_idx >= 0 && thai_idx < MAX_GLYPHS_TH) {
                    auto &th_g = font.glyphs_th[thai_idx];
                    if (th_g.w > 0 && th_g.h > 0) {
                        glyph = &th_g; // Use directly, don't use static copy
                    }
                }
            } else {
                glyph = resolve_glyph(font, cp);
            }

            // resolve_glyph already substitutes '?' and drops the unbaked, so
            // there is nothing left to decide here: either we have a drawable
            // glyph or we advance nothing (the old inline '?' block is what
            // measure() and measure_text disagreed with).
            if (glyph == nullptr) {
                continue;
            }

            f32       gx, gy;
            // Front vowels (เ แ โ ใ ไ)
            if (is_thai_vowel_front(cp)) {
                gx = cursor_x + static_cast<f32>(glyph->bearing_x) * scale;
                gy = cursor_y + static_cast<f32>(glyph->bearing_y) * scale;
                // Draw front vowel at current cursor position
                // (will advance cursor after)
            }
            // Combining characters (vowels above/below, tone marks)
            else if (is_thai_combining(cp) && prev_consonant != 0) {
                gx = cursor_x + static_cast<f32>(glyph->bearing_x) * scale;
                gy = cursor_y + static_cast<f32>(glyph->bearing_y) * scale;

                if (is_thai_vowel_above(cp)) {
                    prev_consonant  = cp;
                    gy             -= static_cast<f32>(font.line_height) * 0.06f * scale;
                } else if (is_thai_vowel_below(cp)) {
                    gy += static_cast<f32>(font.line_height) * 0.06f * scale;
                } else if (is_thai_tone_mark(cp)) {
                    if (is_thai_vowel_above(prev_consonant)) {
                        gy -= static_cast<f32>(font.line_height) * 0.25f * scale;
                    } else {
                        gy -= static_cast<f32>(font.line_height) * 0.06f * scale;
                    }
                }

                // Don't advance cursor for combining marks
                goto add_quad;
            }
            // Base character
            else {
                gx = cursor_x + static_cast<f32>(glyph->bearing_x) * scale;
                gy = cursor_y + static_cast<f32>(glyph->bearing_y) * scale;

                // Store for potential combining marks
                if (is_thai_consonant(cp)) {
                    prev_consonant = cp;
                    // prev_consonant_x = cursor_x;
                    // prev_consonant_y = cursor_y;
                } else {
                    prev_consonant = 0;
                }
            }

            // Advance cursor for base characters and front vowels
            cursor_x += static_cast<f32>(glyph->advance) * scale;

            // Device-pixel snap: static text lands on exact texels (crisp
            // at any content_scale). Animated text (win slide-in) opts out
            // via snap=false so motion stays smooth.
            if (snap && content_scale > 0.0f) {
                gx = __builtin_roundf(gx * content_scale) / content_scale;
                gy = __builtin_roundf(gy * content_scale) / content_scale;
            }

        add_quad:
            // Bounds check before adding quad
            if (vert_count + 4 > max_glyphs * 4 || idx_count + 6 > max_glyphs * 6) {
                MM_LOG("WARNING: Text buffer overflow, skipping remaining glyphs");
                break;
            }

            f32    gw       = static_cast<f32>(glyph->w) * scale;
            f32    gh       = static_cast<f32>(glyph->h) * scale;

            f32    u0       = static_cast<f32>(glyph->u) / font.atlas_w;
            f32    v0       = static_cast<f32>(glyph->v) / font.atlas_h;
            f32    u1       = static_cast<f32>(glyph->u + glyph->w) / font.atlas_w;
            f32    v1       = static_cast<f32>(glyph->v + glyph->h) / font.atlas_h;

            u32 base     = vert_count;
            auto     add_vert = [&](f32 px, f32 py, f32 pu, f32 pv) {
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

            indices[idx_count++] = static_cast<u16>(base);
            indices[idx_count++] = static_cast<u16>(base + 1);
            indices[idx_count++] = static_cast<u16>(base + 2);
            indices[idx_count++] = static_cast<u16>(base + 1);
            indices[idx_count++] = static_cast<u16>(base + 3);
            indices[idx_count++] = static_cast<u16>(base + 2);
        }

        if (vert_count == 0) {
            return;
        }

        if (text_vertex_count + vert_count > MAX_TEXT_VERTS || text_index_count + idx_count > MAX_TEXT_INDICES) {
            // SILENT DROP. These two budgets (~1024 glyphs) are shared by EVERY
            // draw_text in the frame and are not per-widget, so a page with a lot
            // of labels loses whichever calls happen to come last in pool order -
            // not the longest string, and not deterministically the same ones.
            // text_dropped_calls is how a host finds out (see its declaration).
            text_dropped_calls++;
            return;
        }

        u32 cur_vert_off   = text_vertex_count;
        u32 cur_idx_off    = text_index_count;
        u32 vb_byte_offset = cur_vert_off * sizeof(SpriteVertex);
        u32 ib_byte_offset = cur_idx_off * sizeof(u16);

        (void)backend->update_buffer(text_vb, verts, vb_byte_offset, vert_count * sizeof(SpriteVertex));
        (void)backend->update_buffer(text_ib, indices, ib_byte_offset, idx_count * sizeof(u16));

        SortKey key{text_layer, 0, 0, 1.0f};
        graph.bind_pipeline(sdf_pipeline, key);
        graph.bind_vertex_buffer(text_vb, 0, 0, sizeof(SpriteVertex), key);
        graph.bind_uniform_buffer(camera_ubo, 0, key); // Logical Slot 0: UBO
        graph.bind_index_buffer(text_ib, IndexType::Uint16, 0, key);
        graph.bind_fragment_texture(font_tex, 0, key);     // Logical Slot 0: Texture
        graph.bind_fragment_sampler(font_sampler, 0, key); // Logical Slot 0: Sampler
        graph.draw_indexed(key, idx_count, 1, cur_idx_off, static_cast<i32>(cur_vert_off));

        text_vertex_count += vert_count;
        text_index_count  += idx_count;
    }

    // Reset the text buffer counters for the new frame.
    // draw_text uploads glyphs and records its draw incrementally
    // during the frame (text_vertex_count / text_index_count are the
    // running offsets into the shared text buffers), so the counters
    // must return to 0 before the first draw_text of the next frame
    // or text would append past the end of the buffer.
    void flush_text() noexcept {
        text_vertex_count = 0;
        text_index_count  = 0;
    }

    void resize(u32 w, u32 h) noexcept {
        width      = w;
        height     = h;
        inv_width  = 1.0f / static_cast<f32>(w);
        inv_height = 1.0f / static_cast<f32>(h);
        ortho(0.0f, static_cast<f32>(w), static_cast<f32>(h), 0.0f, -1.0f, 1.0f);
    }
};

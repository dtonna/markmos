// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// 2D Stress Test — 10k animated sprites @ 60fps
// Tests: SpriteBatch SoA fill rate, vertex generation throughput, draw call batching

#include "../rhi/mm_rhi_concept.hpp"
#include "../core/mm_types.h"
#include "../render/mm_sprite_batch.hpp"
#include "../render/mm_shader_registry.hpp"
#include "../core/mm_arena.hpp"
#include "../game/mm_camera_trauma.hpp"
#include "../app/mm_app.hpp"
#include "../math/mm_mat4.h"
#if defined(USE_METAL_BACKEND)
#include "../rhi/mm_metal_backend.hpp"
#elif defined(USE_VULKAN_BACKEND)
#include "../rhi/mm_vulkan_backend.hpp"
#endif

static constexpr u16 SPRITE_COUNT = 10000;
static constexpr size_t   ARENA_SIZE   = 2 * 1024 * 1024;

static SpriteBatch   g_batch;
static BufferHandle  g_vb, g_ub;
static PipelineHandle g_pipeline;
static TextureHandle g_texture;
static SamplerHandle g_sampler;
alignas(64) static char g_arena_buf[ARENA_SIZE];
static FrameArena   g_arena;
static CameraTrauma g_camera;
static f32        g_time;
static u32     g_frame_count;

static void game_init(void*) {
    g_time = 0.0f;
    g_frame_count = 0;
    g_batch.init();

    // Spawn 10k sprites in a grid pattern
    u16 grid_side = 100;
    for (u16 i = 0; i < SPRITE_COUNT && i < MAX_SPRITES; ++i) {
        f32 x = static_cast<f32>(i % grid_side) * 16.0f;
        f32 y = static_cast<f32>(i / grid_side) * 16.0f;
        g_batch.add(x, y, 14.0f, 14.0f, 0.0f, 0xFFFFFFFF, 1);
    }

    auto& bk = *g_backend;

    BufferDesc vb_desc = {
        .type = BufferType::Vertex,
        .size = MAX_VERTS * sizeof(SpriteVertex),
        .stride = sizeof(SpriteVertex),
        .cpu_visible = true
    };
    auto vr = bk.create_buffer(vb_desc);
    if (!vr) return;
    g_vb = *vr;

    BufferDesc ub_desc = { .type = BufferType::Uniform, .size = 64, .stride = 0, .cpu_visible = true };
    auto ur = bk.create_buffer(ub_desc);
    if (!ur) return;
    g_ub = *ur;

    auto vs = shader::sprite_vertex();
    auto fs = shader::sprite_fragment();
    VertexAttribute va[3] = {
        { 0, PixelFormat::R16G16_FLOAT,    0, 12 },
        { 1, PixelFormat::R16G16_FLOAT,    4, 12 },
        { 2, PixelFormat::R8G8B8A8_UNORM,  8, 12 },
    };
    PipelineDesc pd = {};
    pd.vertex_shader     = vs;
    pd.fragment_shader   = fs;
    pd.prim_type         = PrimitiveType::Triangle;
    pd.cull_mode         = CullMode::None;
    pd.src_blend         = BlendFactor::SrcAlpha;
    pd.dst_blend         = BlendFactor::OneMinusSrcAlpha;
    pd.blend_op          = BlendOp::Add;
    pd.color_formats[0]  = PixelFormat::B8G8R8A8_SRGB;
    pd.color_count       = 1;
    for (u8 i = 0; i < 3; ++i) pd.vertex_attrs[i] = va[i];
    pd.vertex_attr_count = 3;
    auto pr = bk.create_pipeline(pd);
    if (!pr) return;
    g_pipeline = *pr;

    TextureDesc td = { TextureType::Tex2D, PixelFormat::R8G8B8A8_UNORM, 1, 1, 1, 1, 1 };
    auto tr = bk.create_texture(td);
    if (!tr) return;
    g_texture = *tr;
    u32 white = 0xFFFFFFFF;
    bk.update_texture(g_texture, &white, 0, 0, 1, 1, 0, 0);

    SamplerDesc sd = { SamplerFilter::Nearest, SamplerFilter::Nearest, SamplerFilter::Nearest,
                       SamplerAddress::ClampToEdge, SamplerAddress::ClampToEdge, SamplerAddress::ClampToEdge,
                       CompareOp::Never, 1.0f };
    auto sr = bk.create_sampler(sd);
    if (!sr) return;
    g_sampler = *sr;

    g_arena.init(g_arena_buf, ARENA_SIZE);
}

static void game_frame(void*, f32 dt, InputState&) {
    g_time += dt;
    ++g_frame_count;

    // Camera shake every second
    if (g_frame_count % 60 == 0) g_camera.add_trauma(SHAKE_SMALL);
    g_camera.update(dt);

    // Animate sprites in a wave pattern
    for (u16 i = 0; i < g_batch.count; ++i) {
        if (!g_batch.active[i]) continue;
        f32 phase = g_time + static_cast<f32>(i) * 0.01f;
        g_batch.world_x[i] += __builtin_sinf(phase) * 0.5f;
        g_batch.world_y[i] += __builtin_cosf(phase * 0.7f) * 0.5f;
    }

    auto& bk = *g_backend;
    g_arena.reset();

    f32 cx, cy, ca;
    g_camera.get_offset(g_time, cx, cy, ca);
    mm_math::mat4 cam = mm_math::mat4::ortho(0.0f, 1170.0f, 2532.0f, 0.0f, -1.0f, 1.0f);
    bk.update_buffer(g_ub, cam.data(), 0, sizeof(mm_math::mat4));

    g_batch.flush(bk, g_vb, g_ub, g_pipeline, g_texture, g_sampler, g_arena, cx, cy, 1.0f);
}

static void game_cleanup(void*) {
    auto& bk = *g_backend;
    bk.destroy_buffer(g_vb);
    bk.destroy_buffer(g_ub);
    bk.destroy_pipeline(g_pipeline);
    bk.destroy_texture(g_texture);
    bk.destroy_sampler(g_sampler);
}

extern "C" AppCallbacks markmos_main(int, char**) {
    return {
        .user_data = nullptr,
        .init = game_init,
        .frame = game_frame,
        .cleanup = game_cleanup,
    };
}

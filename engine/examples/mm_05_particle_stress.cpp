// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Particle Stress Test — 2048 GPU-instanced particles @ 60fps
// Tests: ParticlePool SoA update, GPU instancing throughput, burst/cone/rain emitters
// Cache measurement: sequential SoA write = L1 streaming, single instanced draw call

#include "../game/mm_particle_pool.hpp"
#include "../game/mm_camera_trauma.hpp"
#include "../core/mm_arena.hpp"
#include "../render/mm_shader_registry.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include "../app/mm_app.hpp"
#if defined(USE_METAL_BACKEND)
#include "../rhi/mm_metal_backend.hpp"
#elif defined(USE_VULKAN_BACKEND)
#include "../rhi/mm_vulkan_backend.hpp"
#endif

#include <cstring>
#include <cmath>

// ─── Math helpers ────────────────────────────────────────────────
struct Mat4 {
    float m[16];

    static Mat4 ortho(float l, float r, float b, float t, float n, float f) noexcept {
        Mat4 r_{};
        r_.m[0]  = 2.0f / (r - l);   r_.m[4]  = 0;                r_.m[8]  = 0;  r_.m[12] = -(r + l) / (r - l);
        r_.m[1]  = 0;                 r_.m[5]  = 2.0f / (t - b);   r_.m[9]  = 0;  r_.m[13] = -(t + b) / (t - b);
        r_.m[2]  = 0;                 r_.m[6]  = 0;                r_.m[10] = -2.0f / (f - n); r_.m[14] = -(f + n) / (f - n);
        r_.m[3]  = 0;                 r_.m[7]  = 0;                r_.m[11] = 0;  r_.m[15] = 1.0f;
        return r_;
    }

    static Mat4 translate(float tx, float ty) noexcept {
        Mat4 r = identity();
        r.m[12] = tx;
        r.m[13] = ty;
        return r;
    }

    static Mat4 identity() noexcept {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& rhs) const noexcept {
        Mat4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += m[k * 4 + j] * rhs.m[i * 4 + k];
                r.m[i * 4 + j] = sum;
            }
        return r;
    }
};

// ─── Instance data layout ────────────────────────────────────────
// Shader reads: instances[iid * 4 + 0..2] as float4[3], stride = 64 (4 float4)
// float4[0]: pos_scale  = (px, py, unused, scale)
// float4[1]: color_atlas = (r, g, b, atlas_tile)
// float4[2]: rotation_alpha = (rotation, alpha, unused, unused)
// float4[3]: padding (unused)
struct alignas(16) ParticleInstance {
    float px, py, _pad0, scale;
    float r, g, b, atlas;
    float rotation, alpha, _pad1, _pad2;
    float _pad3[4];
};

static constexpr float SCREEN_W = 1170.0f;
static constexpr float SCREEN_H = 2532.0f;

// GPU resources
static BufferHandle     g_instance_buf;
static BufferHandle     g_camera_ub;
static BufferHandle     g_atlas_ub;
static PipelineHandle   g_pipeline;
static TextureHandle    g_texture;
static SamplerHandle    g_sampler;

static ParticlePool     g_particles;
static CameraTrauma     g_camera;
static float            g_time;
static uint32_t         g_frame;

static Mat4 g_view_proj;

// Emitters
struct Emitter {
    float x, y;
    float timer, interval, lifetime;
    uint32_t color;
    uint8_t type;
};

static Emitter g_emitters[8];
static uint8_t g_emitter_count;

static void init_emitters() noexcept {
    g_time = 0.0f;
    g_frame = 0;
    g_emitter_count = 4;
    g_emitters[0] = { 400, 600, 0, 0.5f, 1.0f, 0xFFFFAA00, 0 };
    g_emitters[1] = { 100, 400, 0, 0.3f, 0.5f, 0xFF4488FF, 1 };
    g_emitters[2] = { 360, 0, 0, 0.1f, 2.0f, 0xFF88FF44, 2 };
    g_emitters[3] = { 360, 640, 0, 0.05f, 0.3f, 0xFFFF44FF, 1 };
}

static void update_emitters(float dt) noexcept {
    g_time += dt;
    ++g_frame;

    for (uint8_t i = 0; i < g_emitter_count; ++i) {
        auto& e = g_emitters[i];
        e.timer -= dt;
        if (e.timer > 0) continue;
        e.timer = e.interval;

        switch (e.type) {
        case 0:
            g_particles.spawn_burst(e.x, e.y, 16, 50, 150, e.lifetime, e.color);
            break;
        case 1:
            g_particles.spawn_cone(e.x, e.y, 8, 0.0f, 0.5f, 200, e.lifetime, e.color);
            break;
        case 2:
            g_particles.spawn_rain(e.x, e.y, 120, 200, 6, e.lifetime, e.color);
            break;
        }
    }

    g_particles.update(dt, 0.0f, 200.0f);

    if (g_frame % 90 == 0) g_camera.add_trauma(SHAKE_MEDIUM);
    g_camera.update(dt);
}

// ─── Game callbacks ──────────────────────────────────────────────
static void game_init(void*) {
    init_emitters();
    auto& bk = *g_backend;

    // Instance buffer — 64 bytes × MAX_PARTICLES
    BufferDesc inst_desc = {
        .type = BufferType::Vertex,
        .size = static_cast<uint32_t>(4096 * sizeof(ParticleInstance)),
        .stride = 0,
        .cpu_visible = true
    };
    auto ir = bk.create_buffer(inst_desc);
    if (!ir) return;
    g_instance_buf = *ir;

    BufferDesc ub_desc = {
        .type = BufferType::Uniform,
        .size = sizeof(Mat4),
        .stride = 0,
        .cpu_visible = true
    };
    auto cr = bk.create_buffer(ub_desc);
    if (!cr) return;
    g_camera_ub = *cr;

    auto ar = bk.create_buffer(ub_desc);
    if (!ar) return;
    // AtlasInfo: 2 float2 = 16 bytes — reuse uniform buffer size
    g_atlas_ub = *ar;

    // 1×1 white texture
    TextureDesc tex_desc = {
        .type = TextureType::Tex2D,
        .format = PixelFormat::R8G8B8A8_UNORM,
        .width = 1, .height = 1, .depth = 1,
        .mip_levels = 1, .array_layers = 1
    };
    auto tr = bk.create_texture(tex_desc);
    if (!tr) return;
    g_texture = *tr;
    static const uint32_t white_pixel = 0xFFFFFFFF;
    bk.update_texture(g_texture, &white_pixel, 0, 0, 0, 1, 1, 1);

    SamplerDesc samp_desc = {
        .min_filter = SamplerFilter::Nearest,
        .mag_filter = SamplerFilter::Nearest,
        .mip_filter = SamplerFilter::Nearest,
        .address_u = SamplerAddress::ClampToEdge,
        .address_v = SamplerAddress::ClampToEdge,
        .address_w = SamplerAddress::ClampToEdge,
        .compare = CompareOp::Never,
        .max_anisotropy = 0.0f
    };
    auto sr = bk.create_sampler(samp_desc);
    if (!sr) return;
    g_sampler = *sr;

    // Pipeline — no vertex attributes, uses vertex_id + instance_id
    auto vs = shader::particle_vertex();
    auto fs = shader::particle_fragment();
    PipelineDesc pd = {};
    pd.vertex_shader   = vs;
    pd.fragment_shader = fs;
    pd.prim_type       = PrimitiveType::Triangle;
    pd.cull_mode       = CullMode::None;
    pd.src_blend       = BlendFactor::SrcAlpha;
    pd.dst_blend       = BlendFactor::OneMinusSrcAlpha;
    pd.blend_op        = BlendOp::Add;
    pd.depth_test      = false;
    pd.depth_write     = false;
    pd.color_formats[0] = PixelFormat::B8G8R8A8_SRGB;
    pd.color_count     = 1;
    pd.vertex_attr_count = 0;
    auto pr = bk.create_pipeline(pd);
    if (!pr) return;
    g_pipeline = *pr;
}

static void game_frame(void*, float dt, InputState&) {
    update_emitters(dt);

    // Camera shake
    float cam_x, cam_y, cam_angle;
    g_camera.get_offset(g_time, cam_x, cam_y, cam_angle);
    Mat4 proj = Mat4::ortho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
    Mat4 view = Mat4::translate(-cam_x, -cam_y);
    g_view_proj = proj * view;

    // Count active particles
    uint32_t active_count = 0;
    for (uint16_t i = 0; i < g_particles.count; ++i)
        if (g_particles.active[i]) ++active_count;

    if (active_count == 0) return;

    auto& bk = *g_backend;

    // Build instance data
    char arena_buf[256 * 1024];
    FrameArena arena(arena_buf, sizeof(arena_buf));
    auto* instances = arena.alloc_array<ParticleInstance>(active_count);
    if (!instances) return;

    uint32_t idx = 0;
    for (uint16_t i = 0; i < g_particles.count && idx < active_count; ++i) {
        if (!g_particles.active[i]) continue;
        auto& inst = instances[idx++];
        inst.px = g_particles.px[i];
        inst.py = g_particles.py[i];
        inst.scale = g_particles.scale[i];

        // Unpack RGBA8 → float4
        uint32_t c = g_particles.color[i];
        inst.r = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
        inst.g = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
        inst.b = static_cast<float>(c & 0xFF) / 255.0f;
        inst.atlas = 0.0f;  // atlas tile 0
        inst.rotation = g_particles.rotation[i];
        // Alpha from life ratio
        float life_ratio = g_particles.life[i] / g_particles.life_max[i];
        inst.alpha = life_ratio;
    }

    // Upload instance data
    uint32_t upload_size = active_count * sizeof(ParticleInstance);
    bk.update_buffer(g_instance_buf, instances, 0, upload_size);

    // Upload camera UBO
    bk.update_buffer(g_camera_ub, &g_view_proj, 0, sizeof(Mat4));

    // Upload atlas info (tile_size = 1×1 for white texture, atlas_size = 1×1)
    float atlas_data[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bk.update_buffer(g_atlas_ub, atlas_data, 0, sizeof(atlas_data));

    // Draw
    bk.bind_pipeline(g_pipeline);
    BufferHandle bufs[3] = {g_instance_buf, g_camera_ub, g_atlas_ub};
    uint32_t bindings[3] = {1, 2, 3};
    bk.bind_vertex_buffers(bufs, 3, nullptr, nullptr, bindings);
    bk.bind_fragment_texture(g_texture, 0);
    bk.bind_fragment_sampler(g_sampler, 0);
    bk.draw(4, active_count, 0, 0);
}

static void game_cleanup(void*) {
    auto& bk = *g_backend;
    bk.destroy_buffer(g_instance_buf);
    bk.destroy_buffer(g_camera_ub);
    bk.destroy_buffer(g_atlas_ub);
    bk.destroy_pipeline(g_pipeline);
    bk.destroy_texture(g_texture);
    bk.destroy_sampler(g_sampler);
}

AppCallbacks markmos_main(int, char**) {
    return {
        .user_data = nullptr,
        .init = game_init,
        .frame = game_frame,
        .cleanup = game_cleanup,
    };
}

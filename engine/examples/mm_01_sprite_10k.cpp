// 2D Stress Test — 10k animated sprites @ 60fps
// Tests: SpriteBatch SoA fill rate, vertex generation throughput, draw call batching

#include "../rhi/mm_rhi_concept.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../render/mm_shader_registry.hpp"
#include "../core/mm_arena.hpp"
#include "../game/mm_camera_trauma.hpp"
#include "../app/mm_app.hpp"
#if defined(USE_METAL_BACKEND)
#include "../rhi/mm_metal_backend.hpp"
#elif defined(USE_VULKAN_BACKEND)
#include "../rhi/mm_vulkan_backend.hpp"
#endif

struct Mat4 {
    float m[16];
    static Mat4 ortho_2d(float w, float h) noexcept {
        Mat4 mat{};
        mat.m[0]  = 2.0f / w;
        mat.m[5]  = -2.0f / h;
        mat.m[10] = -1.0f;
        mat.m[12] = -1.0f;
        mat.m[13] = 1.0f;
        mat.m[15] = 1.0f;
        return mat;
    }
};

static constexpr uint16_t SPRITE_COUNT = 10000;
static constexpr size_t   ARENA_SIZE   = 2 * 1024 * 1024;

static SpriteBatch   g_batch;
static BufferHandle  g_vb, g_ub;
static PipelineHandle g_pipeline;
static TextureHandle g_texture;
static SamplerHandle g_sampler;
alignas(64) static char g_arena_buf[ARENA_SIZE];
static FrameArena   g_arena;
static CameraTrauma g_camera;
static float        g_time;
static uint32_t     g_frame_count;

static void game_init(void*) {
    g_time = 0.0f;
    g_frame_count = 0;
    g_batch.init();

    // Spawn 10k sprites in a grid pattern
    uint16_t grid_side = 100;
    for (uint16_t i = 0; i < SPRITE_COUNT && i < MAX_SPRITES; ++i) {
        float x = static_cast<float>(i % grid_side) * 16.0f;
        float y = static_cast<float>(i / grid_side) * 16.0f;
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
    for (uint8_t i = 0; i < 3; ++i) pd.vertex_attrs[i] = va[i];
    pd.vertex_attr_count = 3;
    auto pr = bk.create_pipeline(pd);
    if (!pr) return;
    g_pipeline = *pr;

    TextureDesc td = { TextureType::Tex2D, PixelFormat::R8G8B8A8_UNORM, 1, 1, 1, 1, 1 };
    auto tr = bk.create_texture(td);
    if (!tr) return;
    g_texture = *tr;
    uint32_t white = 0xFFFFFFFF;
    bk.update_texture(g_texture, &white, 0, 0, 1, 1, 0, 0);

    SamplerDesc sd = { SamplerFilter::Nearest, SamplerFilter::Nearest, SamplerFilter::Nearest,
                       SamplerAddress::ClampToEdge, SamplerAddress::ClampToEdge, SamplerAddress::ClampToEdge,
                       CompareOp::Never, 1.0f };
    auto sr = bk.create_sampler(sd);
    if (!sr) return;
    g_sampler = *sr;

    g_arena.init(g_arena_buf, ARENA_SIZE);
}

static void game_frame(void*, float dt, InputState&) {
    g_time += dt;
    ++g_frame_count;

    // Camera shake every second
    if (g_frame_count % 60 == 0) g_camera.add_trauma(SHAKE_SMALL);
    g_camera.update(dt);

    // Animate sprites in a wave pattern
    for (uint16_t i = 0; i < g_batch.count; ++i) {
        if (!g_batch.active[i]) continue;
        float phase = g_time + static_cast<float>(i) * 0.01f;
        g_batch.world_x[i] += std::sin(phase) * 0.5f;
        g_batch.world_y[i] += std::cos(phase * 0.7f) * 0.5f;
    }

    auto& bk = *g_backend;
    g_arena.reset();

    float cx, cy, ca;
    g_camera.get_offset(g_time, cx, cy, ca);
    Mat4 cam = Mat4::ortho_2d(1170.0f, 2532.0f);
    bk.update_buffer(g_ub, &cam, 0, sizeof(Mat4));

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

AppCallbacks markmos_main(int, char**) {
    return {
        .user_data = nullptr,
        .init = game_init,
        .frame = game_frame,
        .cleanup = game_cleanup,
    };
}

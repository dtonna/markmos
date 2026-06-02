// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// 3D Stress Test — 100 animated cubes with perspective
// Tests: Vertex buffer update, MVP uniform, depth-less rasterization

#include "../rhi/mm_rhi_concept.hpp"
#include "../render/mm_shader_registry.hpp"
#include "../core/mm_handle.hpp"
#include "../app/mm_app.hpp"
#if defined(USE_METAL_BACKEND)
#include "../rhi/mm_metal_backend.hpp"
#elif defined(USE_VULKAN_BACKEND)
#include "../rhi/mm_vulkan_backend.hpp"
#endif

#include <cmath>
#include <cstring>

// Column-major float4x4
struct Mat4 {
    float m[16];

    static Mat4 identity() noexcept {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 perspective(float fov_rad, float aspect, float near, float far) noexcept {
        Mat4 r{};
        float f = 1.0f / std::tan(fov_rad * 0.5f);
        r.m[0]  = f / aspect;
        r.m[5]  = f;
        r.m[10] = far / (near - far);
        r.m[11] = -1.0f;
        r.m[14] = near * far / (near - far);
        return r;
    }

    static Mat4 look_at(float ex, float ey, float ez,
                        float tx, float ty, float tz,
                        float ux, float uy, float uz) noexcept {
        float fwd[3] = {tx - ex, ty - ey, tz - ez};
        float flen = std::sqrt(fwd[0]*fwd[0] + fwd[1]*fwd[1] + fwd[2]*fwd[2]);
        fwd[0] /= flen; fwd[1] /= flen; fwd[2] /= flen;

        float right[3];
        right[0] = fwd[1]*uz - fwd[2]*uy;
        right[1] = fwd[2]*ux - fwd[0]*uz;
        right[2] = fwd[0]*uy - fwd[1]*ux;
        float rlen = std::sqrt(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
        right[0] /= rlen; right[1] /= rlen; right[2] /= rlen;

        float up[3];
        up[0] = right[1]*fwd[2] - right[2]*fwd[1];
        up[1] = right[2]*fwd[0] - right[0]*fwd[2];
        up[2] = right[0]*fwd[1] - right[1]*fwd[0];

        Mat4 r{};
        r.m[0] = right[0]; r.m[4] = right[1]; r.m[8]  = right[2]; r.m[12] = -(right[0]*ex + right[1]*ey + right[2]*ez);
        r.m[1] = up[0];    r.m[5] = up[1];    r.m[9]  = up[2];    r.m[13] = -(up[0]*ex + up[1]*ey + up[2]*ez);
        r.m[2] = -fwd[0];  r.m[6] = -fwd[1];  r.m[10] = -fwd[2]; r.m[14] = (fwd[0]*ex + fwd[1]*ey + fwd[2]*ez);
        r.m[15] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& rhs) const noexcept {
        Mat4 r{};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += m[k*4 + j] * rhs.m[i*4 + k];
                r.m[i*4 + j] = sum;
            }
        }
        return r;
    }
};

// Per-vertex: float3 position + uchar4 color (stride = 16)
struct CubeVert {
    float    px, py, pz;
    uint32_t color;
};

static constexpr uint16_t CUBE_COUNT   = 100;   // 10×10 grid
static constexpr uint16_t VERTS_PER_CUBE = 36;   // 6 faces × 2 tris × 3 verts
static constexpr uint16_t TOTAL_VERTS  = CUBE_COUNT * VERTS_PER_CUBE;

// Unit cube vertex data (relative to center) + per-face colors
static CubeVert g_unit_verts[VERTS_PER_CUBE];
// Output buffer for all cubes (CPU side, uploaded each frame)
static CubeVert g_all_verts[TOTAL_VERTS];

// GPU handles
static BufferHandle  g_vb, g_ub;
static PipelineHandle g_pipeline;
static float g_time;
static float g_cube_angles[CUBE_COUNT];

// Unit cube vertices at origin (8 corners)
static const float CUBE_CORNERS[8][3] = {
    {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
    {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
};
// 6 faces, 6 verts each (2 tris), referencing corner indices
static const uint8_t CUBE_FACES[6][6] = {
    {0,1,2, 0,2,3},  // back  (-z)
    {5,4,7, 5,7,6},  // front (+z)
    {4,0,3, 4,3,7},  // left  (-x)
    {1,5,6, 1,6,2},  // right (+x)
    {3,2,6, 3,6,7},  // top   (+y)
    {4,5,1, 4,1,0},  // bottom(-y)
};
static const uint32_t FACE_COLORS[6] = {
    0xFFFF4444, 0xFF44FF44, 0xFF4488FF,
    0xFFFFFF44, 0xFFFF44FF, 0xFF44FFFF,
};

static void init_geo() noexcept {
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 6; ++v) {
            int ci = CUBE_FACES[f][v];
            int idx = f * 6 + v;
            g_unit_verts[idx].px = CUBE_CORNERS[ci][0];
            g_unit_verts[idx].py = CUBE_CORNERS[ci][1];
            g_unit_verts[idx].pz = CUBE_CORNERS[ci][2];
            g_unit_verts[idx].color = FACE_COLORS[f];
        }
    }

    for (uint16_t i = 0; i < CUBE_COUNT; ++i) {
        g_cube_angles[i] = static_cast<float>(i) * 0.3f;
    }
}

// Rotate cube verts around Y, translate by grid position
static void update_cubes(float dt) noexcept {
    g_time += dt;

    for (uint16_t ci = 0; ci < CUBE_COUNT; ++ci) {
        g_cube_angles[ci] += dt * 0.5f;
        float angle = g_cube_angles[ci];
        float ca = std::cos(angle), sa = std::sin(angle);

        int gx = ci % 10;
        int gy = ci / 10;
        float wx = (static_cast<float>(gx) - 4.5f) * 3.0f;
        float wy = (static_cast<float>(gy) - 4.5f) * 3.0f;
        float wz = 0.0f + std::sin(g_time * 1.5f + static_cast<float>(ci) * 0.2f) * 1.5f;

        for (int v = 0; v < VERTS_PER_CUBE; ++v) {
            const auto& src = g_unit_verts[v];
            float rx = src.px * ca - src.pz * sa;
            float rz = src.px * sa + src.pz * ca;
            int idx = ci * VERTS_PER_CUBE + v;
            g_all_verts[idx].px = rx + wx;
            g_all_verts[idx].py = src.py + wy;
            g_all_verts[idx].pz = rz + wz;
            g_all_verts[idx].color = src.color;
        }
    }
}

static void game_init(void*) {
    init_geo();
    g_time = 0.0f;

    auto& bk = *g_backend;

    BufferDesc vb_desc = {
        .type = BufferType::Vertex,
        .size = TOTAL_VERTS * sizeof(CubeVert),
        .stride = sizeof(CubeVert),
        .cpu_visible = true
    };
    auto vr = bk.create_buffer(vb_desc);
    if (!vr) return;
    g_vb = *vr;

    BufferDesc ub_desc = { .type = BufferType::Uniform, .size = sizeof(Mat4), .stride = 0, .cpu_visible = true };
    auto ur = bk.create_buffer(ub_desc);
    if (!ur) return;
    g_ub = *ur;

    auto vs = shader::cube_vertex();
    auto fs = shader::cube_fragment();
    VertexAttribute va[2] = {
        { 0, PixelFormat::R32G32B32_FLOAT,  0, 16 },
        { 1, PixelFormat::R8G8B8A8_UNORM,  12, 16 },
    };
    PipelineDesc pd = {};
    pd.vertex_shader     = vs;
    pd.fragment_shader   = fs;
    pd.prim_type         = PrimitiveType::Triangle;
    pd.cull_mode         = CullMode::Back;
    pd.src_blend         = BlendFactor::One;
    pd.dst_blend         = BlendFactor::Zero;
    pd.blend_op          = BlendOp::Add;
    pd.depth_test        = true;
    pd.depth_write       = true;
    pd.depth_compare     = CompareOp::Less;
    pd.color_formats[0]  = PixelFormat::B8G8R8A8_SRGB;
    pd.color_count       = 1;
    pd.depth_format      = PixelFormat::D32_FLOAT;
    pd.vertex_attrs[0]   = va[0];
    pd.vertex_attrs[1]   = va[1];
    pd.vertex_attr_count = 2;
    auto pr = bk.create_pipeline(pd);
    if (!pr) return;
    g_pipeline = *pr;
}

static void game_frame(void*, float dt, InputState&) {
    update_cubes(dt);

    auto& bk = *g_backend;

    // Upload vertex data
    bk.update_buffer(g_vb, g_all_verts, 0, TOTAL_VERTS * sizeof(CubeVert));

    // Camera: perspective looking at origin from slightly above
    float aspect = 1170.0f / 2532.0f;
    Mat4 proj = Mat4::perspective(0.8f, aspect, 0.1f, 100.0f);
    Mat4 view = Mat4::look_at(0.0f, -18.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Mat4 mvp = proj * view;
    bk.update_buffer(g_ub, &mvp, 0, sizeof(Mat4));

    // Draw
    bk.bind_pipeline(g_pipeline);
    BufferHandle bufs[2] = {g_vb, g_ub};
    uint32_t bindings[2] = {0, 2};
    bk.bind_vertex_buffers(bufs, 2, nullptr, nullptr, bindings);
    bk.draw(TOTAL_VERTS, 1, 0, 0);
}

static void game_cleanup(void*) {
    auto& bk = *g_backend;
    bk.destroy_buffer(g_vb);
    bk.destroy_buffer(g_ub);
    bk.destroy_pipeline(g_pipeline);
}

AppCallbacks markmos_main(int, char**) {
    return {
        .user_data = nullptr,
        .init = game_init,
        .frame = game_frame,
        .cleanup = game_cleanup,
    };
}

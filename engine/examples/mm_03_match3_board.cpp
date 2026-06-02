// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Match-3 Board Prototype — complete match-3 gameplay loop
// Tests: BoardGrid SoA match detection, gravity, swap logic
//         TweenPool for anim, ParticlePool for effects, InputState for input
// Cache expectation: match scan = 256-byte linear walk (L1), gravity compact = L1

#include "../game/mm_board_grid.hpp"
#include "../game/mm_game_state.hpp"
#include "../game/mm_tween_pool.hpp"
#include "../game/mm_particle_pool.hpp"
#include "../game/mm_text_popup_pool.hpp"
#include "../game/mm_camera_trauma.hpp"
#include "../input/mm_input_state.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../render/mm_shader_registry.hpp"
#include "../app/mm_app.hpp"
#if defined(USE_METAL_BACKEND)
#include "../rhi/mm_metal_backend.hpp"
#elif defined(USE_VULKAN_BACKEND)
#include "../rhi/mm_vulkan_backend.hpp"
#endif

// Column-major 4x4 matrix for CameraUBO
struct alignas(16) Mat4 {
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

struct Match3Game {
    // Core systems
    BoardGrid       board;
    GameState       state;
    StateStack      state_stack;
    TweenPool       tweens;
    ParticlePool    particles;
    TextPopupPool   text_popups;
    CameraTrauma    camera;
    SpriteBatch     sprite_batch;

    // GPU resources
    BufferHandle    vb;           // vertex buffer (cpu_visible)
    BufferHandle    ub;           // uniform buffer (CameraUBO)
    PipelineHandle  pipeline;     // sprite pipeline
    TextureHandle   texture;      // fallback 1x1 white texture
    SamplerHandle   sampler;      // nearest-clamp sampler

    // Frame arena (temp vertex storage for flush)
    static constexpr size_t ARENA_SIZE = 2 * 1024 * 1024;
    alignas(64)     char          arena_buf[ARENA_SIZE];
    FrameArena      arena;

    // Game state
    uint32_t        score;
    uint8_t         moves_left;
    uint8_t         selected_row, selected_col;
    bool            has_selection;
    float           time;

    void init(uint8_t cols, uint8_t rows, uint8_t types) noexcept {
        board.init(cols, rows, types, 64);
        state = PlayState{&board, 0, 1000, 30, 0, 60.0f};
        has_selection = false;
        score = 0;
        moves_left = 30;
        time = 0.0f;

        // Fill board with random tiles
        for (uint8_t r = 0; r < rows; ++r) {
            for (uint8_t c = 0; c < cols; ++c) {
                board.cell_type[board.idx(r, c)] =
                    static_cast<CellType>(1 + (r * 7 + c * 13) % types);
                board.cell_state[board.idx(r, c)] = CellState::Idle;
            }
        }

        // Remove any pre-existing matches
        uint16_t matches[BOARD_MAX];
        while (board.find_matches(3, matches) > 0) {
            for (uint8_t r = 0; r < rows; ++r) {
                for (uint8_t c = 0; c < cols; ++c) {
                    board.cell_type[board.idx(r, c)] =
                        static_cast<CellType>(1 + (r * 17 + c * 31 + static_cast<uint32_t>(time)) % types);
                    board.cell_state[board.idx(r, c)] = CellState::Idle;
                }
            }
        }
    }

    void update(float dt, InputState& input) noexcept {
        time += dt;

        // 1. Process game-facing actions from InputState
        for (uint8_t i = 0; i < input.action_count; ++i) {
            switch (input.actions[i]) {
                case InputAction::Select:
                    handle_tap(input.action_x, input.action_y);
                    break;
                case InputAction::SwapUp:
                    if (has_selection) handle_swipe_dir(-1, 0);
                    break;
                case InputAction::SwapDown:
                    if (has_selection) handle_swipe_dir(1, 0);
                    break;
                case InputAction::SwapLeft:
                    if (has_selection) handle_swipe_dir(0, -1);
                    break;
                case InputAction::SwapRight:
                    if (has_selection) handle_swipe_dir(0, 1);
                    break;
                case InputAction::Pause:
                    state = PauseState{};
                    break;
                default: break;
            }
        }

        // 2. Update game state
        auto* play = std::get_if<PlayState>(&state);
        if (play) {
            play->score = score;
            play->moves_left = moves_left;
        }

        // 3. Update effects
        tweens.update(dt);
        particles.update(dt);
        text_popups.update(dt);
        camera.update(dt);
    }

    void handle_tap(float x, float y) noexcept {
        uint8_t col = static_cast<uint8_t>(x / board.cell_size);
        uint8_t row = static_cast<uint8_t>(y / board.cell_size);
        if (!board.in_bounds(row, col)) return;

        if (!has_selection) {
            selected_row = row;
            selected_col = col;
            has_selection = true;
        } else {
            int8_t dr = static_cast<int8_t>(row) - static_cast<int8_t>(selected_row);
            int8_t dc = static_cast<int8_t>(col) - static_cast<int8_t>(selected_col);

            if ((std::abs(dr) + std::abs(dc)) == 1) {
                try_swap(selected_row, selected_col, row, col);
            }
            has_selection = false;
        }
    }

    void handle_swipe_dir(int8_t dr, int8_t dc) noexcept {
        uint8_t tr = static_cast<uint8_t>(static_cast<int8_t>(selected_row) + dr);
        uint8_t tc = static_cast<uint8_t>(static_cast<int8_t>(selected_col) + dc);
        if (board.in_bounds(tr, tc)) {
            try_swap(selected_row, selected_col, tr, tc);
        }
        has_selection = false;
    }

    void try_swap(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2) noexcept {
        if (!board.would_match(r1, c1, r2, c2)) {
            camera.add_trauma(SHAKE_SMALL);

            float* x1 = &sprite_batch.world_x[r1 * board.cols + c1];
            float* x2 = &sprite_batch.world_x[r2 * board.cols + c2];
            tweens.spawn(x1, *x1, *x1 + 4.0f, 0.15f, EaseType::SineInOut, 2);
            tweens.spawn(x2, *x2, *x2 - 4.0f, 0.15f, EaseType::SineInOut, 2);
            return;
        }

        board.swap(r1, c1, r2, c2);
        --moves_left;

        uint16_t matches[BOARD_MAX];
        uint8_t match_count = board.find_matches(3, matches);

        if (match_count > 0) {
            board.mark_matched(matches, match_count);

            for (uint8_t m = 0; m + 1 < match_count; m += 2) {
                uint16_t packed = matches[m];
                uint8_t r = static_cast<uint8_t>(packed >> 8);
                uint8_t c = static_cast<uint8_t>(packed & 0xFF);
                float px = c * board.cell_size + board.cell_size * 0.5f;
                float py = r * board.cell_size + board.cell_size * 0.5f;
                particles.spawn_burst(px, py, 12, 40, 100, 0.4f, 0xFFFFAA00);
                text_popups.spawn_score(px, py, 100, 0xFFFFFF00);
            }

            for (uint16_t i = 0; i < BOARD_MAX; ++i) {
                if (board.cell_state[i] == CellState::Matched) {
                    board.cell_type[i] = CellType::Empty;
                    board.cell_state[i] = CellState::Empty;
                }
            }

            board.apply_gravity();

            for (uint8_t c = 0; c < board.cols; ++c) {
                for (uint8_t r = 0; r < board.rows; ++r) {
                    if (board.cell_type[board.idx(r, c)] == CellType::Empty) {
                        board.cell_type[board.idx(r, c)] =
                            static_cast<CellType>(1 + (r * 13 + c * 7 + static_cast<uint8_t>(time)) % board.num_types);
                        board.cell_state[board.idx(r, c)] = CellState::Spawning;
                    }
                }
            }
        }
    }

    void render() noexcept {
        sprite_batch.reset();

        for (uint8_t r = 0; r < board.rows; ++r) {
            for (uint8_t c = 0; c < board.cols; ++c) {
                uint16_t i = board.idx(r, c);
                if (board.cell_type[i] == CellType::Empty) continue;

                float x = c * board.cell_size;
                float y = r * board.cell_size;
                float s = static_cast<float>(board.cell_size) - 2.0f;

                static constexpr uint32_t COLORS[] = {
                    0xFFFF4444, 0xFF4488FF, 0xFF44FF44,
                    0xFFFFFF44, 0xFFFF44FF, 0xFFFF8844
                };
                uint32_t color = COLORS[static_cast<uint8_t>(board.cell_type[i]) % 6];
                sprite_batch.add(x, y, s, s, 0.0f, color, LAYER_PIECES);
            }
        }
    }
};

// Sokol-style entry point
static Match3Game g_game;

static void game_init(void*) {
    g_game.init(8, 8, 6);

    // ── Create GPU resources ──────────────────────────────────────
    auto& bk = *g_backend;

    // Vertex buffer for sprite vertices (cpu_visible)
    BufferDesc vb_desc = {
        .type = BufferType::Vertex,
        .size = MAX_VERTS * sizeof(SpriteVertex),
        .stride = sizeof(SpriteVertex),
        .cpu_visible = true
    };
    auto vb_res = bk.create_buffer(vb_desc);
    if (!vb_res) return;
    g_game.vb = *vb_res;

    // Uniform buffer for CameraUBO (float4x4 = 64 bytes)
    BufferDesc ub_desc = {
        .type = BufferType::Uniform,
        .size = 64,
        .stride = 0,
        .cpu_visible = true
    };
    auto ub_res = bk.create_buffer(ub_desc);
    if (!ub_res) return;
    g_game.ub = *ub_res;

    // Pipeline: sprite vertex + fragment shaders
    auto vs = shader::sprite_vertex();
    auto fs = shader::sprite_fragment();
    VertexAttribute vert_attrs[3] = {
        { 0, PixelFormat::R16G16_FLOAT,    0,  12 },
        { 1, PixelFormat::R16G16_FLOAT,    4,  12 },
        { 2, PixelFormat::R8G8B8A8_UNORM,  8,  12 },
    };
    PipelineDesc pd = {};
    pd.vertex_shader   = vs;
    pd.fragment_shader = fs;
    pd.prim_type       = PrimitiveType::Triangle;
    pd.cull_mode       = CullMode::None;
    pd.src_blend       = BlendFactor::SrcAlpha;
    pd.dst_blend       = BlendFactor::OneMinusSrcAlpha;
    pd.blend_op        = BlendOp::Add;
    pd.color_formats[0]= PixelFormat::B8G8R8A8_SRGB;
    pd.color_count     = 1;
    pd.vertex_attrs[0] = vert_attrs[0];
    pd.vertex_attrs[1] = vert_attrs[1];
    pd.vertex_attrs[2] = vert_attrs[2];
    pd.vertex_attr_count = 3;
    auto pipe_res = bk.create_pipeline(pd);
    if (!pipe_res) return;
    g_game.pipeline = *pipe_res;

    // White 1x1 fallback texture
    TextureDesc td = { TextureType::Tex2D, PixelFormat::R8G8B8A8_UNORM,
                       1, 1, 1, 1, 1 };
    auto tex_res = bk.create_texture(td);
    if (!tex_res) return;
    g_game.texture = *tex_res;
    uint32_t white_pixel = 0xFFFFFFFF;
    bk.update_texture(g_game.texture, &white_pixel, 0, 0, 1, 1, 0, 0);

    // Nearest-clamp sampler
    SamplerDesc sd = { SamplerFilter::Nearest, SamplerFilter::Nearest,
                       SamplerFilter::Nearest,
                       SamplerAddress::ClampToEdge, SamplerAddress::ClampToEdge,
                       SamplerAddress::ClampToEdge,
                       CompareOp::Never, 1.0f };
    auto samp_res = bk.create_sampler(sd);
    if (!samp_res) return;
    g_game.sampler = *samp_res;

    // Frame arena (2 MB backing buffer)
    g_game.arena.init(g_game.arena_buf, Match3Game::ARENA_SIZE);
}

static void game_frame(void*, float dt, InputState& input) {
    auto& bk = *g_backend;

    g_game.update(dt, input);
    g_game.render();

    // Arena reset and camera upload
    g_game.arena.reset();

    // Orthographic camera mapping pixel coords → NDC
    Mat4 cam = Mat4::ortho_2d(1170.0f, 2532.0f);
    bk.update_buffer(g_game.ub, &cam, 0, sizeof(Mat4));

    // Flush sprites to GPU
    g_game.sprite_batch.flush(bk, g_game.vb, g_game.ub, g_game.pipeline,
                              g_game.texture, g_game.sampler, g_game.arena);
}

static void game_cleanup(void*) {
    auto& bk = *g_backend;
    bk.destroy_buffer(g_game.vb);
    bk.destroy_buffer(g_game.ub);
    bk.destroy_pipeline(g_game.pipeline);
    bk.destroy_texture(g_game.texture);
    bk.destroy_sampler(g_game.sampler);
}

AppCallbacks markmos_main(int, char**) {
    return {
        .user_data = nullptr,
        .init = game_init,
        .frame = game_frame,
        .cleanup = game_cleanup,
    };
}

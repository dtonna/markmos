// Block Puzzle Prototype — Tetris/block-dropping gameplay
// Tests: BoardGrid gravity, piece collision, row clear
//         TweenPool slide anim, ParticlePool clear effects

#include "../game/mm_board_grid.hpp"
#include "../game/mm_game_state.hpp"
#include "../game/mm_tween_pool.hpp"
#include "../game/mm_particle_pool.hpp"
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

// Tetromino shapes (4 rotation states each)
static constexpr uint16_t TETRO_I[4] = {
    0b0000111100000000, 0b0010001000100010,
    0b0000111100000000, 0b0010001000100010
};
static constexpr uint16_t TETRO_O[4] = { 0b0110011000000000, 0b0110011000000000, 0b0110011000000000, 0b0110011000000000 };
static constexpr uint16_t TETRO_T[4] = {
    0b0100011100000000, 0b0100011001000000,
    0b0000111001000000, 0b0100110001000000
};
static constexpr uint16_t TETRO_S[4] = {
    0b0110110000000000, 0b0100011000100000,
    0b0000011011000000, 0b1000110001000000
};
static constexpr uint16_t TETRO_Z[4] = {
    0b1100011000000000, 0b0010011001000000,
    0b0000110001100000, 0b0100110010000000
};
static constexpr uint16_t TETRO_L[4] = {
    0b0100010001100000, 0b0000111010000000,
    0b0110010001000000, 0b0010111000000000
};
static constexpr uint16_t TETRO_J[4] = {
    0b0110010001000000, 0b1000111000000000,
    0b0100010001100000, 0b0000111000100000
};

static constexpr uint32_t PIECE_COLORS[] = {
    0xFFFF4444, 0xFF4488FF, 0xFF44FF44, 0xFFFFFF44,
    0xFFFF44FF, 0xFFFF8844, 0xFF44FFFF
};

struct BlockPuzzle {
    BoardGrid       board;
    GameState       state;
    TweenPool       tweens;
    ParticlePool    particles;
    CameraTrauma    camera;
    SpriteBatch     sprite_batch;

    // GPU resources
    BufferHandle    vb;
    BufferHandle    ub;
    PipelineHandle  pipeline;
    TextureHandle   texture;
    SamplerHandle   sampler;
    static constexpr size_t ARENA_SIZE = 2 * 1024 * 1024;
    alignas(64)     char          arena_buf[ARENA_SIZE];
    FrameArena      arena;

    // Current falling piece
    uint16_t        piece_shape[4];
    uint8_t         piece_rotation;
    int8_t          piece_x, piece_y;
    uint8_t         piece_type;
    bool            piece_active;

    // Game state
    uint32_t        score;
    uint32_t        lines_cleared;
    uint8_t         level;
    float           drop_timer;
    float           drop_interval;

    // Next piece preview
    uint8_t         next_piece_type;

    void init(uint8_t cols, uint8_t rows) noexcept {
        board.init(cols, rows, 7, 48);
        state = PlayState{&board, 0, 5000, 0, 0, 0.0f};
        score = 0;
        level = 1;
        drop_interval = 1.0f;
        drop_timer = 0.0f;
        lines_cleared = 0;
        piece_active = false;

        spawn_piece();
    }

    void spawn_piece() noexcept {
        piece_type = static_cast<uint8_t>(rand() % 7);
        piece_rotation = 0;
        piece_x = static_cast<int8_t>(board.cols / 2 - 2);
        piece_y = 0;

        constexpr const uint16_t* SHAPES[7] = {
            TETRO_I, TETRO_O, TETRO_T, TETRO_S, TETRO_Z, TETRO_L, TETRO_J
        };
        memcpy(piece_shape, SHAPES[piece_type], sizeof(piece_shape));

        if (!check_collision(piece_x, piece_y, piece_shape[0])) {
            state = FailState{0, 0.0f, 2.0f};
            piece_active = false;
        } else {
            piece_active = true;
        }
        piece_active = true;
    }

    bool check_collision(int8_t x, int8_t y, uint16_t shape) noexcept {
        for (int8_t r = 0; r < 4; ++r) {
            for (int8_t c = 0; c < 4; ++c) {
                if (!(shape & (1 << (15 - r * 4 - c)))) continue;
                int8_t bx = x + c;
                int8_t by = y + r;
                if (bx < 0 || bx >= board.cols || by >= board.rows) return false;
                if (by >= 0 && board.cell_type[board.idx(static_cast<uint8_t>(by), static_cast<uint8_t>(bx))] != CellType::Empty) {
                    return false;
                }
            }
        }
        return true;
    }

    void lock_piece() noexcept {
        if (!piece_active) return;
        for (int8_t r = 0; r < 4; ++r) {
            for (int8_t c = 0; c < 4; ++c) {
                if (!(piece_shape[piece_rotation] & (1 << (15 - r * 4 - c)))) continue;
                int8_t bx = piece_x + c;
                int8_t by = piece_y + r;
                if (by >= 0 && by < board.rows && bx >= 0 && bx < board.cols) {
                    board.cell_type[board.idx(static_cast<uint8_t>(by), static_cast<uint8_t>(bx))] =
                        static_cast<CellType>(piece_type + 1);
                    board.cell_state[board.idx(static_cast<uint8_t>(by), static_cast<uint8_t>(bx))] = CellState::Idle;
                }
            }
        }
        piece_active = false;

        clear_rows();
        spawn_piece();
    }

    uint8_t clear_rows() noexcept {
        uint8_t cleared = 0;
        for (int8_t r = board.rows - 1; r >= 0; --r) {
            bool full = true;
            for (uint8_t c = 0; c < board.cols; ++c) {
                if (board.cell_type[board.idx(static_cast<uint8_t>(r), c)] == CellType::Empty) {
                    full = false;
                    break;
                }
            }
            if (full) {
                for (uint8_t c = 0; c < board.cols; ++c) {
                    uint16_t i = board.idx(static_cast<uint8_t>(r), c);
                    float px = c * board.cell_size;
                    float py = r * board.cell_size;
                    particles.spawn_burst(px + board.cell_size * 0.5f, py + board.cell_size * 0.5f,
                                          4, 30, 80, 0.3f, 0xFFFFFFFF);
                    board.cell_type[i] = CellType::Empty;
                    board.cell_state[i] = CellState::Empty;
                }
                ++cleared;
                for (int8_t r2 = r - 1; r2 >= 0; --r2) {
                    for (uint8_t c = 0; c < board.cols; ++c) {
                        uint16_t src = board.idx(static_cast<uint8_t>(r2), c);
                        uint16_t dst = board.idx(static_cast<uint8_t>(r2 + 1), c);
                        board.cell_type[dst] = board.cell_type[src];
                        board.cell_state[dst] = board.cell_state[src];
                        board.cell_type[src] = CellType::Empty;
                        board.cell_state[src] = CellState::Empty;
                    }
                }
                ++r;
            }
        }

        if (cleared > 0) {
            camera.add_trauma(SHAKE_MEDIUM);
            lines_cleared += cleared;
            static constexpr uint32_t ROW_SCORES[] = {0, 100, 300, 500, 800};
            score += ROW_SCORES[cleared] * level;
            level = static_cast<uint8_t>(1 + lines_cleared / 10);
            drop_interval = std::max(0.1f, 1.0f - (level - 1) * 0.08f);
        }

        return cleared;
    }

    void update(float dt, InputState& input) noexcept {
        if (!piece_active) return;

        drop_timer += dt;
        if (drop_timer >= drop_interval) {
            drop_timer = 0.0f;
            if (check_collision(piece_x, piece_y + 1, piece_shape[piece_rotation])) {
                ++piece_y;
            } else {
                lock_piece();
                return;
            }
        }

        for (uint8_t i = 0; i < input.action_count; ++i) {
            switch (input.actions[i]) {
                case InputAction::Select:
                    while (check_collision(piece_x, piece_y + 1, piece_shape[piece_rotation])) {
                        ++piece_y;
                    }
                    lock_piece();
                    camera.add_trauma(SHAKE_SMALL);
                    break;
                case InputAction::SwapLeft:
                    if (check_collision(piece_x - 1, piece_y, piece_shape[piece_rotation])) {
                        --piece_x;
                    }
                    break;
                case InputAction::SwapRight:
                    if (check_collision(piece_x + 1, piece_y, piece_shape[piece_rotation])) {
                        ++piece_x;
                    }
                    break;
                case InputAction::SwapUp:
                {
                    uint8_t next_rot = (piece_rotation + 1) % 4;
                    if (check_collision(piece_x, piece_y, piece_shape[next_rot])) {
                        piece_rotation = next_rot;
                    }
                    break;
                }
                default: break;
            }
        }

        tweens.update(dt);
        particles.update(dt);
        camera.update(dt);
    }

    void render() noexcept {
        sprite_batch.reset();

        float s = static_cast<float>(board.cell_size) - 2.0f;

        for (uint8_t r = 0; r < board.rows; ++r) {
            for (uint8_t c = 0; c < board.cols; ++c) {
                uint16_t i = board.idx(r, c);
                CellType t = board.cell_type[i];
                if (t == CellType::Empty) continue;

                float x = c * board.cell_size;
                float y = r * board.cell_size;
                uint32_t color = PIECE_COLORS[static_cast<uint8_t>(t) % 7];
                sprite_batch.add(x, y, s, s, 0.0f, color, 1);
            }
        }

        if (piece_active) {
            uint32_t pc = PIECE_COLORS[piece_type % 7];
            for (int8_t r = 0; r < 4; ++r) {
                for (int8_t c = 0; c < 4; ++c) {
                    if (!(piece_shape[piece_rotation] & (1 << (15 - r * 4 - c)))) continue;
                    int8_t bx = piece_x + c;
                    int8_t by = piece_y + r;
                    if (by < 0) continue;
                    float x = bx * board.cell_size;
                    float y = by * board.cell_size;
                    sprite_batch.add(x, y, s, s, 0.0f, pc, 2);
                }
            }
        }
    }
};

// Sokol-style entry point
static BlockPuzzle g_game;

static void game_init(void*) {
    g_game.init(10, 20);

    auto& bk = *g_backend;

    BufferDesc vb_desc = {
        .type = BufferType::Vertex,
        .size = MAX_VERTS * sizeof(SpriteVertex),
        .stride = sizeof(SpriteVertex),
        .cpu_visible = true
    };
    auto vb_res = bk.create_buffer(vb_desc);
    if (!vb_res) return;
    g_game.vb = *vb_res;

    BufferDesc ub_desc = { .type = BufferType::Uniform, .size = 64, .stride = 0, .cpu_visible = true };
    auto ub_res = bk.create_buffer(ub_desc);
    if (!ub_res) return;
    g_game.ub = *ub_res;

    auto vs = shader::sprite_vertex();
    auto fs = shader::sprite_fragment();
    VertexAttribute vert_attrs[3] = {
        { 0, PixelFormat::R16G16_FLOAT,    0,  12 },
        { 1, PixelFormat::R16G16_FLOAT,    4,  12 },
        { 2, PixelFormat::R8G8B8A8_UNORM,  8,  12 },
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
    pd.vertex_attrs[0]   = vert_attrs[0];
    pd.vertex_attrs[1]   = vert_attrs[1];
    pd.vertex_attrs[2]   = vert_attrs[2];
    pd.vertex_attr_count = 3;
    auto pipe_res = bk.create_pipeline(pd);
    if (!pipe_res) return;
    g_game.pipeline = *pipe_res;

    TextureDesc td = { TextureType::Tex2D, PixelFormat::R8G8B8A8_UNORM, 1, 1, 1, 1, 1 };
    auto tex_res = bk.create_texture(td);
    if (!tex_res) return;
    g_game.texture = *tex_res;
    uint32_t white_pixel = 0xFFFFFFFF;
    bk.update_texture(g_game.texture, &white_pixel, 0, 0, 1, 1, 0, 0);

    SamplerDesc sd = { SamplerFilter::Nearest, SamplerFilter::Nearest, SamplerFilter::Nearest,
                       SamplerAddress::ClampToEdge, SamplerAddress::ClampToEdge, SamplerAddress::ClampToEdge,
                       CompareOp::Never, 1.0f };
    auto samp_res = bk.create_sampler(sd);
    if (!samp_res) return;
    g_game.sampler = *samp_res;

    g_game.arena.init(g_game.arena_buf, BlockPuzzle::ARENA_SIZE);
}

static void game_frame(void*, float dt, InputState& input) {
    auto& bk = *g_backend;

    g_game.update(dt, input);
    g_game.render();

    g_game.arena.reset();

    Mat4 cam = Mat4::ortho_2d(1170.0f, 2532.0f);
    bk.update_buffer(g_game.ub, &cam, 0, sizeof(Mat4));

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

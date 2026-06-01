// Production Game Entry — revised architecture version
// Improvements:
// - removed hidden sparse leaks
// - reusable block slots
// - safer scene destruction
// - split update/render stages
// - removed duplicated UI clear
// - safer game_over()
// - reduced future maintenance risk
// - deterministic-friendly structure

#include "../app/mm_app.hpp"
#include "../audio/mm_audio_system.hpp"
#include "../game/mm_camera_trauma.hpp"
#include "../game/mm_particle_pool.hpp"
#include "../game/mm_scene.hpp"
#include "../game/mm_text_popup_pool.hpp"
#include "../game/mm_tween_pool.hpp"
#include "../math/mm_color.h"
#include "../render/mm_renderer.hpp"
#include "../render/mm_sprite.hpp"
#include "../ui/mm_ui.hpp"

#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────

static constexpr uint16_t MAX_BLOCKS    = 256;
static constexpr uint16_t MAX_STARS     = 32;

static constexpr float    GRAVITY       = 300.0f;
static constexpr float    COMBO_TIMEOUT = 1.5f;

// ─────────────────────────────────────────────────────────────

enum class GameState : uint8_t {
    Playing,
    GameOver,
};

// ─────────────────────────────────────────────────────────────

struct Block {

    float    x, y;
    float    w, h;

    uint32_t color;

    float    spawn_scale;

    bool     active;
};

static_assert(sizeof(Block) <= 32);

// ─────────────────────────────────────────────────────────────

struct Game {

    Renderer      renderer;
    SpriteBatch   batch;

    CameraTrauma  camera;
    TweenPool     tweens;
    TextPopupPool popups;
    ParticlePool  particles;

    Scene         scene;

    SpriteAtlas   star_atlas;
    TextureHandle star_tex;
    SamplerHandle star_sampler;

    ui::Manager   ui;

    Block         blocks[MAX_BLOCKS];

    uint16_t      active_blocks = 0;

    float         drop_timer    = 0.0f;
    float         drop_interval = 0.8f;

    float         time          = 0.0f;

    float         combo_timer   = 0.0f;

    float         tap_cooldown  = 0.0f;
    float         hit_cooldown  = 0.0f;

    float         restart_timer = 0.0f;

    uint32_t      score         = 0;
    uint32_t      high_score    = 0;

    int           combo         = 0;

    bool          started       = false;
    bool          ui_built      = false;
    bool          hud_built     = false;
    bool          sound_on      = true;
    bool          particles_on  = true;

    GameState     state         = GameState::Playing;

    uint8_t       sfx_hit_id    = 0;
    uint8_t       sfx_score_id  = 0;
    uint8_t       sfx_tap_id    = 0;

    // HUD widget IDs (for live text updates)
    uint16_t      hud_id_score  = UINT16_MAX;
    uint16_t      hud_id_combo  = UINT16_MAX;

    // deferred destroy list
    uint16_t      destroy_queue[MAX_ENTITIES];
    uint16_t      destroy_count = 0;
};

static Game g_game;

// ─────────────────────────────────────────────────────────────

static void play_sfx(uint8_t id, int priority, float base, float range) noexcept {

    float pitch = base + static_cast<float>(std::rand() % static_cast<int>(range * 100.0f + 0.5f)) / 100.0f;

    g_audio_system.sfx.play_id(id, static_cast<uint8_t>(priority), pitch);
}

// ─────────────────────────────────────────────────────────────

static void reset_game() noexcept {

    auto &g         = g_game;

    g.active_blocks = 0;

    for (auto &b : g.blocks) {
        b.active = false;
    }

    g.score         = 0;
    g.combo         = 0;
    g.combo_timer   = 0.0f;

    g.drop_timer    = 1.0f;
    g.drop_interval = 0.8f;

    g.time          = 0.0f;

    g.started       = false;
    g.ui_built      = false;

    g.state         = GameState::Playing;

    g.tap_cooldown  = 0.0f;
    g.hit_cooldown  = 0.0f;

    g.restart_timer = 0.0f;

    g.batch.reset();

    g.tweens.reset();
    g.popups.reset();

    g.scene.init();

    g.camera       = CameraTrauma{};
    g.camera.decay = 0.92f;

    g.ui.init();
    g.ui.theme = ui::Theme::load("test_theme.json");
}

// ─────────────────────────────────────────────────────────────

static Block *alloc_block() noexcept {

    auto &g = g_game;

    for (auto &b : g.blocks) {

        if (!b.active) {

            b.active = true;

            ++g.active_blocks;

            return &b;
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────

static void spawn_block() noexcept {

    auto *b = alloc_block();

    if (!b) {
        return;
    }

    float max_x = static_cast<float>(g_game.renderer.width) - 80.0f;
    b->x        = max_x > 80.0f ? static_cast<float>(std::rand() % static_cast<int>(max_x - 80.0f + 1.0f) + 40) : 40.0f;
    b->y        = -100.0f;

    b->w        = 80.0f;
    b->h        = 80.0f;

    using mm_math::color;

    static const color colors[] = {
        color(1.0f, 0.267f, 0.267f),
        color(0.267f, 1.0f, 0.267f),
        color(0.267f, 0.267f, 1.0f),
        color(1.0f, 1.0f, 0.267f),
    };

    b->color       = colors[std::rand() % 4].to_u32_bgra();

    b->spawn_scale = 0.0f;

    g_game.tweens.spawn(&b->spawn_scale, 0.0f, 1.0f, 0.3f, EaseType::BackOut);
}

// ─────────────────────────────────────────────────────────────

static void game_over() noexcept {

    auto &g = g_game;

    if (g.state == GameState::GameOver) {
        return;
    }

    g.state         = GameState::GameOver;
    g.ui_built      = false;
    g.hud_built     = false;

    g.restart_timer = 0.6f;

    if (g.score > g.high_score) {
        g.high_score = g.score;
    }

    play_sfx(g.sfx_score_id, 200, 0.8f, 0.1f);
}

// ─────────────────────────────────────────────────────────────

static void hit_block(Block &b) noexcept {

    auto &g  = g_game;

    b.active = false;

    --g.active_blocks;

    ++g.combo;

    g.combo_timer  = COMBO_TIMEOUT;

    g.score       += g.combo;

    float cx       = b.x + b.w * 0.5f;
    float cy       = b.y + b.h * 0.5f;

    if (g.particles_on) {
        g.particles.spawn_burst(cx, cy, 10, 60, 160, 0.5f, b.color);
    }

    g.popups.spawn_score(cx, b.y, g.combo, 0xFF44FF88);

    g.camera.add_trauma(SHAKE_SMALL);

    if (g.hit_cooldown <= 0.0f) {

        g.hit_cooldown = 0.08f;

        play_sfx(g.sfx_hit_id, 100, 0.9f, 0.2f);
    }
}

// ─────────────────────────────────────────────────────────────

static void update_blocks(float dt) noexcept {

    auto &g       = g_game;

    g.drop_timer -= dt;

    if (g.drop_timer <= 0.0f) {

        g.drop_timer = g.drop_interval;

        spawn_block();

        if (g.drop_interval > 0.2f) {
            g.drop_interval -= 0.003f;
        }
    }

    for (auto &b : g.blocks) {

        if (!b.active) {
            continue;
        }

        b.y += dt * GRAVITY;

        if (b.y > (float)g_game.renderer.height + 100.0f) {

            game_over();

            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────

static void update_scene(float dt) noexcept {

    auto &g         = g_game;

    g.destroy_count = 0;

    g.scene.each([dt](uint16_t idx) {
        float rs;

        std::memcpy(&rs, &g_game.scene.user_data[idx], sizeof(rs));

        g_game.scene.rotation[idx] += dt * rs;

        g_game.scene.y[idx]        -= dt * 40.0f;

        if (g_game.scene.y[idx] < -80.0f) {

            g_game.destroy_queue[g_game.destroy_count++] = idx;
        }
    });

    for (uint16_t i = 0; i < g.destroy_count; ++i) {

        g.scene.despawn_at(g.destroy_queue[i]);
    }
}

// ─────────────────────────────────────────────────────────────

static void render_world() noexcept {

    auto &g = g_game;
    auto &r = g.renderer;

    g.batch.reset();

    for (auto &b : g.blocks) {

        if (!b.active) {
            continue;
        }

        float sw = b.w * b.spawn_scale;
        float sh = b.h * b.spawn_scale;

        float sx = b.x + (b.w - sw) * 0.5f;

        float sy = b.y + (b.h - sh) * 0.5f;

        g.batch.add(sx, sy, sw, sh, 0.0f, b.color, 0);
    }

    r.flush_sprites(g.batch);
}

// ─────────────────────────────────────────────────────────────

// ─── UI callbacks ──────────────────────────────────────────────────
static void on_play_click(uint16_t) {
    g_game.started = true;
    g_game.state   = GameState::Playing;
    g_game.ui.clear();
}

static void on_toggle_sound(uint16_t) {
    g_game.sound_on = !g_game.sound_on;
    g_audio_system.sfx.set_master_volume(g_game.sound_on ? 1.0f : 0.0f);
}

static void on_checkbox_part(uint16_t) { g_game.particles_on = !g_game.particles_on; }

static void on_slider_volume(uint16_t, float val) { g_audio_system.sfx.set_master_volume(val); }

static void on_quit_click(uint16_t) {
    auto &g = g_game;
    reset_game();
    g.ui.clear();
    g.ui_built  = false;
    g.hud_built = false;
    g.started   = false;
    g.state     = GameState::Playing;
}

static void on_restart_click(uint16_t) {
    auto &g = g_game;
    reset_game();
    g.ui.clear();
    g.ui_built  = false;
    g.hud_built = false;
    g.started   = true;
    g.state     = GameState::Playing;
}

// ─── In-game HUD ─────────────────────────────────────────────────
static void build_hud() noexcept {
    auto &g  = g_game;
    auto &m  = g.ui;

    float cw = static_cast<float>(g.renderer.width);

    // Score label (top-left)
    m.label(20.0f, 10.0f, "Score: 0", 0xFFFFFFFF, 1.2f);
    g.hud_id_score = g.ui.count - 1;

    // Combo label (center)
    m.label(cw * 0.5f - 40.0f, 10.0f, "", 0xFF88FF88, 1.2f);
    g.hud_id_combo = g.ui.count - 1;

    // High score (top-right)
    char buf[48];
    int  len = snprintf(buf, sizeof(buf), "Best: %u", g.high_score);
    if (len > 0) {
        float tw = static_cast<float>(len) * 12.0f;
        m.label(cw - tw - 20.0f, 10.0f, buf, 0xFFAAAAAA, 1.0f);
    }

    // Quit button (bottom-right)
    m.button(cw - 80.0f, 10.0f, 70.0f, 30.0f, "Quit", 0x55333333, 0xFFCCCCCC, on_quit_click);
}

// ─── Game Over screen ─────────────────────────────────────────────
static void build_game_over() noexcept {
    auto &g  = g_game;
    auto &m  = g.ui;

    float cw = static_cast<float>(g.renderer.width);
    float ch = static_cast<float>(g.renderer.height);
    float cx = cw * 0.5f;
    float cy = ch * 0.5f;

    m.label(cx - 80.0f, cy - 60.0f, "Game Over!", 0xFFFF4444, 2.0f);

    char buf[64];
    int  len = snprintf(buf, sizeof(buf), "Score: %u  |  Best: %u", g.score, g.high_score);
    if (len > 0) {
        m.label(cx - 120.0f, cy - 10.0f, buf, 0xFFFFFFFF, 1.2f);
    }

    uint16_t btn_restart = m.button(cx - 95.0f, cy + 40.0f, 190.0f, 44.0f, "Restart", 0xFF4488FF, 0xFFFFFFFF, on_restart_click);
    m.pool[btn_restart].scale = 1.0f;
    uint16_t btn_menu = m.button(cx - 120.0f, cy + 100.0f, 240.0f, 44.0f, "Main Menu", 0xFF554466, 0xFFFFFFFF, on_quit_click);
    m.pool[btn_menu].scale = 1.0f;
}

// ─── Title screen ─────────────────────────────────────────────────
static void build_ui_demo() noexcept {
    auto &g   = g_game;
    auto &m   = g.ui;

    float cx  = 160.0f;
    float cy  = 60.0f;
    float gap = 50.0f;

    // Title
    m.label(cx, cy, "Markmos UI Demo", 0xFFFFAAFF, 1.5f);
    cy += 50.0f;
    // Thai font test
    m.label(cx, cy + 28.0f, "ภาษาไทย 123", 0xFF88FF88, 1.0f);
    cy += 50.0f;

    // Button
    uint16_t btn_play = m.button(cx, cy, 240.0f, 50.0f, "Play Game", 0xFF4488FF, 0xFFFFFFFF, on_play_click);
    m.pool[btn_play].scale = 1.0f;
    cy += gap + 4.0f;

    // Toggle (switch)
    m.toggle(cx, cy, 48.0f, 28.0f, "", 0xFF44FF44, 0xFF444444, 0, false, on_toggle_sound);
    m.label(cx + 56.0f, cy + 27.0f, "Sound ON/OFF", 0xFFCCCCCC, 1.0f);
    cy += gap;

    // Checkbox
    m.checkbox(cx, cy, "Enable Particles", 0xFF44FF88, 0x33444444, 0xFFCCCCCC, true, on_checkbox_part);
    cy += gap;

    // Slider
    m.label(cx, cy + 14.0f, "Volume", 0xFFAAAAAA, 1.0f);
    cy += 26.0f;
    m.slider(cx, cy, 200.0f, 28.0f, 0.75f, on_slider_volume);
    cy += gap;

    // TextField
    m.label(cx, cy + 14.0f, "Player Name", 0xFFAAAAAA, 1.0f);
    cy += 34.0f;
    uint16_t txt_name = m.textfield(cx, cy, 220.0f, 32.0f, "Player1", 0x33555555, 0xFFFFFFFF);
    m.pool[txt_name].scale = 0.6f;
    cy += gap + 26.0f;

    // Buttons row: layout demo
    float    bx         = cx;
    m.label(bx, cy - 14.0f, "Layout:", 0xFF888888, 1.0f);
    uint16_t btn_panel  = m.panel(bx, cy, 440.0f, 50.0f, 0x22444444);
    m.set_layout(btn_panel, 1, 8, 8);
    m.button(0, 0, 100.0f, 34.0f, "One", 0xFF554466, 0xFFFFFFFF, nullptr, btn_panel);
    m.button(0, 0, 100.0f, 34.0f, "Two", 0xFF665577, 0xFFFFFFFF, nullptr, btn_panel);
    m.button(0, 0, 125.0f, 34.0f, "Three", 0xFF776688, 0xFFFFFFFF, nullptr, btn_panel);
    m.layout(g.renderer);
}

static void game_frame(void *, float dt, InputState &input) {

    auto &g         = g_game;

    g.time         += dt;

    g.tap_cooldown -= dt;
    g.hit_cooldown -= dt;
    if (g.combo_timer > 0.0f) {
        g.combo_timer -= dt;
        if (g.combo_timer <= 0.0f) {
            g.combo = 0;
        }
    }

    if (g.state == GameState::GameOver) {
        if (!g.ui_built) {
            build_game_over();
            g.ui_built  = true;
            g.hud_built = false;
        }
        g.ui.handle(input);
    } else if (!g.started) {
        if (!g.ui_built) {
            build_ui_demo();
            g.ui_built = true;
        }
        g.ui.handle(input);
    } else {
        // Playing — build or rebuild HUD each frame so score/combo update
        if (!g.hud_built) {
            g.ui.clear();
            build_hud();
            g.hud_built = true;
        }
        g.ui.handle(input);

        // Tap/click to hit blocks (only when no widget was clicked)
        if (g.tap_cooldown <= 0.0f && g.ui.clicked == UINT16_MAX) {
            for (uint8_t a = 0; a < input.action_count; ++a) {
                if (input.actions[a] == InputAction::Select) {
                    float tx = input.action_x;
                    float ty = input.action_y;
                    for (auto &b : g.blocks) {
                        if (!b.active) {
                            continue;
                        }
                        if (tx >= b.x && tx <= b.x + b.w && ty >= b.y && ty <= b.y + b.h) {
                            hit_block(b);
                            g.tap_cooldown = 0.15f;
                            break;
                        }
                    }
                    break;
                }
            }
        }

        update_blocks(dt);
        update_scene(dt);

        g.tweens.update(dt);

        if (g.particles_on) {
            g.particles.update(dt, 0.0f, 400.0f);
        }

        g.popups.update(dt);
        g.camera.update(dt);

        // Update HUD text live
        if (g.hud_id_score != UINT16_MAX) {
            char buf[32];
            int  len = snprintf(buf, sizeof(buf), "Score: %u", g.score);
            if (len > 0) {
                std::strncpy(g.ui.pool[g.hud_id_score].text, buf, sizeof(g.ui.pool[g.hud_id_score].text) - 1);
            }
        }
        if (g.hud_id_combo != UINT16_MAX) {
            if (g.combo > 1) {
                char buf[32];
                int  len = snprintf(buf, sizeof(buf), "Combo x%d", g.combo);
                if (len > 0) {
                    std::strncpy(g.ui.pool[g.hud_id_combo].text, buf, sizeof(g.ui.pool[g.hud_id_combo].text) - 1);
                }
                g.ui.pool[g.hud_id_combo].flags |= ui::WF_Visible;
            } else {
                g.ui.pool[g.hud_id_combo].flags   &= ~ui::WF_Visible;
                g.ui.pool[g.hud_id_combo].text[0]  = '\0';
            }
        }
    }

    (void)g.renderer.begin_frame();

    // Sync cmd_buf/drawable from app's g_backend (renderer's embedded backend is separate)
    g.renderer.backend.cmd_buf  = g_backend->cmd_buf;
    g.renderer.backend.drawable = g_backend->drawable;

    PassDesc pass{};

    pass.clear_color[0] = 0.05f;
    pass.clear_color[1] = 0.05f;
    pass.clear_color[2] = 0.10f;
    pass.clear_color[3] = 1.0f;

    pass.color_load     = LoadOp::Clear;

    (void)g.renderer.backend.begin_pass(pass);

    float cam_x;
    float cam_y;
    float cam_angle;

    g.camera.get_offset(g.time, cam_x, cam_y, cam_angle);

    g.renderer.ortho(cam_x, (float)g.renderer.width + cam_x, (float)g.renderer.height + cam_y, cam_y, -1.0f, 1.0f);

    g.renderer.upload_camera();

    render_world();

    g.ui.render(g.renderer, g.batch, dt);

    g.renderer.submit();

    (void)g.renderer.backend.end_pass();

    g.renderer.end_frame();
}

// ─────────────────────────────────────────────────────────────

static void game_init(void *) {
    reset_game();
    auto &g                      = g_game;

    g.sfx_hit_id                 = g_audio_system.sfx.register_sound("sfx/hit.wav");

    g.sfx_score_id               = g_audio_system.sfx.register_sound("sfx/score.wav");

    g.sfx_tap_id                 = g_audio_system.sfx.register_sound("sfx/tap.wav");

    // Share device/cmd_queue/layer from app's backend BEFORE init (renderer has its own embedded instance)
    g.renderer.backend.device    = g_backend->device;
    g.renderer.backend.cmd_queue = g_backend->cmd_queue;
    g.renderer.backend.layer     = g_backend->layer;

    (void)g.renderer.init(nullptr, 0, 0);

    g.batch.init();
}

// ─────────────────────────────────────────────────────────────

static void game_resize(void *, uint32_t w, uint32_t h) {
    g_game.renderer.content_scale = g_content_scale;
    g_game.renderer.resize(w, h);
}

// ─────────────────────────────────────────────────────────────

static void  game_cleanup(void *) { g_game.renderer.shutdown(); }

// ─────────────────────────────────────────────────────────────

AppCallbacks markmos_main(int, char **) {

    return {
        .user_data = nullptr,
        .init      = game_init,
        .frame     = game_frame,
        .resize    = game_resize,
        .cleanup   = game_cleanup,
    };
}

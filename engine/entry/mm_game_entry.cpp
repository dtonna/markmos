// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

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
#include "../core/mm_log.hpp"
#include "../ui/mm_ui.hpp"
#include "../core/mm_vfs.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────

static constexpr uint16_t MAX_BLOCKS    = 256;
static constexpr uint16_t MAX_STARS     = 32;

static constexpr float GRAVITY       = 150.0f;
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

    TextureHandle demo_tex;
    Material      demo_mat;

    ui::Manager   ui;

    Block         blocks[MAX_BLOCKS];

    uint16_t      active_blocks = 0;

    uint64_t      frame_count   = 0;

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

static Game *g_game = nullptr;

// ─────────────────────────────────────────────────────────────

static void play_sfx(uint8_t id, int priority, float base, float range) noexcept {

    float pitch = base + static_cast<float>(std::rand() % static_cast<int>(range * 100.0f + 0.5f)) / 100.0f;

    g_audio_system.sfx.play_id(id, static_cast<uint8_t>(priority), pitch);
}

// ─────────────────────────────────────────────────────────────

static void reset_game() noexcept {

    auto &g         = *g_game;

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
    g.frame_count   = 0;

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

    auto &g = *g_game;

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

static float get_game_scale() noexcept {
#if defined(TARGET_ANDROID) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    float s = g_game->renderer.content_scale;
    return s < 0.5f ? 1.0f : s;
#else
    return 1.0f;
#endif
}

static void spawn_block() noexcept {

    auto *b = alloc_block();

    if (!b) {
        return;
    }

    float scale = get_game_scale();
    float bw    = 80.0f * scale;
    float max_x = static_cast<float>(g_game->renderer.width) - bw;
    b->x        = max_x > bw ? static_cast<float>(std::rand() % static_cast<int>(max_x - bw + 1.0f) + (bw * 0.5f)) : (bw * 0.5f);
    b->y        = -bw;

    b->w        = bw;
    b->h        = bw;

    using mm_math::color;

    static const color colors[] = {
        color(1.0f, 0.267f, 0.267f),
        color(0.267f, 1.0f, 0.267f),
        color(0.267f, 0.267f, 1.0f),
        color(1.0f, 1.0f, 0.267f),
    };

    b->color       = colors[std::rand() % 4].to_u32_bgra();

    b->spawn_scale = 0.0f;

    g_game->tweens.spawn(&b->spawn_scale, 0.0f, 1.0f, 0.3f, EaseType::BackOut);
}

// ─────────────────────────────────────────────────────────────

static void game_over() noexcept {

    auto &g = *g_game;

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

    auto &g  = *g_game;

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

    auto &g       = *g_game;

    g.drop_timer -= dt;

    if (g.drop_timer <= 0.0f) {

        g.drop_timer = g.drop_interval;

        spawn_block();

        if (g.drop_interval > 0.2f) {
            g.drop_interval -= 0.003f;
        }
    }

    float scale = get_game_scale();
    float gravity = GRAVITY * scale;

    for (auto &b : g.blocks) {

        if (!b.active) {
            continue;
        }

        b.y += dt * gravity;

        if (b.y > (float)g_game->renderer.height + (b.h * 2.0f)) {

            game_over();

            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────

static void update_scene(float dt) noexcept {

    auto &g         = *g_game;

    g.destroy_count = 0;

    g.scene.each([dt](uint16_t idx) {
        float rs;

        std::memcpy(&rs, &g_game->scene.user_data[idx], sizeof(rs));

        g_game->scene.rotation[idx] += dt * rs;

        g_game->scene.y[idx]        -= dt * 40.0f;

        if (g_game->scene.y[idx] < -80.0f) {

            g_game->destroy_queue[g_game->destroy_count++] = idx;
        }
    });

    for (uint16_t i = 0; i < g.destroy_count; ++i) {

        g.scene.despawn_at(g.destroy_queue[i]);
    }
}

// ─────────────────────────────────────────────────────────────

static void render_world() noexcept {

    auto &g = *g_game;
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

    r.flush_particles(g.particles);

    static uint64_t last_log_frame = 0;
    if (g.frame_count > last_log_frame + 120) {
        MM_LOG("render_world: frame=%llu active_blocks=%u", (unsigned long long)g.frame_count, (uint32_t)g.active_blocks);
        last_log_frame = g.frame_count;
    }
}

// ─────────────────────────────────────────────────────────────

// ─── UI callbacks ──────────────────────────────────────────────────
static void on_play_click(uint16_t) {
    reset_game();
    g_game->started     = true;
    g_game->state       = GameState::Playing;
    g_game->drop_timer  = 0.0f;  // spawn first block immediately
}

static void on_toggle_sound(uint16_t) {
    g_game->sound_on = !g_game->sound_on;
    g_audio_system.sfx.set_master_volume(g_game->sound_on ? 1.0f : 0.0f);
}

static void on_checkbox_part(uint16_t) { g_game->particles_on = !g_game->particles_on; }

static void on_slider_volume(uint16_t, float val) { g_audio_system.sfx.set_master_volume(val); }

static void on_quit_click(uint16_t) {
    auto &g = *g_game;
    reset_game();
    g.ui.clear();
    g.ui_built  = false;
    g.hud_built = false;
    g.started   = false;
    g.state     = GameState::Playing;
}

static void on_exit_click(uint16_t) {
    app_quit();
}

static void on_restart_click(uint16_t) {
    auto &g = *g_game;
    reset_game();
    g.ui.clear();
    g.ui_built  = false;
    g.hud_built = false;
    g.started   = true;
    g.state     = GameState::Playing;
}

// ─── In-game HUD ─────────────────────────────────────────────────
static void build_hud() noexcept {
    auto &g  = *g_game;
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
    auto &g  = *g_game;
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

    uint16_t btn_restart      = m.button(cx - 95.0f, cy + 40.0f, 190.0f, 44.0f, "Restart", 0xFF4488FF, 0xFFFFFFFF, on_restart_click);
    if (btn_restart != UINT16_MAX) m.pool[btn_restart].scale = 1.0f;
    uint16_t btn_menu         = m.button(cx - 120.0f, cy + 100.0f, 240.0f, 44.0f, "Main Menu", 0xFF554466, 0xFFFFFFFF, on_quit_click);
    if (btn_menu != UINT16_MAX) m.pool[btn_menu].scale    = 1.0f;
}

// ─── Title screen ─────────────────────────────────────────────────
static void build_ui_demo() noexcept {
    auto &g   = *g_game;
    auto &m   = g.ui;

    float cw  = static_cast<float>(g.renderer.width);
    float ch  = static_cast<float>(g.renderer.height);

    float scale = 1.0f;
#if defined(TARGET_ANDROID) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    scale = g.renderer.content_scale * 0.8f;
    if (scale < 0.5f) scale = 0.5f;
#endif

    float cx  = cw * 0.5f;
    float cy  = ch * 0.15f;
    float gap = 60.0f * scale;

    // Title
    m.label(cx - 110.0f * scale, cy, "Markmos UI Demo", 0xFFFFAAFF, 1.5f * scale);
    cy += gap;
    // Thai font test
    m.label(cx - 80.0f * scale, cy + 28.0f * scale, "ภาษาไทยสู้มื้อ", 0xFF88FF88, 1.0f * scale);
    cy                     += gap;

    // Button
    uint16_t btn_play       = m.button(cx - 120.0f * scale, cy, 240.0f * scale, 50.0f * scale, "เล่นเกมกู", 0xFF4488FF, 0xFFFFFFFF, on_play_click);
    if (btn_play != UINT16_MAX) m.pool[btn_play].scale  = 1.0f;
    cy                     += gap + 4.0f * scale;
    uint16_t btn_exit       = m.button(cx - 120.0f * scale, cy, 240.0f * scale, 50.0f * scale, "Exit", 0xFF664466, 0xFFFFFFFF, on_exit_click);
    if (btn_exit != UINT16_MAX) m.pool[btn_exit].scale  = 1.0f;
    cy                     += gap + 4.0f * scale;
    // Toggle (switch)
    m.toggle(cx - 120.0f * scale, cy, 48.0f * scale, 28.0f * scale, "", 0xFF44FF44, 0xFF444444, 0, false, on_toggle_sound);
    m.label(cx - 64.0f * scale, cy + 27.0f * scale, "Sound ON/OFF", 0xFFCCCCCC, 1.0f * scale);
    cy += gap;

    // Checkbox
    m.checkbox(cx - 120.0f * scale, cy, "Enable Particles", 0xFF44FF88, 0x33444444, 0xFFCCCCCC, true, on_checkbox_part);
    cy += gap;

    // Slider
    m.label(cx - 120.0f * scale, cy + 14.0f * scale, "Volume", 0xFFAAAAAA, 1.0f * scale);
    cy += 26.0f * scale;
    m.slider(cx - 120.0f * scale, cy, 200.0f * scale, 28.0f * scale, 0.75f, on_slider_volume);
    cy += gap;

    // TextField
    m.label(cx - 120.0f * scale, cy + 14.0f * scale, "Player Name", 0xFFAAAAAA, 1.0f * scale);
    cy                     += 34.0f * scale;
    uint16_t txt_name       = m.textfield(cx - 110.0f * scale, cy, 220.0f * scale, 32.0f * scale, "Player1", 0x33555555, 0xFFFFFFFF);
    if (txt_name != UINT16_MAX) m.pool[txt_name].scale  = 0.6f;
    cy                     += gap + 26.0f * scale;

    // Buttons row: layout demo
    float bx                = cx - 220.0f * scale;
    m.label(bx, cy - 14.0f * scale, "Layout:", 0xFF888888, 1.0f * scale);
    uint16_t btn_panel = m.panel(bx, cy, 440.0f * scale, 50.0f * scale, 0x22444444);
    m.set_layout(btn_panel, 1, 8, 8);
    m.button(0, 0, 100.0f * scale, 34.0f * scale, "One", 0xFF554466, 0xFFFFFFFF, nullptr, btn_panel);
    m.button(0, 0, 100.0f * scale, 34.0f * scale, "Two", 0xFF665577, 0xFFFFFFFF, nullptr, btn_panel);
    m.button(0, 0, 125.0f * scale, 34.0f * scale, "Three", 0xFF776688, 0xFFFFFFFF, nullptr, btn_panel);
    // Image widget + textured button with procedural texture
    if (g.demo_mat.pipeline.is_valid()) {
        uint16_t img = m.image(cx + 140.0f * scale, ch * 0.1f, 160.0f * scale, 160.0f * scale);
        m.set_material(img, g.demo_mat);

        uint16_t tbtn = m.button(cx + 140.0f * scale, ch * 0.1f + 170.0f * scale, 160.0f * scale, 40.0f * scale, "Tex Btn",
                                  0xFFFFFFFF, 0xFFFFFFFF, nullptr);
        m.set_material(tbtn, g.demo_mat);
    }

    m.layout(g.renderer);
}

static void game_frame(void *, float dt, InputState &input) {

    auto &g         = *g_game;

    g.time         += dt;
    ++g.frame_count;

    g.tap_cooldown -= dt;
    g.hit_cooldown -= dt;

    float cam_x     = 0.0f;
    float cam_y     = 0.0f;
    float cam_angle = 0.0f;
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

        g.camera.get_offset(g.time, cam_x, cam_y, cam_angle);

        // Move blocks + update animations BEFORE hit-test so visual position matches
        update_blocks(dt);
        update_scene(dt);
        g.tweens.update(dt);

        // Immediate hit on touch-down (before gesture waits for release → blocks drift)
        bool hit_immediate = false;
        if (g.ui.clicked == UINT16_MAX) {
            for (uint8_t i = 0; i < input.touch.active_count; ++i) {
                auto &f = input.touch.fingers[i];
                if (f.phase != TouchPhase::Pressing) continue;
                float tx = f.curr_x + cam_x;
                float ty = f.curr_y + cam_y;
                fprintf(stderr, "[pressing] f=%llu tx=%.4f ty=%.4f active:", (unsigned long long)g.frame_count, tx, ty);
                for (auto &b : g.blocks) {
                    if (!b.active) continue;
                    fprintf(stderr, " (%.1f,%.1f)", b.x, b.y);
                }
                fprintf(stderr, "\n");
                float const EPS_LT = 15.0f;
                float const EPS_RB = 10.0f;
                for (int32_t bi = MAX_BLOCKS - 1; bi >= 0; --bi) {
                    auto &b = g.blocks[bi];
                    if (!b.active) continue;
                    float half_w = b.w * 0.5f;
                    float half_h = b.h * 0.5f;
                    if (tx + EPS_LT >= b.x - half_w && tx - EPS_RB <= b.x + half_w &&
                        ty + EPS_LT >= b.y - half_h && ty - EPS_RB <= b.y + half_h) {
                        fprintf(stderr, "[pressing] HIT block at (%.0f,%.0f)!\n", b.x, b.y);
                        hit_block(b);
                        g.tap_cooldown = 0.15f;
                        hit_immediate = true;
                        break;
                    }
                }
                break;
            }
        }

        // Tap/click to hit blocks (completed gesture — fallback)
        for (uint8_t a = 0; a < input.action_count && !hit_immediate; ++a) {
            if (input.actions[a] == InputAction::Select) {
                float tx = input.action_x;
                float ty = input.action_y;
                fprintf(stderr, "[click] f=%llu screen=(%.0f,%.0f) cam=(%.1f,%.1f) world=(%.0f,%.0f) clicked=%u\n",
                        (unsigned long long)g.frame_count, tx, ty, cam_x, cam_y, tx + cam_x, ty + cam_y, g.ui.clicked);
                // Don't hit if a widget was clicked instead
                if (g.ui.clicked != UINT16_MAX) break;

                // Convert screen tap position → world space
                tx += cam_x;
                ty += cam_y;
                fprintf(stderr, "[trace] f=%llu tx=%.4f ty=%.4f active:", (unsigned long long)g.frame_count, tx, ty);
                for (auto &b : g.blocks) {
                    if (!b.active) continue;
                    fprintf(stderr, " (%.1f,%.1f,%.0f,%.0f)", b.x, b.y, b.w, b.h);
                }
                fprintf(stderr, "\n");
                // Reverse order so top-most (last-rendered) block is checked first
                // 15px top/left, 10px bottom/right grace
                float const EPS_LT = 15.0f;
                float const EPS_RB = 10.0f;
                for (int32_t bi = MAX_BLOCKS - 1; bi >= 0; --bi) {
                    auto &b = g.blocks[bi];
                    if (!b.active) continue;
                    float half_w = b.w * 0.5f;
                    float half_h = b.h * 0.5f;
                    if (tx + EPS_LT >= b.x - half_w && tx - EPS_RB <= b.x + half_w &&
                        ty + EPS_LT >= b.y - half_h && ty - EPS_RB <= b.y + half_h) {
                        fprintf(stderr, "[click] HIT block at (%.0f,%.0f)!\n", b.x, b.y);
                        hit_block(b);
                        g.tap_cooldown = 0.15f;
                        break;
                    }
                }
                break;
            }
        }

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

    static uint64_t last_log_frame = 0;
    if (g.frame_count > last_log_frame + 60) {
        MM_LOG("game_frame: f=%llu - adding render commands", (unsigned long long)g.frame_count);
        last_log_frame = g.frame_count;
    }

    g.renderer.begin_frame();

    PassDesc pass{};
    pass.color_load     = LoadOp::Clear;
    pass.color_store    = StoreOp::Store;
    pass.clear_color[0] = 0.05f; // Dark blue clear
    pass.clear_color[1] = 0.05f;
    pass.clear_color[2] = 0.10f;
    pass.clear_color[3] = 1.0f;

    pass.color_load     = LoadOp::Clear;
    pass.color_store    = StoreOp::Store;

    g.renderer.graph.begin_pass(pass);

    g.renderer.ortho(cam_x, (float)g.renderer.width + cam_x, (float)g.renderer.height + cam_y, cam_y, -1.0f, 1.0f);

    g.renderer.upload_camera();

    render_world();

    // Reset camera for UI so hit-tests (pick) match screen coordinates
    g.renderer.ortho(0.0f, (float)g.renderer.width, (float)g.renderer.height, 0.0f, -1.0f, 1.0f);
    g.renderer.upload_camera();

    g.ui.render(g.renderer, g.batch, dt);

    g.renderer.graph.end_pass();

    g.renderer.submit();
    g.renderer.end_frame();
}

// ─────────────────────────────────────────────────────────────

static void game_init(void *) {
    if (!g_game) {
        MM_LOG("Allocating Game object (size: %zu bytes)", sizeof(Game));
        g_game = new Game();
    }
    reset_game();
    auto &g                      = *g_game;
    g.sfx_hit_id                 = g_audio_system.sfx.register_sound("hit.wav");
    g.sfx_score_id               = g_audio_system.sfx.register_sound("score.wav");
    g.sfx_tap_id                 = g_audio_system.sfx.register_sound("tap.wav");

    auto rend_res = g.renderer.init(g_backend, nullptr, 0, 0);
    if (!rend_res) {
        MM_ERROR("Renderer initialization FAILED!");
    } else {
        MM_LOG("Renderer initialized successfully");
    }

    g.batch.init();

    // Create procedural demo texture (no external PNG needed)
    {
        uint8_t tex_pixels[256 * 256 * 4];
        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 256; ++x) {
                int   i    = (y * 256 + x) * 4;
                float cx   = (float)(x - 128);
                float cy   = (float)(y - 128);
                float dist = sqrtf(cx * cx + cy * cy) / 128.0f;
                if (dist > 1.0f) dist = 1.0f;
                // Blue-purple radial gradient
                uint8_t tr  = (uint8_t)(220 - dist * 180);
                uint8_t tg  = (uint8_t)(160 - dist * 120);
                uint8_t tb  = (uint8_t)(255 - dist * 100);
                // Checkerboard overlay
                bool check = ((x / 32) + (y / 32)) % 2 == 0;
                if (check) { tr = tr * 6 / 10; tg = tg * 6 / 10; tb = tb * 6 / 10; }
                tex_pixels[i + 0] = tr;
                tex_pixels[i + 1] = tg;
                tex_pixels[i + 2] = tb;
                tex_pixels[i + 3] = 0xFF;
            }
        }
        TextureDesc tex_desc{};
        tex_desc.type       = TextureType::Tex2D;
        tex_desc.format     = PixelFormat::R8G8B8A8_UNORM;
        tex_desc.width      = 256;
        tex_desc.height     = 256;
        tex_desc.mip_levels = 1;
        tex_desc.array_layers = 1;
        auto tex_res        = g.renderer.backend->create_texture(tex_desc);
        if (tex_res) {
            g.demo_tex = *tex_res;
            g.renderer.backend->update_texture(g.demo_tex, tex_pixels, 0, 0, 256, 256, 0, 0);
            g.demo_mat = g.renderer.make_material(g.renderer.sprite_pipeline, g.demo_tex, g.renderer.default_sampler);
        }
    }
    g.batch.init();
}

// ─────────────────────────────────────────────────────────────

static void game_resize(void *, uint32_t w, uint32_t h) {
    g_game->renderer.content_scale = g_content_scale;
    g_game->renderer.resize(w, h);
}

// ─────────────────────────────────────────────────────────────

static void  game_cleanup(void *) {
    if (g_game) {
        g_game->renderer.shutdown();
        delete g_game;
        g_game = nullptr;
    }
}

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

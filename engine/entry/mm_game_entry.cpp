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
#include "../core/mm_log.hpp"
#include "../core/mm_vfs.hpp"
#include "../game/mm_camera_trauma.hpp"
#include "../game/mm_particle_pool.hpp"
#include "../game/mm_scene.hpp"
#include "../game/mm_text_popup_pool.hpp"
#include "../game/mm_tween_pool.hpp"
#include "../math/mm_color.h"
#include "../render/mm_renderer.hpp"
#include "../render/mm_sprite.hpp"
#include "../ui/mm_ui.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────

static constexpr uint16_t MAX_BLOCKS    = 256;
static constexpr uint16_t MAX_STARS     = 32;

static constexpr float    GRAVITY       = 150.0f;
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

static void  play_sfx(uint8_t id, int priority, float base, float range) noexcept {

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

    g.hud_built     = false;
    g.sound_on      = true;
    g.particles_on  = true;
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
    g.ui.theme = ui::Theme::load("themes/test_theme.json");
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
    // On mobile, the coordinate system is already logical (DIPs).
    // We don't need to scale by content_scale again, but we might want
    // a small boost (e.g., 1.2f) for better touch targets if needed.
    return 1.1f;
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
    float bw    = 60.0f * scale; // Slightly smaller base, but scaled for mobile
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

    float scale   = get_game_scale();
    float gravity = GRAVITY * scale;

#if defined(TARGET_ANDROID) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    gravity *= 1.5f; // Fall faster on mobile
#endif

    for (auto &b : g.blocks) {

        if (!b.active) {
            continue;
        }

        b.y += dt * gravity;

        if (b.y > (float)g_game->renderer.height + (b.h * 1.5f)) {

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
        // MM_LOG("render_world: frame=%llu active_blocks=%u", (unsigned long long)g.frame_count, (uint32_t)g.active_blocks);
        last_log_frame = g.frame_count;
    }
}

// ─────────────────────────────────────────────────────────────

// ─── UI callbacks ──────────────────────────────────────────────────
static void on_play_click(uint16_t) {
    reset_game();
    g_game->started    = true;
    g_game->state      = GameState::Playing;
    g_game->drop_timer = 0.0f; // spawn first block immediately
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

static void on_exit_click(uint16_t) { app_quit(); }

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
    auto &g     = *g_game;
    auto &m     = g.ui;
    auto &r     = g.renderer;

    float cw    = static_cast<float>(r.width);
    float scale = get_game_scale();

    // Safety margin from top
#if defined(TARGET_ANDROID) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    float safe_top = 80.0f * scale;
#else
    float safe_top = 20.0f;
#endif

    float sc_scale = 0.9f * scale;
    m.label(20.0f * scale, safe_top, "Score: 0", 0xFFFFFFFF, sc_scale);
    g.hud_id_score         = m.count - 1;

    // Combo centered
    const char *combo_text = "Combo x99"; // max width guess for init
    float       tw_combo, th_combo;
    r.measure_text(r.default_font, combo_text, 1.0f * scale, tw_combo, th_combo);
    m.label(cw * 0.5f - tw_combo * 0.5f, safe_top, "", 0xFF88FF88, 1.0f * scale);
    g.hud_id_combo = m.count - 1;

    // High score (left of Quit)
    char buf[48];
    snprintf(buf, sizeof(buf), "Best: %u", g.high_score);
    float tw_best, th_best;
    float best_scale = 0.8f * scale;
    r.measure_text(r.default_font, buf, best_scale, tw_best, th_best);

    float btn_w = 45.0f * scale;
    float btn_h = 28.0f * scale;
    float btn_x = cw - btn_w - (15.0f * scale);

    m.label(btn_x - tw_best - (10.0f * scale), safe_top + (2.0f * scale), buf, 0xFFAAAAAA, best_scale);
    m.button(btn_x, safe_top - (2.0f * scale), btn_w, btn_h, "Quit", 0x55333333, 0xFFCCCCCC, on_quit_click);
}

// ─── Game Over screen ─────────────────────────────────────────────
static void build_game_over() noexcept {
    auto &g  = *g_game;
    auto &m  = g.ui;
    auto &r  = g.renderer;

    float cw = static_cast<float>(r.width);
    float ch = static_cast<float>(r.height);
    float cx = cw * 0.5f;
    float cy = ch * 0.5f;

#if defined(TARGET_ANDROID) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    float       scale     = 1.1f;
    float       gap       = 65.0f * scale;

    const char *title     = "Game Over!";
    float       fsc_title = 1.2f * scale;
    float       tw_title, th_title;
    r.measure_text(r.default_font, title, fsc_title, tw_title, th_title);
    m.label(cx - tw_title * 0.5f, cy - 80.0f * scale, title, 0xFFFF4444, fsc_title);

    char buf[64];
    snprintf(buf, sizeof(buf), "Score: %u  |  Best: %u", g.score, g.high_score);
    float fsc_score = 0.9f * scale;
    float tw_score, th_score;
    r.measure_text(r.default_font, buf, fsc_score, tw_score, th_score);
    m.label(cx - tw_score * 0.5f, cy - 10.0f, buf, 0xFFFFFFFF, fsc_score);

    // Measure and build buttons with dynamic width
    float       btn_h       = 55.0f * scale;
    float       pad         = 40.0f * scale;

    const char *restart_txt = "Restart";
    float       tw_res, th_res;
    r.measure_text(r.default_font, restart_txt, 1.0f, tw_res, th_res);
    float bw_res = tw_res + pad;
    m.button(cx - bw_res * 0.5f, cy + 50.0f, bw_res, btn_h, restart_txt, 0xFF4488FF, 0xFFFFFFFF, on_restart_click);

    const char *menu_txt = "Main Menu";
    float       tw_menu, th_menu;
    r.measure_text(r.default_font, menu_txt, 1.0f, tw_menu, th_menu);
    float bw_menu = tw_menu + pad;
    m.button(cx - bw_menu * 0.5f, cy + 50.0f + gap, bw_menu, btn_h, menu_txt, 0xFF554466, 0xFFFFFFFF, on_quit_click);
#else
    m.label(cx - 80.0f, cy - 60.0f, "Game Over!", 0xFFFF4444, 2.0f);
    char buf[64];
    snprintf(buf, sizeof(buf), "Score: %u  |  Best: %u", g.score, g.high_score);
    m.label(cx - 120.0f, cy - 10.0f, buf, 0xFFFFFFFF, 1.2f);
    m.button(cx - 95.0f, cy + 40.0f, 190.0f, 44.0f, "Restart", 0xFF4488FF, 0xFFFFFFFF, on_restart_click);
    m.button(cx - 120.0f, cy + 100.0f, 240.0f, 44.0f, "Main Menu", 0xFF554466, 0xFFFFFFFF, on_quit_click);
#endif
}

// ─── Title screen ─────────────────────────────────────────────────
static void build_ui_demo() noexcept {
    auto &g  = *g_game;
    auto &m  = g.ui;
    auto &r  = g.renderer;

    float cw = static_cast<float>(r.width);
    float ch = static_cast<float>(r.height);
    float cx = cw * 0.5f;
    float cy = ch * 0.15f;

#if defined(TARGET_ANDROID) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    float       scale     = 1.1f;
    float       gap       = 65.0f * scale;

    const char *title     = "Markmos Mobile";
    float       fsc_title = 0.95f * scale;
    float       tw_title, th_title;
    r.measure_text(r.default_font, title, fsc_title, tw_title, th_title);
    m.label(cx - tw_title * 0.5f, cy, title, 0xFFFFAAFF, fsc_title);

    cy                    += gap;
    const char *thai_test  = "ภาษาไทยสู้มื้อ";
    float       tw_thai, th_thai;
    r.measure_text(r.default_font, thai_test, 1.0f, tw_thai, th_thai);
    m.label(cx - tw_thai * 0.5f, cy + 20.0f, thai_test, 0xFF88FF88, 1.0f);

    cy += gap;
    m.button(cx - 100.0f * scale, cy, 200.0f * scale, 55.0f * scale, "เล่นเกม", 0xFF4488FF, 0xFFFFFFFF, on_play_click);
    cy += gap + 10.0f;
    m.button(cx - 100.0f * scale, cy, 200.0f * scale, 55.0f * scale, "Exit", 0xFF664466, 0xFFFFFFFF, on_exit_click);
    cy += gap;
    m.toggle(cx - 100.0f * scale, cy, 50.0f * scale, 30.0f * scale, "", 0xFF44FF44, 0xFF444444, 0, false, on_toggle_sound);
    m.label(cx - 40.0f * scale, cy + 25.0f, "Sound", 0xFFCCCCCC, 1.0f);
#else
    m.label(cx - 110.0f, cy, "Markmos Desktop", 0xFFFFAAFF, 1.5f);
    cy += 55.0f;
    m.label(cx - 80.0f, cy + 28.0f, "ภาษาไทยสู้มื้อ", 0xFF88FF88, 1.0f);
    cy += 55.0f;
    m.button(cx - 120.0f, cy, 240.0f, 50.0f, "เล่นเกมกู", 0xFF4488FF, 0xFFFFFFFF, on_play_click);
    cy += 65.0f;
    m.button(cx - 120.0f, cy, 240.0f, 50.0f, "Exit", 0xFF664466, 0xFFFFFFFF, on_exit_click);
    cy += 55.0f;
    m.toggle(cx - 120.0f, cy, 48.0f, 28.0f, "", 0xFF44FF44, 0xFF444444, 0, false, on_toggle_sound);
    m.label(cx - 64.0f, cy + 27.0f, "Sound ON/OFF", 0xFFCCCCCC, 1.0f);
    if (g.demo_mat.pipeline.is_valid()) {
        m.image(cx + 160.0f, ch * 0.15f, 150.0f, 150.0f);
    }
#endif
    m.layout(r);
}

static void game_frame(void *, float dt, InputState &input) {

    auto &g  = *g_game;

    g.time  += dt;
    ++g.frame_count;

    g.tap_cooldown  -= dt;
    g.hit_cooldown  -= dt;

    float cam_x      = 0.0f;
    float cam_y      = 0.0f;
    float cam_angle  = 0.0f;
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
                if (f.phase != TouchPhase::Pressing) {
                    continue;
                }
                float tx = f.curr_x + cam_x;
                float ty = f.curr_y + cam_y;
                fprintf(stderr, "[pressing] f=%llu tx=%.4f ty=%.4f active:", (unsigned long long)g.frame_count, tx, ty);
                for (auto &b : g.blocks) {
                    if (!b.active) {
                        continue;
                    }
                    fprintf(stderr, " (%.1f,%.1f)", b.x, b.y);
                }
                fprintf(stderr, "\n");
                float const EPS_LT = 15.0f;
                float const EPS_RB = 10.0f;
                for (int32_t bi = MAX_BLOCKS - 1; bi >= 0; --bi) {
                    auto &b = g.blocks[bi];
                    if (!b.active) {
                        continue;
                    }
                    float half_w = b.w * 0.5f;
                    float half_h = b.h * 0.5f;
                    if (tx + EPS_LT >= b.x - half_w && tx - EPS_RB <= b.x + half_w && ty + EPS_LT >= b.y - half_h && ty - EPS_RB <= b.y + half_h) {
                        fprintf(stderr, "[pressing] HIT block at (%.0f,%.0f)!\n", b.x, b.y);
                        hit_block(b);
                        g.tap_cooldown = 0.15f;
                        hit_immediate  = true;
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
                fprintf(stderr, "[click] f=%llu screen=(%.0f,%.0f) cam=(%.1f,%.1f) world=(%.0f,%.0f) clicked=%u\n", (unsigned long long)g.frame_count, tx, ty,
                        cam_x, cam_y, tx + cam_x, ty + cam_y, g.ui.clicked);
                // Don't hit if a widget was clicked instead
                if (g.ui.clicked != UINT16_MAX) {
                    break;
                }

                // Convert screen tap position → world space
                tx += cam_x;
                ty += cam_y;
                fprintf(stderr, "[trace] f=%llu tx=%.4f ty=%.4f active:", (unsigned long long)g.frame_count, tx, ty);
                for (auto &b : g.blocks) {
                    if (!b.active) {
                        continue;
                    }
                    fprintf(stderr, " (%.1f,%.1f,%.0f,%.0f)", b.x, b.y, b.w, b.h);
                }
                fprintf(stderr, "\n");
                // Reverse order so top-most (last-rendered) block is checked first
                // 15px top/left, 10px bottom/right grace
                float const EPS_LT = 15.0f;
                float const EPS_RB = 10.0f;
                for (int32_t bi = MAX_BLOCKS - 1; bi >= 0; --bi) {
                    auto &b = g.blocks[bi];
                    if (!b.active) {
                        continue;
                    }
                    float half_w = b.w * 0.5f;
                    float half_h = b.h * 0.5f;
                    if (tx + EPS_LT >= b.x - half_w && tx - EPS_RB <= b.x + half_w && ty + EPS_LT >= b.y - half_h && ty - EPS_RB <= b.y + half_h) {
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
        // MM_LOG("game_frame: f=%llu - adding render commands  w=%u h=%u cs=%.2f",
        //        (unsigned long long)g.frame_count,
        //        g.renderer.width, g.renderer.height, g.renderer.content_scale);
        last_log_frame = g.frame_count;
    }

    g.renderer.begin_frame();

    PassDesc pass{};
    pass.color_load     = LoadOp::Clear;
    pass.color_store    = StoreOp::Store;
    pass.clear_color[0] = 0.05f;
    pass.clear_color[1] = 0.05f;
    pass.clear_color[2] = 0.10f;
    pass.clear_color[3] = 1.0f;

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
    auto &g        = *g_game;
    g.sfx_hit_id   = g_audio_system.sfx.register_sound("sfx/hit.wav");
    g.sfx_score_id = g_audio_system.sfx.register_sound("sfx/score.wav");
    g.sfx_tap_id   = g_audio_system.sfx.register_sound("sfx/tap.wav");


    auto rend_res  = g.renderer.init(g_backend, nullptr, 0, 0);
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
                if (dist > 1.0f) {
                    dist = 1.0f;
                }
                // Blue-purple radial gradient
                uint8_t tr    = (uint8_t)(220 - dist * 180);
                uint8_t tg    = (uint8_t)(160 - dist * 120);
                uint8_t tb    = (uint8_t)(255 - dist * 100);
                // Checkerboard overlay
                bool    check = ((x / 32) + (y / 32)) % 2 == 0;
                if (check) {
                    tr = tr * 6 / 10;
                    tg = tg * 6 / 10;
                    tb = tb * 6 / 10;
                }
                tex_pixels[i + 0] = tr;
                tex_pixels[i + 1] = tg;
                tex_pixels[i + 2] = tb;
                tex_pixels[i + 3] = 0xFF;
            }
        }
        TextureDesc tex_desc{};
        tex_desc.type         = TextureType::Tex2D;
        tex_desc.format       = PixelFormat::R8G8B8A8_UNORM;
        tex_desc.width        = 256;
        tex_desc.height       = 256;
        tex_desc.mip_levels   = 1;
        tex_desc.array_layers = 1;
        auto tex_res          = g.renderer.backend->create_texture(tex_desc);
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

static void game_cleanup(void *) {
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

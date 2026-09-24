// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// UI Lab — visual playground for engine/ui (Manager + widgets + styles).
// Read-only w.r.t. engine/ui: this file only *uses* the API. Found a bug?
// Note it down and file a separate fix task — do not patch mm_ui.* here.
//
// ─── HOW TO USE ─────────────────────────────────────────────────────
// Run (from repo root):
//   cmake -S . -B build_debug && cmake --build build_debug --target mm_07_ui_lab
//   ./build_debug/engine/mm_07_ui_lab
//
// Keys (D0-D9 = number-row digit keys, NOT numpad):
//   D1 panels+labels | D2 buttons | D3 toggles+checkboxes | D4 sliders
//   D5 textfields (type real text) | D6 styles/shapes | D7 HBox/VBox layout
//   D8 focus nav (Tab/Shift-Tab) | D9 stress + benchmark | M materials
//   Up/Down font scale | Left/Right layout spacing | +/- style corner radius
//   B toggle style border 0/0.08 | Space pause animations | R rebuild page
//
// Live-tune model: keys mutate g_font_scale / g_spacing / g_corner /
// g_border_w, then the page is rebuilt (build_page). Animation state
// (press/hover/thumb/cursor) lives in the widgets and keeps running.
// ────────────────────────────────────────────────────────────────────

#include "../app/mm_app.hpp"
#include "../math/mm_math.h"
#include "../render/mm_sprite_batch.hpp"
#include "../render/mm_renderer.hpp"
#include "../ui/mm_ui.hpp"
#include "../game/mm_ember_pool.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

// ─── Mode ────────────────────────────────────────────────────────────
enum class LabMode : uint8_t {
    PanelsLabels,
    Buttons,
    TogglesChecks,
    Sliders,
    TextFields,
    StylesShapes,
    Layout,
    FocusNav,
    Stress,
    Materials, // engine materials ported from mm_06 lab (key M)
    _Count,
};

const char *ModeName(LabMode m) noexcept {
    switch (m) {
    case LabMode::PanelsLabels:
        return "panels+labels";
    case LabMode::Buttons:
        return "buttons";
    case LabMode::TogglesChecks:
        return "toggles+checks";
    case LabMode::Sliders:
        return "sliders";
    case LabMode::TextFields:
        return "textfields";
    case LabMode::StylesShapes:
        return "styles+shapes";
    case LabMode::Layout:
        return "layout";
    case LabMode::FocusNav:
        return "focus";
    case LabMode::Stress:
        return "stress";
    case LabMode::Materials:
        return "materials";
    default:
        return "?";
    }
}

// ─── State ───────────────────────────────────────────────────────────
static Renderer    g_r;
static SpriteBatch g_batch;
static ui::Manager g_ui;

static LabMode g_mode      = LabMode::Buttons;
static float   g_view_w    = 900.0f;
static float   g_view_h    = 640.0f;
static float   g_time      = 0.0f;
static bool    g_paused    = false;
static float   g_fps_ema   = 60.0f;

// Live-tune knobs (keys mutate these, page rebuilds)
static float g_font_scale = 1.0f; // Up/Down, 0.5..2.0
static float g_spacing    = 8.0f; // Left/Right, 0..32
static float g_corner     = 0.12f; // +/-, 0..0.5 (style corner_r)
static float g_border_w   = 0.04f; // B toggles 0 / 0.08

// Interaction counters for the HUD
static uint32_t g_clicks[8] = {};
static float    g_slider_seen[4];
static uint8_t  g_slider_n = 0;
static char     g_last_event[64] = "-";

// Benchmark (D9 + HUD): CPU ms of each UI stage, averaged over 30 frames
static float g_ms_measure = 0.0f, g_ms_layout = 0.0f, g_ms_render = 0.0f;
static float g_ms_acc_m = 0.0f, g_ms_acc_l = 0.0f, g_ms_acc_r = 0.0f;
static uint32_t g_ms_n = 0;

// ─── Materials demo (key M): engine ports of the mm_06 lab shaders ───
static TextureHandle g_check_tex{};
static TextureHandle g_sinenrm_tex{};
static TextureHandle g_ember_tex{};
static SpriteBatch   g_ebatch;
static EmberPool     g_embers;

static bool MakeTexture(uint16_t w, uint16_t h, PixelFormat fmt, TextureHandle &out) noexcept {
    auto &bk = *g_backend;
    TextureDesc td = {TextureType::Tex2D, fmt, w, h, 1, 1, 1};
    auto tr = bk.create_texture(td);
    if (!tr) {
        return false;
    }
    out = *tr;
    return true;
}

static void BuildCheckerTexture() noexcept {
    static constexpr uint32_t N = 64;
    static uint32_t pixels[N * N];
    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N; ++x) {
            bool even = ((x / 8) + (y / 8)) % 2 == 0;
            pixels[y * N + x] = even ? 0xFFFFFFFFu : 0xFFB0B0B0u;
        }
    }
    auto &bk = *g_backend;
    (void)bk.update_texture(g_check_tex, pixels, 0, 0, N, N, 0, 0);
}

// Sine-heightfield normal map (smooth bumps) for NORMAL_MAP/PLASTIC demo.
static void BuildSineNormalTexture() noexcept {
    static constexpr uint16_t N = 128;
    static float   height[N * N];
    static uint32_t px[N * N];
    for (uint16_t y = 0; y < N; ++y) {
        for (uint16_t x = 0; x < N; ++x) {
            float u = static_cast<float>(x) / N;
            float v = static_cast<float>(y) / N;
            height[y * N + x] = std::sin(u * mm_math::MM_TWO_PI * 3.0f) * std::sin(v * mm_math::MM_TWO_PI * 3.0f);
        }
    }
    auto hat = [&](int x, int y) noexcept -> float {
        x = (x + N) % N;
        y = (y + N) % N;
        return height[static_cast<uint32_t>(y) * N + static_cast<uint32_t>(x)];
    };
    for (uint16_t y = 0; y < N; ++y) {
        for (uint16_t x = 0; x < N; ++x) {
            float dhdx = (hat(x + 1, y) - hat(x - 1, y)) * 0.5f;
            float dhdy = (hat(x, y + 1) - hat(x, y - 1)) * 0.5f;
            float nx = -dhdx * 2.0f, ny = -dhdy * 2.0f, nz = 1.0f;
            float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            auto enc = [&](float f) noexcept -> uint32_t {
                int q = static_cast<int>((f * 0.5f + 0.5f) * 255.0f + 0.5f);
                if (q < 0) q = 0;
                if (q > 255) q = 255;
                return static_cast<uint32_t>(q);
            };
            uint32_t r = enc(nx * inv), g = enc(ny * inv), b = enc(nz * inv);
            px[y * N + x] = (0xFFu << 24) | (b << 16) | (g << 8) | r;
        }
    }
    auto &bk = *g_backend;
    (void)bk.update_texture(g_sinenrm_tex, px, 0, 0, N, N, 0, 0);
}

static void BuildMaterialsResources() noexcept {
    auto &bk = *g_backend;
    (void)bk;
    if (MakeTexture(64, 64, PixelFormat::R8G8B8A8_UNORM, g_check_tex)) {
        BuildCheckerTexture();
    }
    if (MakeTexture(128, 128, PixelFormat::R8G8B8A8_UNORM, g_sinenrm_tex)) {
        BuildSineNormalTexture();
    }
    if (MakeTexture(EMBER_GLOW_SIZE, EMBER_GLOW_SIZE, PixelFormat::R8G8B8A8_UNORM, g_ember_tex)) {
        static uint32_t glow[EMBER_GLOW_SIZE * EMBER_GLOW_SIZE];
        MakeEmberGlowTexture(glow);
        (void)bk.update_texture(g_ember_tex, glow, 0, 0, EMBER_GLOW_SIZE, EMBER_GLOW_SIZE, 0, 0);
    }
    g_ebatch.init();
    g_embers.init(g_view_w - 150.0f, g_view_h - 140.0f, 24, 12345u);
}

static float NowMs() noexcept {
    return static_cast<float>(clock()) * 1000.0f / static_cast<float>(CLOCKS_PER_SEC);
}

// ─── Callbacks ───────────────────────────────────────────────────────
void OnLabButton(uint16_t id) noexcept {
    // Map widget id -> 0..7 bucket by order of creation on button pages.
    // (Coarse but enough for a click counter HUD.)
    uint32_t b = static_cast<uint32_t>(id) % 8;
    if (g_clicks[b] < 999999u) {
        ++g_clicks[b];
    }
    snprintf(g_last_event, sizeof(g_last_event), "click id=%u", id);
}

void OnLabSlider(uint16_t id, float v) noexcept {
    if (g_slider_n < 4) {
        g_slider_seen[g_slider_n++] = v;
    } else {
        g_slider_seen[0] = g_slider_seen[1];
        g_slider_seen[1] = g_slider_seen[2];
        g_slider_seen[2] = g_slider_seen[3];
        g_slider_seen[3] = v;
    }
    snprintf(g_last_event, sizeof(g_last_event), "slider id=%u val=%.2f", id, v);
}

void OnLabToggle(uint16_t id) noexcept {
    snprintf(g_last_event, sizeof(g_last_event), "toggle id=%u state=%d", id, g_ui.is_toggled(id) ? 1 : 0);
}

// Style used by the gallery: rounded rect with live-tuned corner/border.
uint8_t GalleryStyle() noexcept {
    ui::WidgetStyle s{};
    s.bg_tex       = TextureHandle::invalid();
    s.border_color = 0xFF88AAFF; // 0xAARRGGBB
    s.border_width = g_border_w;
    s.corner_r     = g_corner;
    s.shape        = ui::ShapeType::RoundedRect;
    s._pad[0] = s._pad[1] = s._pad[2] = 0;
    return g_ui.register_style(s);
}

// HUD reserves the top strip; pages live below it (no overlap).
static constexpr float HUD_H  = 196.0f;
static constexpr float PAGE_Y = 210.0f;

// ─── Page builders ───────────────────────────────────────────────────
void BuildPanelsLabels() noexcept {
    uint8_t st = GalleryStyle();
    uint16_t root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, 0xFF2D2D2D, UINT16_MAX, st);
    g_ui.label(20.0f, 12.0f, "Panels + Labels (D1)", 0xFFFFFFFF, 0.50f, root);
    g_ui.label(20.0f, 44.0f, "nested panel below has WF_Clip", 0xFFAAAAAA, 0.36f, root);
    uint16_t inner = g_ui.panel(20.0f, 76.0f, 300.0f, 150.0f, 0xFF3A3A4A, root, st);
    g_ui.pool[inner].flags = static_cast<uint8_t>(g_ui.pool[inner].flags | ui::WF_Clip);
    g_ui.label(10.0f, 10.0f, "clipped child 1", 0xFFFFFFFF, 0.40f, inner);
    g_ui.label(10.0f, 40.0f, "clipped child 2 (overflow...)", 0xFFFFFFFF, 0.40f, inner);
    g_ui.label(10.0f, 70.0f, "clipped child 3", 0xFFFFFFFF, 0.40f, inner);
    g_ui.label(10.0f, 100.0f, "clipped child 4", 0xFFFFFFFF, 0.40f, inner);
    g_ui.label(10.0f, 142.0f, "clipped child 5 (cut!)", 0xFFFFFFFF, 0.40f, inner);
    uint16_t inner2 = g_ui.panel(340.0f, 76.0f, 300.0f, 150.0f, 0xFF2A4A2A, root, st);
    g_ui.label(10.0f, 10.0f, "plain nested panel", 0xFFFFFFFF, 0.40f, inner2);
    g_ui.label(10.0f, 44.0f, "scale 0.30 / 0.50 / 0.70", 0xFFAAAAAA, 0.30f, inner2);
    g_ui.label(10.0f, 70.0f, "scale 0.30 / 0.50 / 0.70", 0xFFAAAAAA, 0.50f, inner2);
    g_ui.label(10.0f, 110.0f, "scale 0.30 / 0.50 / 0.70", 0xFFAAAAAA, 0.70f, inner2);
}

void BuildButtons() noexcept {
    uint8_t st = GalleryStyle();
    uint16_t root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, 0xFF2D2D2D, UINT16_MAX, st);
    g_ui.label(20.0f, 12.0f, "Buttons (D2) - hover/press/click", 0xFFFFFFFF, 0.50f, root);
    g_ui.button(20.0f, 50.0f, 160.0f, 44.0f, "Normal", 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, root, 0.40f, st);
    g_ui.button(200.0f, 50.0f, 160.0f, 44.0f, "Accent", 0xFF3366CC, 0xFFFFFFFF, OnLabButton, root, 0.40f, st);
    uint16_t dis = g_ui.button(380.0f, 50.0f, 160.0f, 44.0f, "Disabled", 0xFF3A3A3A, 0xFF888888, OnLabButton, root, 0.40f, st);
    g_ui.pool[dis].flags = static_cast<uint8_t>(g_ui.pool[dis].flags & ~ui::WF_Enabled);
    uint16_t custom = g_ui.button(20.0f, 110.0f, 340.0f, 44.0f, "", 0, 0xFFFFFFFF, OnLabButton, root, 0.40f, st);
    g_ui.pool[custom].shape = static_cast<uint8_t>(ui::ShapeType::Custom);
    g_ui.pool[custom].on_draw = [](uint16_t, Renderer &r, SpriteBatch &b, float ax, float ay, float) noexcept {
        // Custom-drawn button: teal pill (proves on_draw path).
        b.add(ax + 170.0f, ay + 22.0f, 340.0f, 44.0f, 0.0f, 0xFF00CED1, 5, 0, 0.5f, 0.0f, 0);
        r.flush_rounded_sprites(b);
        b.reset();
        r.draw_text(r.default_font, "Custom on_draw", ax + 100.0f, ay + 14.0f, 0xFF000000, 0.40f);
    };
    g_ui.label(20.0f, 170.0f, "click counters live on the HUD (top-left)", 0xFFAAAAAA, 0.36f, root);
}

void BuildTogglesChecks() noexcept {
    uint8_t st = GalleryStyle();
    uint16_t root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, 0xFF2D2D2D, UINT16_MAX, st);
    g_ui.label(20.0f, 12.0f, "Toggles + Checkboxes (D3)", 0xFFFFFFFF, 0.50f, root);
    // NOTE: toggle()/checkbox() have no scale param; text size follows the
    // pool scale field (default 1.0), like freecell does for shape.
    uint16_t t0 = g_ui.toggle(20.0f, 52.0f, 140.0f, 40.0f, "Sound", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, true, OnLabToggle, root, st);
    uint16_t t1 = g_ui.toggle(20.0f, 102.0f, 140.0f, 40.0f, "Effects", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, OnLabToggle, root, st);
    uint16_t t2 = g_ui.toggle(20.0f, 152.0f, 140.0f, 40.0f, "Music", 0xFF4488FF, 0xFF444444, 0xFFFFFFFF, false, OnLabToggle, root, st);
    g_ui.pool[t0].scale = 0.45f;
    g_ui.pool[t1].scale = 0.45f;
    g_ui.pool[t2].scale = 0.45f;
    uint16_t c0 = g_ui.checkbox(330.0f, 56.0f, "Hints", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, true, OnLabToggle, root, st);
    uint16_t c1 = g_ui.checkbox(330.0f, 106.0f, "Autopilot", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, OnLabToggle, root, st);
    g_ui.pool[c0].scale = 0.45f;
    g_ui.pool[c1].scale = 0.45f;
    g_ui.label(20.0f, 210.0f, "watch thumb_pos slide 0<->1 on HUD state", 0xFFAAAAAA, 0.36f, root);
}

void BuildSliders() noexcept {
    uint8_t st = GalleryStyle();
    uint16_t root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, 0xFF2D2D2D, UINT16_MAX, st);
    g_ui.label(20.0f, 12.0f, "Sliders (D4) - drag the thumbs", 0xFFFFFFFF, 0.50f, root);
    g_ui.slider(20.0f, 56.0f, 400.0f, 28.0f, 0.0f, OnLabSlider, root, st);
    g_ui.slider(20.0f, 100.0f, 400.0f, 28.0f, 0.5f, OnLabSlider, root, st);
    g_ui.slider(20.0f, 144.0f, 400.0f, 28.0f, 1.0f, OnLabSlider, root, st);
    g_ui.slider(20.0f, 188.0f, 200.0f, 28.0f, 0.25f, OnLabSlider, root, st);
    g_ui.label(20.0f, 226.0f, "last values scroll on the HUD", 0xFFAAAAAA, 0.36f, root);
}

void BuildTextFields() noexcept {
    uint8_t st = GalleryStyle();
    uint16_t root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, 0xFF2D2D2D, UINT16_MAX, st);
    g_ui.label(20.0f, 12.0f, "TextFields (D5) - click then type", 0xFFFFFFFF, 0.50f, root);
    g_ui.textfield(20.0f, 52.0f, 400.0f, 40.0f, "Player One", 0xFF1D1D1D, 0xFFFFFFFF, root, st);
    g_ui.textfield(20.0f, 104.0f, 400.0f, 40.0f, "", 0xFF1D1D1D, 0xFFFFFFFF, root, st);
    g_ui.label(20.0f, 160.0f, "Tab moves focus, cursor blinks via cursor_timer", 0xFFAAAAAA, 0.36f, root);
}

void BuildStylesShapes() noexcept {
    uint8_t rounded = GalleryStyle();
    ui::WidgetStyle circle{};
    circle.bg_tex       = TextureHandle::invalid();
    circle.border_color = 0xFF88AAFF;
    circle.border_width = g_border_w;
    circle.corner_r     = 0.5f;
    circle.shape        = ui::ShapeType::Circle;
    circle._pad[0] = circle._pad[1] = circle._pad[2] = 0;
    uint8_t circle_st = g_ui.register_style(circle);
    ui::WidgetStyle sharp{};
    sharp.bg_tex       = TextureHandle::invalid();
    sharp.border_color = 0xFFFFCC00;
    sharp.border_width = g_border_w;
    sharp.corner_r     = 0.0f;
    sharp.shape        = ui::ShapeType::Rect;
    sharp._pad[0] = sharp._pad[1] = sharp._pad[2] = 0;
    uint8_t sharp_st = g_ui.register_style(sharp);

    uint16_t root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, 0xFF2D2D2D, UINT16_MAX, rounded);
    g_ui.label(20.0f, 12.0f, "Styles + Shapes (D6) - +/- corner, B border", 0xFFFFFFFF, 0.50f, root);
    g_ui.button(20.0f, 52.0f, 200.0f, 52.0f, "Rounded", 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, root, 0.40f, rounded);
    g_ui.button(240.0f, 52.0f, 200.0f, 52.0f, "Rect", 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, root, 0.40f, sharp_st);
    g_ui.button(460.0f, 52.0f, 52.0f, 52.0f, "", 0xFF3366CC, 0xFFFFFFFF, OnLabButton, root, 0.40f, circle_st);
    g_ui.button(530.0f, 52.0f, 52.0f, 52.0f, "", 0xFF5CB85C, 0xFFFFFFFF, OnLabButton, root, 0.40f, circle_st);
    char buf[64];
    snprintf(buf, sizeof(buf), "corner_r=%.2f border=%.3f (live)", g_corner, g_border_w);
    g_ui.label(20.0f, 120.0f, buf, 0xFFAAAAAA, 0.36f, root);
}

void BuildLayout() noexcept {
    // Demos the extended layout system: main/cross alignment (0=Start,
    // 1=Center, 2=End), percent widths, and margins. All containers are
    // root-level; nested containers under plain panels work too now.
    uint8_t st = GalleryStyle();
    uint16_t bg = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, 0xFF2D2D2D, UINT16_MAX, st);
    g_ui.label(20.0f, 12.0f, "Align + Percent + Margin (D7)", 0xFFFFFFFF, 0.50f, bg);
    g_ui.label(20.0f, 44.0f, "rows: Start / Center / End + 30/40/20% + margins", 0xFFAAAAAA, 0.36f, bg);

    float row_w = g_view_w - 80.0f - 40.0f; // inside bg (20px inset each side)
    float row_x = 40.0f + 20.0f;
    float row_y = PAGE_Y + 84.0f;
    auto demo_row = [&](const char *b1, const char *b2, const char *b3, uint8_t main_a, uint8_t cross_a) noexcept {
        uint16_t row = g_ui.panel(row_x, row_y, row_w, 56.0f, 0xFF222233, UINT16_MAX, st);
        g_ui.set_layout(row, 1, 8, static_cast<uint8_t>(g_spacing));
        g_ui.set_layout_align(row, main_a, cross_a);
        g_ui.button(0.0f, 0.0f, 110.0f, 32.0f, b1, 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, row, 0.36f, st);
        g_ui.button(0.0f, 0.0f, 110.0f, 32.0f, b2, 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, row, 0.36f, st);
        g_ui.button(0.0f, 0.0f, 110.0f, 32.0f, b3, 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, row, 0.36f, st);
        row_y += 68.0f;
    };
    demo_row("A1", "A2", "A3", 0, 0); // Start / Top (legacy look)
    demo_row("B1", "B2", "B3", 1, 1); // Center / Center
    demo_row("C1", "C2", "C3", 2, 2); // End / Bottom

    // Percent + margin row: 30/40/20% of inner width, 6px side margins.
    uint16_t prow = g_ui.panel(row_x, row_y, row_w, 56.0f, 0xFF222233, UINT16_MAX, st);
    g_ui.set_layout(prow, 1, 8, static_cast<uint8_t>(g_spacing));
    uint16_t p1 = g_ui.button(0.0f, 0.0f, 110.0f, 40.0f, "30%", 0xFF3366CC, 0xFFFFFFFF, OnLabButton, prow, 0.36f, st);
    uint16_t p2 = g_ui.button(0.0f, 0.0f, 110.0f, 40.0f, "40%", 0xFF3366CC, 0xFFFFFFFF, OnLabButton, prow, 0.36f, st);
    uint16_t p3 = g_ui.button(0.0f, 0.0f, 110.0f, 40.0f, "20%", 0xFF3366CC, 0xFFFFFFFF, OnLabButton, prow, 0.36f, st);
    g_ui.set_size_pct(p1, 30, 0);
    g_ui.set_size_pct(p2, 40, 0);
    g_ui.set_size_pct(p3, 20, 0);
    g_ui.set_margin(p1, 0, 6, 0, 6);
    g_ui.set_margin(p2, 0, 6, 0, 6);
    g_ui.set_margin(p3, 0, 6, 0, 6);
}

void BuildFocusNav() noexcept {
    uint8_t st = GalleryStyle();
    uint16_t root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, 0xFF2D2D2D, UINT16_MAX, st);
    g_ui.label(20.0f, 12.0f, "Focus nav (D8) - Tab / Shift-Tab", 0xFFFFFFFF, 0.50f, root);
    g_ui.button(20.0f, 52.0f, 160.0f, 44.0f, "First", 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, root, 0.40f, st);
    g_ui.toggle(200.0f, 52.0f, 200.0f, 44.0f, "Second", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, OnLabToggle, root, st);
    g_ui.textfield(420.0f, 52.0f, 220.0f, 44.0f, "Third", 0xFF1D1D1D, 0xFFFFFFFF, root, st);
    g_ui.button(20.0f, 112.0f, 160.0f, 44.0f, "Fourth", 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, root, 0.40f, st);
    g_ui.checkbox(200.0f, 116.0f, "Fifth", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, OnLabToggle, root, st);
    g_ui.slider(420.0f, 116.0f, 220.0f, 36.0f, 0.5f, OnLabSlider, root, st);
    g_ui.label(20.0f, 176.0f, "green ring = focused widget (watch focus_id)", 0xFFAAAAAA, 0.36f, root);
}

void BuildStress() noexcept {
    uint8_t st = GalleryStyle();
    // Fill the pool with a repeating mix of every widget type.
    uint16_t n = 0;
    float    x = 20.0f, y = PAGE_Y;
    while (n < ui::Manager::MAX - 4) {
        if (x + 130.0f > g_view_w - 20.0f) {
            x = 20.0f;
            y += 46.0f;
        }
        if (y + 40.0f > g_view_h - 20.0f) {
            break;
        }
        uint16_t id = UINT16_MAX;
        // NOTE: stress cells use empty text — widget labels (toggle text now
        // draws right of its track) would spill into neighbor cells.
        switch (n % 6) {
        case 0:
            id = g_ui.button(x, y, 120.0f, 36.0f, "Btn", 0xFF3A3A3A, 0xFFFFFFFF, OnLabButton, UINT16_MAX, 0.32f, st);
            break;
        case 1:
            id = g_ui.toggle(x, y, 120.0f, 36.0f, "", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, (n % 2) == 0, OnLabToggle, UINT16_MAX,
                             st);
            break;
        case 2:
            // Narrow slider: the engine % label draws right of the track.
            id = g_ui.slider(x, y, 80.0f, 30.0f, 0.5f, OnLabSlider, UINT16_MAX, st);
            break;
        case 3:
            id = g_ui.checkbox(x, y, "", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, (n % 2) == 0, OnLabToggle, UINT16_MAX, st);
            break;
        case 4:
            id = g_ui.label(x, y, "lbl", 0xFFCCCCCC, 0.32f, UINT16_MAX, st);
            break;
        default:
            id = g_ui.panel(x, y, 120.0f, 36.0f, 0xFF333344, UINT16_MAX, st);
            break;
        }
        if (id == UINT16_MAX) {
            break;
        }
        ++n;
        x += 130.0f;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "stress: %u widgets (pool %u)", n, ui::Manager::MAX);
    printf("[uilab] %s\n", buf);
}

void BuildPage(LabMode m) noexcept {
    g_ui.clear();
    // Fresh default style slot per page (style pool resets with clear).
    switch (m) {
    case LabMode::PanelsLabels:
        BuildPanelsLabels();
        break;
    case LabMode::Buttons:
        BuildButtons();
        break;
    case LabMode::TogglesChecks:
        BuildTogglesChecks();
        break;
    case LabMode::Sliders:
        BuildSliders();
        break;
    case LabMode::TextFields:
        BuildTextFields();
        break;
    case LabMode::StylesShapes:
        BuildStylesShapes();
        break;
    case LabMode::Layout:
        BuildLayout();
        break;
    case LabMode::FocusNav:
        BuildFocusNav();
        break;
    case LabMode::Stress:
        BuildStress();
        break;
    case LabMode::Materials:
        // No widgets: RenderMaterials() draws raw batches + embers instead.
        break;
    default:
        break;
    }
    printf("[uilab] mode=%s widgets=%u view=%.0fx%.0f\n", ModeName(m), g_ui.count, g_view_w, g_view_h);
}

// ─── Materials demo (key M): one quad per engine-ported lab shader ───
void RenderMaterials(float dt) noexcept {
    // 2 rows x 3 + 1 wide quad, below the HUD strip.
    const float qw = 230.0f, qh = 150.0f, gx = 250.0f, gy = 190.0f;
    const float ox = (g_view_w - (2.0f * gx + qw)) * 0.5f;
    const float oy = 210.0f;
    auto quad_xy = [&](uint16_t i, float &x, float &y) noexcept {
        x = ox + qw * 0.5f + static_cast<float>(i % 3) * gx;
        y = oy + qh * 0.5f + static_cast<float>(i / 3) * gy;
    };
    auto caption = [&](const char *s, float x, float y) noexcept {
        float tw = 0.0f, th = 0.0f;
        g_r.measure_text(g_r.default_font, s, 0.32f, tw, th);
        g_r.draw_text(g_r.default_font, s, x - tw * 0.5f, y, 0xFFFFFFFF, 0.32f);
    };

    // Animated light orbiting overhead (drives normal/cartoon/plastic).
    float az = g_time * 0.5f;
    float el = 0.85f;
    float ce = std::cos(el);
    float lx = ce * std::cos(az), ly = ce * std::sin(az), lz = std::sin(el);

    float x = 0.0f, y = 0.0f;
    // Row 0: derive / normal_map / cartoon (checker albedo shows edges).
    quad_xy(0, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw, qh, 0.0f, 0xFFFFFFFFu, 1);
    g_r.flush_normal_derive(g_batch, g_check_tex, g_r.default_sampler, lx, ly, lz, 1.0f, 0.25f, 0.5f, 2.0f);
    caption("normal_derive", x, y + qh * 0.5f + 16.0f);
    quad_xy(1, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw, qh, 0.0f, 0xFFFFFFFFu, 1);
    g_r.flush_normal_map(g_batch, g_check_tex, g_sinenrm_tex, g_r.default_sampler, lx, ly, lz, 1.0f, 0.25f, 0.5f);
    caption("normal_map", x, y + qh * 0.5f + 16.0f);
    quad_xy(2, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw, qh, 0.0f, 0xFFFFFFFFu, 1);
    g_r.flush_cartoon(g_batch, g_check_tex, g_r.default_sampler, lx, ly, lz, 1.0f, 0.25f, 0.5f, 2.0f, 3.0f, 0.35f, 0.85f);
    caption("cartoon", x, y + qh * 0.5f + 16.0f);
    // Row 1: plastic / glow_pulse / gold_border.
    quad_xy(3, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw, qh, 0.0f, 0xFFFFFFFFu, 1);
    g_r.flush_plastic(g_batch, g_check_tex, g_sinenrm_tex, g_r.default_sampler, lx, ly, lz, 1.0f, 0.25f, 0.5f, 0.6f, 0.5f, 0.4f, 120.0f);
    caption("plastic", x, y + qh * 0.5f + 16.0f);
    quad_xy(4, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw * 1.5f, qh * 1.5f, 0.0f, 0xFFFFFFFFu, 1);
    g_r.flush_glow_pulse(g_batch, g_check_tex, g_r.default_sampler, 1.2f, 0.12f, 0.025f, 0xFF00CED1u, qw / qh, 1.5f, 1.5f);
    caption("glow_pulse", x, y + qh * 0.75f + 16.0f);
    quad_xy(5, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw * 1.5f, qh * 1.5f, 0.0f, 0xFFFFFFFFu, 1);
    g_r.flush_gold_border(g_batch, g_check_tex, g_r.default_sampler, 1.0f, 0.035f, 0xFFFFC740u, qw / qh, 1.5f, 1.5f);
    caption("gold_border", x, y + qh * 0.75f + 16.0f);
    // Row 2: gold_stay + ember corner.
    quad_xy(6, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw * 1.5f, qh * 1.5f, 0.0f, 0xFFFFFFFFu, 1);
    g_r.flush_gold_stay(g_batch, g_check_tex, g_r.default_sampler, 1.0f, 0.05f, 0xFFFFC740u, qw / qh, 1.5f, 1.5f);
    caption("gold_stay", x, y + qh * 0.75f + 16.0f);

    // Embers (EmberPool + additive material) drift in the corner.
    g_embers.update(dt, g_time);
    g_ebatch.reset();
    for (uint16_t i = 0; i < g_embers.count; ++i) {
        const EmberParticle &e = g_embers.embers[i];
        float t = (e.max_life > 0.0f) ? (e.life / e.max_life) : 0.0f;
        g_ebatch.add(e.x, e.y, EmberSize(e), EmberSize(e), 0.0f, EmberColor(t), 2);
    }
    g_r.flush_sprites(g_ebatch, g_r.make_material(g_r.sprite_additive_pipeline, g_ember_tex, g_r.default_sampler));
    caption("embers (EmberPool)", g_view_w - 150.0f, g_view_h - 60.0f);
}

// ─── HUD ─────────────────────────────────────────────────────────────
void DrawHud() noexcept {
    char buf[160];
    float y = 8.0f;
    auto line = [&](const char *s) noexcept {
        g_r.draw_text(g_r.default_font, s, 10.0f, y, 0xFFFFFFFF, 0.34f);
        y += 20.0f;
    };
    snprintf(buf, sizeof(buf), "UI LAB  mode=%s  fps=%.0f  widgets=%u/%u freelist=%u", ModeName(g_mode), g_fps_ema, g_ui.count, ui::Manager::MAX,
             g_ui.freelist_count);
    line(buf);
    snprintf(buf, sizeof(buf), "view=%.0fx%.0f r=%ux%u scale=%.2f", g_view_w, g_view_h, g_r.width, g_r.height, g_r.content_scale);
    line(buf);
    // (ids print raw; 65535 = UINT16_MAX = none)
    snprintf(buf, sizeof(buf), "hot=%u active=%u clicked=%u focus=%u editing=%u", g_ui.hot, g_ui.active, g_ui.clicked, g_ui.focus_id,
             g_ui.editing_id);
    line(buf);
    snprintf(buf, sizeof(buf), "bench ms: measure=%.2f layout=%.2f render=%.2f", g_ms_measure, g_ms_layout, g_ms_render);
    line(buf);
    snprintf(buf, sizeof(buf), "tune: font=%.2f spacing=%.0f corner=%.2f border=%.3f", g_font_scale, g_spacing, g_corner, g_border_w);
    line(buf);
    snprintf(buf, sizeof(buf), "last: %s", g_last_event);
    line(buf);
    if (g_slider_n > 0) {
        snprintf(buf, sizeof(buf), "sliders: %.2f %.2f %.2f %.2f", g_slider_seen[0], g_slider_seen[1], g_slider_seen[2], g_slider_seen[3]);
        line(buf);
    }
    uint32_t total = 0;
    for (uint32_t c : g_clicks) {
        total += c;
    }
    snprintf(buf, sizeof(buf), "clicks total=%u [0..3]=%u/%u/%u/%u", total, g_clicks[0], g_clicks[1], g_clicks[2], g_clicks[3]);
    line(buf);
}

// ─── App callbacks ───────────────────────────────────────────────────
void game_init(void *) {
    g_batch.init();
    auto res = g_r.init(g_backend, nullptr, 0, 0);
    if (!res) {
        printf("[uilab] renderer init FAILED\n");
    }
    g_r.content_scale = g_content_scale; // resized properly in game_resize; this covers first frame
    g_ui.init();
    g_ui.theme.font_scale = g_font_scale;
    for (float &f : g_slider_seen) {
        f = 0.0f;
    }
    BuildPage(g_mode);
    BuildMaterialsResources();
    printf("[uilab] keys: D1-9 pages | M materials | Up/Down font | Left/Right spacing | +/- corner | B border | Space pause | R rebuild\n");
}

void game_resize(void *, uint32_t w, uint32_t h) {
    if (w > 0) {
        g_view_w = static_cast<float>(w);
    }
    if (h > 0) {
        g_view_h = static_cast<float>(h);
    }
    // MUST sync: ui render resets scissor to (0,0,w,h) every frame and the
    // backend scales it by content_scale (retina 2x). Stale 1.0 clips the
    // window to its top-left quarter. (Same line freecell has.)
    g_r.content_scale = g_content_scale;
    g_r.resize(w, h);
    g_embers.anchor_x = g_view_w - 150.0f;
    g_embers.anchor_y = g_view_h - 140.0f;
    BuildPage(g_mode);
}

void SetMode(LabMode m) noexcept {
    g_mode = m;
    BuildPage(g_mode);
}

void game_frame(void *, float dt, InputState &input) noexcept {
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        g_fps_ema += (fps - g_fps_ema) * 0.05f;
    }
    float udt = g_paused ? 0.0f : dt;
    if (!g_paused) {
        g_time += dt;
    }

    if (input.just_pressed(KeyCode::D1)) {
        SetMode(LabMode::PanelsLabels);
    } else if (input.just_pressed(KeyCode::D2)) {
        SetMode(LabMode::Buttons);
    } else if (input.just_pressed(KeyCode::D3)) {
        SetMode(LabMode::TogglesChecks);
    } else if (input.just_pressed(KeyCode::D4)) {
        SetMode(LabMode::Sliders);
    } else if (input.just_pressed(KeyCode::D5)) {
        SetMode(LabMode::TextFields);
    } else if (input.just_pressed(KeyCode::D6)) {
        SetMode(LabMode::StylesShapes);
    } else if (input.just_pressed(KeyCode::D7)) {
        SetMode(LabMode::Layout);
    } else if (input.just_pressed(KeyCode::D8)) {
        SetMode(LabMode::FocusNav);
    } else if (input.just_pressed(KeyCode::D9)) {
        SetMode(LabMode::Stress);
    } else if (input.just_pressed(KeyCode::M)) {
        SetMode(LabMode::Materials);
    } else if (input.just_pressed(KeyCode::Space)) {
        g_paused = !g_paused;
    } else if (input.just_pressed(KeyCode::R)) {
        BuildPage(g_mode); // rebuild (re-registers styles, resets widgets)
    } else if (input.just_pressed(KeyCode::B)) {
        g_border_w = (g_border_w > 0.001f) ? 0.0f : 0.08f;
        printf("[uilab] border=%.3f\n", g_border_w);
        BuildPage(g_mode);
    }
    if (input.just_pressed(KeyCode::Up)) {
        g_font_scale += 0.1f;
        if (g_font_scale > 2.0f) {
            g_font_scale = 2.0f;
        }
        g_ui.theme.font_scale = g_font_scale;
        printf("[uilab] font_scale=%.2f\n", g_font_scale);
    }
    if (input.just_pressed(KeyCode::Down)) {
        g_font_scale -= 0.1f;
        if (g_font_scale < 0.5f) {
            g_font_scale = 0.5f;
        }
        g_ui.theme.font_scale = g_font_scale;
        printf("[uilab] font_scale=%.2f\n", g_font_scale);
    }
    if (input.just_pressed(KeyCode::Right)) {
        g_spacing += 2.0f;
        if (g_spacing > 32.0f) {
            g_spacing = 32.0f;
        }
        printf("[uilab] spacing=%.0f\n", g_spacing);
        BuildPage(g_mode);
    }
    if (input.just_pressed(KeyCode::Left)) {
        g_spacing -= 2.0f;
        if (g_spacing < 0.0f) {
            g_spacing = 0.0f;
        }
        printf("[uilab] spacing=%.0f\n", g_spacing);
        BuildPage(g_mode);
    }
    for (uint8_t i = 0; i < input.text_count; ++i) {
        char c = input.text_input[i];
        if (c == '+' || c == '=') {
            g_corner += 0.05f;
            if (g_corner > 0.5f) {
                g_corner = 0.5f;
            }
            printf("[uilab] corner=%.2f\n", g_corner);
            BuildPage(g_mode);
        } else if (c == '-' || c == '_') {
            g_corner -= 0.05f;
            if (g_corner < 0.0f) {
                g_corner = 0.0f;
            }
            printf("[uilab] corner=%.2f\n", g_corner);
            BuildPage(g_mode);
        }
    }

    g_ui.handle(input);

    g_r.advance_time(udt);

    float t0 = NowMs();
    if (g_mode == LabMode::Materials) {
        // Materials demo: raw batches through the engine-ported lab
        // pipelines + EmberPool. No widgets on this page.
        g_ui.measure(g_r);
        float t1 = NowMs();
        g_ui.layout(g_r);
        float t2 = NowMs();

        (void)g_r.begin_frame();
        PassDesc pass{};
        pass.clear_color[0] = 0.07f;
        pass.clear_color[1] = 0.09f;
        pass.clear_color[2] = 0.12f;
        pass.clear_color[3] = 1.0f;
        pass.clear_depth    = 1.0f;
        pass.clear_stencil  = 0;
        pass.color_load     = LoadOp::Clear;
        pass.depth_load     = LoadOp::DontCare;
        pass.color_store    = StoreOp::Store;
        pass.depth_store    = StoreOp::DontCare;
        g_r.graph.begin_pass(pass);
        g_r.ortho(0.0f, g_view_w, g_view_h, 0.0f, -1.0f, 1.0f);
        g_r.upload_camera();

        RenderMaterials(udt);
        float t3 = NowMs();

        g_batch.reset();
        float hud_w = g_view_w - 16.0f;
        if (hud_w < 64.0f) {
            hud_w = 64.0f;
        }
        g_batch.add(8.0f + hud_w * 0.5f, 8.0f + HUD_H * 0.5f, hud_w, HUD_H, 0.0f, 0xCC000000, 5, 0, 0.08f, 0.0f, 0);
        g_r.flush_rounded_sprites(g_batch);
        DrawHud();

        g_r.graph.end_pass();
        g_r.submit();
        g_r.end_frame();
        return; // materials page skips ui measure/layout/render + bench
    }
    g_ui.measure(g_r);
    float t1 = NowMs();
    g_ui.layout(g_r); // MUST run every frame: positions HBox/VBox children
    float t2 = NowMs();

    (void)g_r.begin_frame();
    PassDesc pass{};
    pass.clear_color[0] = 0.07f;
    pass.clear_color[1] = 0.09f;
    pass.clear_color[2] = 0.12f;
    pass.clear_color[3] = 1.0f;
    pass.clear_depth    = 1.0f;
    pass.clear_stencil  = 0;
    pass.color_load     = LoadOp::Clear;
    pass.depth_load     = LoadOp::DontCare;
    pass.color_store    = StoreOp::Store;
    pass.depth_store    = StoreOp::DontCare;
    g_r.graph.begin_pass(pass);
    g_r.ortho(0.0f, g_view_w, g_view_h, 0.0f, -1.0f, 1.0f);
    g_r.upload_camera();

    g_batch.reset();
    g_ui.render(g_r, g_batch, udt);
    float t3 = NowMs();

    // HUD backing strip: full width so long lines never clip off the rect.
    g_batch.reset();
    float hud_w = g_view_w - 16.0f;
    if (hud_w < 64.0f) {
        hud_w = 64.0f;
    }
    g_batch.add(8.0f + hud_w * 0.5f, 8.0f + HUD_H * 0.5f, hud_w, HUD_H, 0.0f, 0xCC000000, 5, 0, 0.08f, 0.0f, 0);
    g_r.flush_rounded_sprites(g_batch);
    DrawHud();

    g_r.graph.end_pass();
    g_r.submit();
    g_r.end_frame();

    // Benchmark rolling average (skip paused frames for stable numbers)
    if (!g_paused) {
        g_ms_acc_m += (t1 - t0);
        g_ms_acc_l += (t2 - t1);
        g_ms_acc_r += (t3 - t2);
        if (++g_ms_n >= 30) {
            g_ms_measure = g_ms_acc_m / 30.0f;
            g_ms_layout  = g_ms_acc_l / 30.0f;
            g_ms_render  = g_ms_acc_r / 30.0f;
            g_ms_acc_m = g_ms_acc_l = g_ms_acc_r = 0.0f;
            g_ms_n                           = 0;
        }
    }
}

void game_cleanup(void *) {
    g_r.shutdown();
}

} // namespace

AppCallbacks markmos_main(int argc, char **argv) {
    // Optional initial page: ./mm_07_ui_lab d7 (also useful for screenshots)
    if (argc > 1 && argv && argv[1] && argv[1][0] == 'd' && argv[1][1] >= '1' && argv[1][1] <= '9' && argv[1][2] == '\0') {
        g_mode = static_cast<LabMode>(argv[1][1] - '1');
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'm' && argv[1][1] == '\0') {
        g_mode = LabMode::Materials;
    }
    return {.user_data = nullptr, .init = game_init, .frame = game_frame, .resize = game_resize, .cleanup = game_cleanup};
}

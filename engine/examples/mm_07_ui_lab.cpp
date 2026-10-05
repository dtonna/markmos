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
//   [Prev]/[Next] buttons (top-right) cycle pages — keys below are shortcuts:
//   D1 panels+labels | D2 buttons | D3 toggles+checkboxes | D4 sliders
//   D5 textfields (type real text) | D6 styles/shapes | D7 HBox/VBox layout
//   D8 focus nav (Tab/Shift-Tab) | D9 stress + benchmark | M materials
//   D0 listboxes | C comboboxes | O radioboxes | P progressbars
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
enum class LabMode : u8 {
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
    Listboxes, // scrollable text list (key D0)
    Comboboxes, // drop-down select + editable filter (key C)
    Radioboxes, // single-select option groups (key O)
    Progressbars, // display-only determinate bars, line + circle (key P)
    Scrollviews,  // a viewport for free-positioned content (key V)
    Treeviews,    // a ListBox with a node model (key T)
    Accordions,   // a header button whose content animates open/closed (key A)
    Datagrids,    // a sortable ListBox with column headers (key G)
    Images,       // image() + set_material() + set_style() (key I)
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
    case LabMode::Listboxes:
        return "listboxes";
    case LabMode::Comboboxes:
        return "comboboxes";
    case LabMode::Radioboxes:
        return "radioboxes";
    case LabMode::Progressbars:
        return "progressbars";
    case LabMode::Scrollviews:
        return "scrollviews";
    case LabMode::Treeviews:
        return "treeviews";
    case LabMode::Accordions:
        return "accordions";
    case LabMode::Datagrids:
        return "datagrids";
    case LabMode::Images:
        return "images";
    default:
        return "?";
    }
}

// ─── State ───────────────────────────────────────────────────────────
static Renderer    g_r;
static SpriteBatch g_batch;
static ui::Manager g_ui;

static LabMode g_mode      = LabMode::Buttons;
static f32   g_view_w    = 900.0f;
static f32   g_view_h    = 640.0f;
static f32   g_time      = 0.0f;
static bool    g_paused    = false;
static f32   g_fps_ema   = 60.0f;

// Live-tune knobs (keys mutate these, page rebuilds)
static f32 g_font_scale = 1.0f; // Up/Down, 0.5..2.0
static f32 g_spacing    = 8.0f; // Left/Right, 0..32
static f32 g_corner     = 0.12f; // +/-, 0..0.5 (style corner_r)
static f32 g_border_w   = 0.04f; // B toggles 0 / 0.08

// Interaction counters for the HUD
static u32 g_clicks[8] = {};
static f32    g_slider_seen[4];
static u8  g_slider_n = 0;
static char     g_last_event[64] = "-";

// Benchmark (D9 + HUD): CPU ms of each UI stage, averaged over 30 frames
static f32 g_ms_measure = 0.0f, g_ms_layout = 0.0f, g_ms_render = 0.0f;
static f32 g_ms_acc_m = 0.0f, g_ms_acc_l = 0.0f, g_ms_acc_r = 0.0f;
static u32 g_ms_n = 0;

// ─── Materials demo (key M): engine ports of the mm_06 lab shaders ───
static TextureHandle g_check_tex{};
static TextureHandle g_sinenrm_tex{};
static TextureHandle g_ember_tex{};
static SpriteBatch   g_ebatch;
static EmberPool     g_embers;

static bool MakeTexture(u16 w, u16 h, PixelFormat fmt, TextureHandle &out) noexcept {
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
    static constexpr u32 N = 64;
    static u32 pixels[N * N];
    for (u32 y = 0; y < N; ++y) {
        for (u32 x = 0; x < N; ++x) {
            bool even = ((x / 8) + (y / 8)) % 2 == 0;
            pixels[y * N + x] = even ? palette::WHITE : 0xFFB0B0B0u;
        }
    }
    auto &bk = *g_backend;
    (void)bk.update_texture(g_check_tex, pixels, 0, 0, N, N, 0, 0);
}

// Sine-heightfield normal map (smooth bumps) for NORMAL_MAP/PLASTIC demo.
static void BuildSineNormalTexture() noexcept {
    static constexpr u16 N = 128;
    static f32   height[N * N];
    static u32 px[N * N];
    for (u16 y = 0; y < N; ++y) {
        for (u16 x = 0; x < N; ++x) {
            f32 u = static_cast<f32>(x) / N;
            f32 v = static_cast<f32>(y) / N;
            height[y * N + x] = __builtin_sinf(u * mm_math::MM_TWO_PI * 3.0f) * __builtin_sinf(v * mm_math::MM_TWO_PI * 3.0f);
        }
    }
    auto hat = [&](int x, int y) noexcept -> f32 {
        x = (x + N) % N;
        y = (y + N) % N;
        return height[static_cast<u32>(y) * N + static_cast<u32>(x)];
    };
    for (u16 y = 0; y < N; ++y) {
        for (u16 x = 0; x < N; ++x) {
            f32 dhdx = (hat(x + 1, y) - hat(x - 1, y)) * 0.5f;
            f32 dhdy = (hat(x, y + 1) - hat(x, y - 1)) * 0.5f;
            f32 nx = -dhdx * 2.0f, ny = -dhdy * 2.0f, nz = 1.0f;
            f32 inv = 1.0f / __builtin_sqrtf(nx * nx + ny * ny + nz * nz);
            auto enc = [&](f32 f) noexcept -> u32 {
                int q = static_cast<int>((f * 0.5f + 0.5f) * 255.0f + 0.5f);
                if (q < 0) q = 0;
                if (q > 255) q = 255;
                return static_cast<u32>(q);
            };
            u32 r = enc(nx * inv), g = enc(ny * inv), b = enc(nz * inv);
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
        static u32 glow[EMBER_GLOW_SIZE * EMBER_GLOW_SIZE];
        MakeEmberGlowTexture(glow);
        (void)bk.update_texture(g_ember_tex, glow, 0, 0, EMBER_GLOW_SIZE, EMBER_GLOW_SIZE, 0, 0);
    }
    g_ebatch.init();
    g_embers.init(g_view_w - 150.0f, g_view_h - 140.0f, 24, 12345u);
}

static f32 NowMs() noexcept {
    return static_cast<f32>(clock()) * 1000.0f / static_cast<f32>(CLOCKS_PER_SEC);
}

// ─── Callbacks ───────────────────────────────────────────────────────
void OnLabButton(u16 id, void *) noexcept {
    // Map widget id -> 0..7 bucket by order of creation on button pages.
    // (Coarse but enough for a click counter HUD.)
    u32 b = static_cast<u32>(id) % 8;
    if (g_clicks[b] < 999999u) {
        ++g_clicks[b];
    }
    snprintf(g_last_event, sizeof(g_last_event), "click id=%u", id);
}

void OnLabSlider(u16 id, f32 v) noexcept {
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

void OnLabToggle(u16 id, void *) noexcept {
    snprintf(g_last_event, sizeof(g_last_event), "toggle id=%u state=%d", id, g_ui.is_toggled(id) ? 1 : 0);
}

void OnLabListbox(u16 id, void *) noexcept {
    snprintf(g_last_event, sizeof(g_last_event), "listbox id=%u sel=%d", id, g_ui.get_listbox_selected(id));
}

void OnLabCombo(u16 id, void *) noexcept {
    snprintf(g_last_event, sizeof(g_last_event), "combo id=%u sel=%d open=%d", id, g_ui.get_combobox_selected(id),
             g_ui.is_combobox_open(id) ? 1 : 0);
}

void OnLabRadio(u16 id, void *) noexcept {
    snprintf(g_last_event, sizeof(g_last_event), "radio id=%u sel=%d", id, g_ui.is_radiobox_selected(id) ? 1 : 0);
}

void SetMode(LabMode m) noexcept; // defined below (used by nav callbacks)

void OnPrevPage(u16, void *) noexcept {
    int n = static_cast<int>(LabMode::_Count);
    int m = (static_cast<int>(g_mode) + n - 1) % n;
    SetMode(static_cast<LabMode>(m));
}

void OnNextPage(u16, void *) noexcept {
    int n = static_cast<int>(LabMode::_Count);
    int m = (static_cast<int>(g_mode) + 1) % n;
    SetMode(static_cast<LabMode>(m));
}

// Style used by the gallery: rounded rect with live-tuned corner/border.
u8 GalleryStyle() noexcept {
    ui::WidgetStyle s{};
    s.border_color = palette::FOCUS_BLUE; // 0xAARRGGBB
    s.border_width = g_border_w;
    s.corner_r     = g_corner;
    s.shape        = ui::shape_type::ROUNDED_RECT;
    s._pad[0] = s._pad[1] = s._pad[2] = 0;
    return g_ui.register_style(s);
}

// Root panels are BACKGROUNDS, not framed cards: same corner, no border.
// (A proportional border on a ~1800pt panel is a ~72pt band that swallows
// children at normal offsets — content must clear border bands, and bg
// panels opt out. Bordered cards keep GalleryStyle.)
u8 GalleryBgStyle() noexcept {
    ui::WidgetStyle s{};
    s.border_color = 0;
    s.border_width = 0.0f;
    s.corner_r     = g_corner;
    s.shape        = ui::shape_type::ROUNDED_RECT;
    s._pad[0] = s._pad[1] = s._pad[2] = 0;
    return g_ui.register_style(s);
}

// PILL style: corner_r 0.5 normalizes against the widget's SHORT axis, so a
// thin track (11pt tall) gets a radius of 5.5pt - fully rounded ends. A subtle
// corner_r like 0.12 would round that same track by only 1.3pt and read as
// square. The radius is per-axis normalized by min(w,h), which is the point
// the pixel-space SDF fix made honest.
u8 GalleryPillStyle(u8 border = 1) noexcept {
    ui::WidgetStyle s{};
    s.border_color = border ? palette::FOCUS_BLUE : 0;
    s.border_width = border ? g_border_w : 0.0f;
    s.corner_r     = 0.5f;
    s.shape        = ui::shape_type::ROUNDED_RECT;
    return g_ui.register_style(s);
}

// Gradient FILL style. The gradient is a TEXTURE the app generates once
// (Renderer::make_gradient_texture) and the style points at it: the rounded
// SDF path samples it as the fill, so the corner radius, the per-side borders
// and the edge AA all survive. No new shader, no new pipeline.
u8 GalleryGradientStyle(u32 c0, u32 c1, bool vertical) noexcept {
    const TextureHandle t = g_r.make_gradient_texture(8, 64, c0, c1, vertical);
    return g_ui.register_style(ui::gradient_style(t, g_corner, palette::FOCUS_BLUE, g_border_w));
}

// Style with a REAL background texture (WidgetStyle::bg_tex). The rounded
// border/corner survive: the UI flushes a textured batch through the SDF
// pipeline, not the plain sprite shader.
u8 GalleryTexStyle(TextureHandle tex) noexcept {
    ui::WidgetStyle s{};
    s.bg_tex       = tex;
    s.border_color = palette::FOCUS_BLUE;
    s.border_width = g_border_w;
    s.corner_r     = g_corner;
    s.shape        = ui::shape_type::ROUNDED_RECT;
    s._pad[0] = s._pad[1] = s._pad[2] = 0;
    return g_ui.register_style(s);
}

// Persistent page nav: rebuilt with every page (BuildPage clears the pool).
// Top-right of the window; HUD text lines start at x=10 and are short.
void BuildNav() noexcept {
    u8 st = GalleryStyle();
    f32 bx = g_view_w - 232.0f;
    if (bx < 8.0f) bx = 8.0f;
    g_ui.button(bx, 8.0f, 108.0f, 36.0f, "< Prev", palette::BUTTON_BG, palette::WHITE, OnPrevPage, UINT16_MAX, 0.36f, st);
    g_ui.button(bx + 116.0f, 8.0f, 108.0f, 36.0f, "Next >", palette::BUTTON_BG, palette::WHITE, OnNextPage, UINT16_MAX, 0.36f, st);
}

// HUD reserves the top strip; pages live below it (no overlap).
static constexpr f32 HUD_H  = 196.0f;
static constexpr f32 PAGE_Y = 210.0f;

// ─── Page builders ───────────────────────────────────────────────────
// Modal demo state: the root id of the open dialog, or UINT16_MAX. Kept as a
// file global (not a widget field) because the engine scopes input by it.
static u16 s_modal_root = UINT16_MAX;
static void OnLabOpenModal(u16, void *) noexcept { s_modal_root = 1; } // any non-invalid id
static void OnLabCloseModal(u16, void *) noexcept { s_modal_root = UINT16_MAX; }

// Tab strip demo state: the app swaps its own content (the engine knows
// nothing about pages - the strip is only a row of cells + one active index).
static u16          g_tab_body = UINT16_MAX;
static const char *const k_tab_body[] = {
    "tab 0: 12 deals in the pool",
    "tab 1: best time 2:41",
    "tab 2: tap a cell, or focus + Up/Dn",
};
static void OnLabTab(u16, f32 tab) noexcept {
    const int i = static_cast<int>(tab);
    if (g_tab_body != UINT16_MAX && i >= 0 && i < 3) {
        g_ui.set_text(g_tab_body, k_tab_body[i]);
    }
}

void BuildPanelsLabels() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Panels + Labels (D1)", palette::WHITE, 0.50f, root);
    g_ui.label(20.0f, 44.0f, "nested panel below has WF_CLIP", palette::GRAY_LIGHT, 0.36f, root);
    u16 inner = g_ui.panel(20.0f, 76.0f, 300.0f, 150.0f, 0xFF3A3A4A, root, st);
    g_ui.set_clip(inner, true);
    g_ui.label(16.0f, 10.0f, "clipped child 1", palette::WHITE, 0.40f, inner);
    g_ui.label(16.0f, 40.0f, "clipped child 2 (overflow...)", palette::WHITE, 0.40f, inner);
    g_ui.label(16.0f, 70.0f, "clipped child 3", palette::WHITE, 0.40f, inner);
    g_ui.label(16.0f, 100.0f, "clipped child 4", palette::WHITE, 0.40f, inner);
    g_ui.label(16.0f, 142.0f, "clipped child 5 (cut!)", palette::WHITE, 0.40f, inner);
    u16 inner2 = g_ui.panel(340.0f, 76.0f, 300.0f, 150.0f, 0xFF2A4A2A, root, st);
    g_ui.label(16.0f, 10.0f, "plain nested panel", palette::WHITE, 0.40f, inner2);
    g_ui.label(16.0f, 44.0f, "scale 0.30 / 0.50 / 0.70", palette::GRAY_LIGHT, 0.30f, inner2);
    g_ui.label(16.0f, 70.0f, "scale 0.30 / 0.50 / 0.70", palette::GRAY_LIGHT, 0.50f, inner2);
    g_ui.label(16.0f, 110.0f, "scale 0.30 / 0.50 / 0.70", palette::GRAY_LIGHT, 0.70f, inner2);

    // Modal: dim backdrop + input routing + focus trap + tap-outside/Back to
    // dismiss. Built LAST on purpose - the dim is emitted where the subtree
    // starts in Pass 1, so anything built before it sits under the dim.
    // Multi-line label: '\\n' was SKIPPED by measure(), so a two-line label
    // silently lost the character. content_h now covers every line.
    static const char *kTwoLine = "two lines now work:\nthe second line is not eaten";
    u16          ml       = g_ui.label(20.0f, 300.0f, "placeholder", palette::WHITE, 0.34f, root);
    g_ui.set_text_ext(ml, kTwoLine);

    // Tooltip: hover a widget, wait, the hint appears above it. The string is
    // caller-owned - the engine never copies or frees it.
    static const char *kTipA = "tooltips are drawn as an overlay,\nnot a widget: no focus, no input";
    static const char *kTipB = "long text is caller-owned too:\nset_text_ext() has no 48 byte cap";
    u16          ta     = g_ui.button(20.0f, 340.0f, 190.0f, 36.0f, "Hover for a tooltip", 0xFF3A6A5C, palette::WHITE, OnLabButton, root, 0.36f, st);
    u16          tb     = g_ui.button(220.0f, 340.0f, 190.0f, 36.0f, "Second tooltip", 0xFF3A6A5C, palette::WHITE, OnLabButton, root, 0.36f, st);
    g_ui.tooltip(ta, kTipA, 0.4f);
    g_ui.tooltip(tb, kTipB, 0.4f);

    g_ui.button(20.0f, 240.0f, 190.0f, 40.0f, "Open modal dialog", palette::BLUE_DARK, palette::WHITE, OnLabOpenModal, root, 0.40f, st);
    g_ui.label(220.0f, 246.0f, "backdrop, focus trap, Back / outside = close", palette::GRAY_LIGHT, 0.32f, root);
    if (s_modal_root != UINT16_MAX) {
        u16 dlg = g_ui.panel(260.0f, 120.0f, 320.0f, 220.0f, 0xFF33334A, UINT16_MAX, st);
        g_ui.set_clip(dlg, true);
        g_ui.label(16.0f, 12.0f, "Modal dialog", palette::WHITE, 0.44f, dlg);
        g_ui.label(16.0f, 40.0f, "Tab stays in here; outside is dead", palette::GRAY_LIGHT, 0.32f, dlg);
        g_ui.button(16.0f, 80.0f, 130.0f, 36.0f, "Confirm", palette::TOGGLE_GREEN, palette::WHITE, OnLabButton, dlg, 0.36f, st);
        g_ui.button(170.0f, 80.0f, 130.0f, 36.0f, "Cancel", 0xFFB85C5C, palette::WHITE, OnLabCloseModal, dlg, 0.36f, st);
        g_ui.toggle(16.0f, 130.0f, 80.0f, 28.0f, "focusable", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, true, OnLabButton, dlg, st);
        g_ui.slider(120.0f, 130.0f, 180.0f, 24.0f, 0.6f, OnLabSlider, dlg, st);
        g_ui.begin_modal(dlg, OnLabCloseModal, nullptr);
        s_modal_root = dlg;
    }
}

void BuildButtons() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 10.0f, "Buttons (D2) - hover/press/click", palette::WHITE, 0.50f, root);
    g_ui.button(20.0f, 40.0f, 160.0f, 44.0f, "Normal", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, st);
    g_ui.button(200.0f, 40.0f, 160.0f, 44.0f, "Accent", palette::BLUE_DARK, palette::WHITE, OnLabButton, root, 0.40f, st);
    u16 dis = g_ui.button(380.0f, 40.0f, 160.0f, 44.0f, "Disabled", palette::BUTTON_BG, palette::GRAY_MID, OnLabButton, root, 0.40f, st);
    g_ui.set_enabled(dis, false);
    u16 custom = g_ui.button(20.0f, 92.0f, 340.0f, 44.0f, "", 0, palette::WHITE, OnLabButton, root, 0.40f, st,
                                  static_cast<u8>(ui::shape_type::CUSTOM),
                                  [](u16, Renderer &r, SpriteBatch &b, f32 ax, f32 ay, f32, void *) noexcept {
        // Custom-drawn button: teal pill (proves on_draw path).
        b.add(ax + 170.0f, ay + 22.0f, 340.0f, 44.0f, 0.0f, 0xFF00CED1, 5, 0, 0.5f, 0.0f, 0);
        r.flush_rounded_sprites(b);
        b.reset();
        r.draw_text(r.default_font, "Custom on_draw", ax + 100.0f, ay + 14.0f, 0xFF000000, 0.40f);
    });
    g_ui.label(20.0f, 142.0f, "click counters live on the HUD (top-left)", palette::GRAY_LIGHT, 0.36f, root);
    // Label fit (engine guarantee): a label that does not fit the PADDED content
    // box is shrunk to fit rather than drawn outside the button. Before the
    // clamp these three all spilled: #1 past both sides, #2 through the top and
    // bottom, #3 on both axes at once.
    g_ui.label(20.0f, 168.0f, "label fit: shrunk to the padded box, never clipped", palette::GRAY_LIGHT, 0.36f, root);
    // Style-level background texture: the fill samples a real texture while
    // the style's rounded corner + border still draw (rounded SDF flush).
    u8 tex_st = GalleryTexStyle(g_check_tex);
    g_ui.label(20.0f, 252.0f, "style bg_tex: texture + rounded corner + border", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.button(20.0f, 280.0f, 160.0f, 48.0f, "bg_tex", palette::WHITE, 0xFF202020, OnLabButton, root, 0.40f, tex_st);
    g_ui.button(200.0f, 280.0f, 160.0f, 48.0f, "bg_tex 2", palette::WHITE, 0xFF202020, OnLabButton, root, 0.40f, tex_st);

    // Style-level hard drop shadow: one extra quad in the same batch (no new
    // pipeline, no extra draw). shadow_grow is what makes a hard shadow read
    // as depth rather than a second border.
    u8 sh_st = GalleryStyle();
    g_ui.styles[sh_st].shadow_color = 0xA0000000;
    g_ui.styles[sh_st].shadow_dx     = 4.0f;
    g_ui.styles[sh_st].shadow_dy     = 6.0f;
    g_ui.styles[sh_st].shadow_grow   = 4.0f;
    g_ui.label(20.0f, 336.0f, "style shadow: hard drop, 1 quad (reads on light cards)", palette::GRAY_LIGHT, 0.36f, root);
    // A hard shadow only reads where the backdrop is LIGHTER than the shadow
    // itself, so the demo puts both buttons on a light strip - the real use
    // case (a card on a light page). On the dark panel a black shadow is
    // invisible no matter how opaque it is.
    g_ui.panel(12.0f, 352.0f, 360.0f, 72.0f, 0xFFF0F2F6, root, bgst);
    g_ui.button(20.0f, 364.0f, 160.0f, 48.0f, "Shadowed", 0xFFE6E9EF, 0xFF202028, OnLabButton, root, 0.40f, sh_st);
    g_ui.button(200.0f, 364.0f, 160.0f, 48.0f, "No shadow", 0xFFE6E9EF, 0xFF202028, OnLabButton, root, 0.40f, st);
    g_ui.button(20.0f, 196.0f, 160.0f, 44.0f, "A very long button label", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, st);
    g_ui.button(200.0f, 196.0f, 160.0f, 36.0f, "Scale 1.0", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 1.00f, st);
    g_ui.button(380.0f, 196.0f, 60.0f, 24.0f, "Tiny", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, st);
}

// Row renderer for the listbox below: coloured left field + right-aligned
// value, i.e. the two-column look a real data list needs.
static void OnLabRichRow(u16, Renderer &r, SpriteBatch &b, f32 row_x, f32 row_y, f32 row_w, f32 row_h, int item,
                         void *) noexcept {
    static const char *lvals[] = {
        "42", "55", "61", "63",
        "70", "77", "80", "84",
        "92", "98", "40", "66",
    };
    if (item < 0 || item >= 12) {
        return;
    }
    static const char *labels[] = {
        "Easy", "Easy", "Medium", "Medium",
        "Medium", "Hard", "Hard", "Hard",
        "Expert", "Expert", "Easy", "Medium",
    };
    static const u32 cols[] = {
        palette::TOGGLE_GREEN, palette::TOGGLE_GREEN, 0xFFB5C85C, 0xFFB5C85C,
        0xFFB5C85C, 0xFF8A6FB5, 0xFF8A6FB5, 0xFF8A6FB5,
        0xFFB55C5C, 0xFFB55C5C, palette::TOGGLE_GREEN, 0xFFB5C85C,
    };
    // Zebra stripe behind the row (the app's business; the selection
    // highlight underneath is still the engine's).
    if ((item & 1) != 0) {
        b.add(row_x + row_w * 0.5f, row_y + row_h * 0.5f, row_w, row_h, 0.0f, 0x18FFFFFF, 0, 0, 0.0f, 0.0f, 0);
        r.flush_sprites(b);
        b.reset();
    }
    // Two fields through the row helper: it owns the vertical centring and the
    // right-edge alignment, so this callback is just data.
    //
    // The right field's box stops short of the row's right edge by the
    // SCROLLBAR, derived from the same constant the engine uses - the content
    // rect is the full inner width and does NOT inset for it, so a
    // right-aligned field would slide under the thumb. (The default
    // one-string row never noticed: its text is left-aligned.) A 6pt margin
    // on top keeps the digits off the thumb's edge.
    const f32 sb = ui::ListboxMetrics::K_BAR_RATIO * row_h;
    const ui::rows::Field fields[2] = {
        {labels[item], 6.0f, row_w - 18.0f, cols[item], false},
        {lvals[item], row_w - 60.0f, 48.0f - sb - 6.0f, 0xFFCCCCCC, true},
    };
    ui::rows::draw(r, fields, 2, row_x, row_y, row_h, 0.34f);
}

void BuildListboxes() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Listboxes (D0) - tap select, wheel/drag scroll", palette::WHITE, 0.50f, root);
    // Item storage is static: the widget keeps pointers, caller owns memory.
    static const char* deals[] = {
        "Easy #1042", "Easy #2088", "Medium #1101", "Medium #1156",
        "Medium #1210", "Hard #1304", "Hard #1377", "Hard #1440",
        "Expert #1501", "Expert #1566", "Easy #2013", "Medium #2077",
        "Hard #2142", "Hard #2208", "Expert #2311", "Expert #2407",
    };
    static const char* diffs[] = {"Easy", "Medium", "Hard"};
    g_ui.listbox(20.0f, 52.0f, 240.0f, 200.0f, deals, 16, OnLabListbox, root, st);

    // Tab strip + separators. The rule needs no render code of its own (Pass 1
    // draws any visible widget with a bg_color); the strip draws bar + active
    // cell + labels and reports changes. The app swaps its own content.
    static const char *const kTabs[] = {"Deals", "Stats", "Help"};
    g_ui.separator(20.0f, 296.0f, 240.0f, root);
    g_ui.tabbar(20.0f, 304.0f, 240.0f, 32.0f, kTabs, 3, root, 0, OnLabTab);
    g_ui.separator_v(268.0f, 304.0f, 32.0f, root);
    g_tab_body = g_ui.label(20.0f, 344.0f, k_tab_body[0], palette::WHITE, 0.30f, root);
    g_ui.listbox(280.0f, 52.0f, 200.0f, 140.0f, diffs, 3, OnLabListbox, root, st);

    // Custom row contents: the app draws the row, the engine keeps scrolling,
    // the selection highlight, hit-testing and clipping. This is what a
    // multi-field row (difficulty + seed + moves + date) needs - and what
    // freecell's solved-games browser was hand-rolling in 242 lines.
    static const char* rows[] = {
        "Easy #1042", "Easy #2088", "Medium #1101", "Medium #1156",
        "Medium #1210", "Hard #1304", "Hard #1377", "Hard #1440",
        "Expert #1501", "Expert #1566", "Easy #2013", "Medium #2077",
    };
    static const char* vals[] = {
        "42 moves", "55 moves", "61 moves", "63 moves",
        "70 moves", "77 moves", "80 moves", "84 moves",
        "92 moves", "98 moves", "40 moves", "66 moves",
    };
    u16 rich = g_ui.listbox(280.0f, 310.0f, 300.0f, 100.0f, rows, 12, OnLabListbox, root, st);
    g_ui.set_row_renderer(rich, OnLabRichRow, nullptr);
    g_ui.label(20.0f, 262.0f, "right: custom row renderer", palette::GRAY_LIGHT, 0.32f, root);
    g_ui.label(20.0f, 284.0f, "(engine keeps scroll + select)", palette::GRAY_LIGHT, 0.32f, root);

    // set_text_align: heading centred, stat right-aligned. Both pin their
    // width - a label is AUTO_W, so an unpinned box hugs the text and every
    // alignment collapses to LEFT.
    {
        // All three share ONE box (the right column's free gap, x 280..760),
        // so the shared edges are visible: the stat's right edge IS the box's
        // right edge, and both centred lines share a centre.
        u16 h1 = g_ui.label(280.0f, 200.0f, "DIFFICULTIES", palette::WHITE, 0.40f, root);
        g_ui.set_text_align(h1, ui::text_align::CENTER, 480.0f);
        u16 h2 = g_ui.label(280.0f, 236.0f, "two lines\ncentred as a block", palette::GRAY_LIGHT, 0.30f, root);
        g_ui.set_text_align(h2, ui::text_align::CENTER, 480.0f);
        u16 s1 = g_ui.label(280.0f, 284.0f, "12 / 12 shown", palette::GRAY_MID, 0.28f, root);
        g_ui.set_text_align(s1, ui::text_align::RIGHT, 480.0f);
    }
    g_ui.label(280.0f, 210.0f, "selection + scroll", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.label(280.0f, 240.0f, "Up/Dn moves selection", palette::GRAY_LIGHT, 0.36f, root);
}

void BuildComboboxes() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Comboboxes (C) - tap field, pick row, Esc closes", palette::WHITE, 0.50f, root);
    // Item storage is static: the widget keeps pointers, caller owns memory.
    static const char* deals[] = {
        "Easy #1042", "Easy #2088", "Medium #1101", "Medium #1156",
        "Medium #1210", "Hard #1304", "Hard #1377", "Hard #1440",
        "Expert #1501", "Expert #1566", "Easy #2013", "Medium #2077",
        "Hard #2142", "Hard #2208", "Expert #2311", "Expert #2407",
    };
    static const char* diffs[] = {"Easy", "Medium", "Hard"};
    // Each popup animates differently: set_popup_anim is per widget, so one
    // screen can show several. Left column = scale+fade / roller door,
    // right column = slide / plain fade.
    u16 c_scale  = g_ui.combobox(20.0f, 52.0f, 240.0f, 40.0f, deals, 16, false, OnLabCombo, root, st);
    u16 c_garage = g_ui.combobox(20.0f, 108.0f, 240.0f, 40.0f, deals, 16, true, OnLabCombo, root, st);
    u16 c_slide  = g_ui.combobox(280.0f, 52.0f, 200.0f, 40.0f, diffs, 3, false, OnLabCombo, root, st);
    u16 c_fade   = g_ui.combobox(280.0f, 108.0f, 200.0f, 40.0f, diffs, 3, false, OnLabCombo, root, st);
    g_ui.set_popup_anim(c_garage, ui::popup_anim_mode::GARAGE);
    g_ui.set_popup_anim(c_slide, ui::popup_anim_mode::SLIDE);
    g_ui.set_popup_anim(c_fade, ui::popup_anim_mode::FADE);
    g_ui.label(20.0f, 168.0f, "left: scale+fade / garage", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.label(280.0f, 168.0f, "right: slide / fade", palette::GRAY_LIGHT, 0.36f, root);
    // The item counts are load-bearing for the keyboard half below: the LEFT
    // column has 16 rows against a 6-row popup, so PageDown has somewhere to go
    // and the popup must scroll to follow it. The right column fits entirely, so
    // its page keys land on the last row - correct, but it cannot show the jump.
    g_ui.label(20.0f, 200.0f, "16 rows: selection lands on the HUD", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.label(280.0f, 200.0f, "3 rows: popup fits, no scroll", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.label(280.0f, 232.0f, "tap outside / Esc cancels, wheel scrolls", palette::GRAY_LIGHT, 0.36f, root);
    // The keyboard half, which the page never stated. Space and Enter share one
    // action (Confirm), so "Space opens, Enter commits" is a UI-layer distinction;
    // arrows own the highlight while focused; and Tab is a FOCUS key - it used to
    // arrive as MenuDown, which this widget matched, so Tab paged the popup and
    // clamped at the last row and you could never Tab out. See the FocusNav (D8)
    // page for the three widgets that used to trap you, all in one focus ring.
    g_ui.label(20.0f, 232.0f, "Space/Enter open, arrows move", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.label(20.0f, 264.0f, "Enter commits, Esc cancels", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.label(20.0f, 296.0f, "Tab cancels an open popup and leaves", 0xFFAAAAFF, 0.36f, root);
    // F4 / Alt+Down toggle, PageUp/PageDown jump a viewport. The toggle CANCELS on
    // the way out (so it is not a second Enter) - stated on the page because
    // "toggle" and "commit" look identical until a player loses a selection.
    g_ui.label(280.0f, 264.0f, "F4 / Alt+Down: toggle open + shut", palette::GRAY, 0.36f, root);
    g_ui.label(280.0f, 296.0f, "PageUp / PageDown: one popup page", palette::GRAY, 0.36f, root);
}

void BuildRadioboxes() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Radioboxes (O) - one selected per group", palette::WHITE, 0.50f, root);
    // Group 1: difficulty (Easy pre-selected). Group 2: theme (System pre-selected).
    g_ui.label(20.0f, 46.0f, "Difficulty", palette::GRAY_LIGHT, 0.40f, root);
    g_ui.radiobox(20.0f, 72.0f, "Easy", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, 1, true, OnLabRadio, root, st);
    g_ui.radiobox(20.0f, 112.0f, "Medium", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, 1, false, OnLabRadio, root, st);
    g_ui.radiobox(20.0f, 152.0f, "Hard", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, 1, false, OnLabRadio, root, st);
    g_ui.label(280.0f, 46.0f, "Theme", palette::GRAY_LIGHT, 0.40f, root);
    g_ui.radiobox(280.0f, 72.0f, "Light", palette::ACCENT_BLUE, palette::GRAY_DARK, palette::WHITE, 2, false, OnLabRadio, root, st);
    g_ui.radiobox(280.0f, 112.0f, "Dark", palette::ACCENT_BLUE, palette::GRAY_DARK, palette::WHITE, 2, false, OnLabRadio, root, st);
    g_ui.radiobox(280.0f, 152.0f, "System", palette::ACCENT_BLUE, palette::GRAY_DARK, palette::WHITE, 2, true, OnLabRadio, root, st);
    g_ui.label(280.0f, 196.0f, "tap selects, re-tap is silent", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.label(20.0f, 196.0f, "selection lands on the HUD", palette::GRAY_LIGHT, 0.36f, root);
}

static u16 s_anim_bar = UINT16_MAX; // progressbar driven live by g_time
static u16 s_grad_bar = UINT16_MAX;
static u16 s_stripe_bar = UINT16_MAX;
static u16 s_anim_c0  = UINT16_MAX; // circles sweep with phase offsets
static u16 s_anim_c1  = UINT16_MAX;

void BuildProgressbars() noexcept {
    u8 st = GalleryPillStyle(); // pill: the bars' radius normalizes against their short axis
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Progressbars (P) - display-only, values ease via update()", palette::WHITE, 0.50f, root);
    g_ui.set_clip(root, true); // a page is a container: its children clip to it
    g_ui.progressbar(20.0f, 52.0f, 400.0f, 28.0f, 0.0f, root, 1.0f, st);
    g_ui.progressbar(20.0f, 96.0f, 400.0f, 28.0f, 0.5f, root, 1.0f, st);
    g_ui.progressbar(20.0f, 140.0f, 400.0f, 28.0f, 1.0f, root, 1.0f, st);
    s_anim_bar = g_ui.progressbar(20.0f, 184.0f, 400.0f, 28.0f, 0.0f, root, 1.0f, st);
    g_ui.label(20.0f, 222.0f, "bottom bar follows sin(time); circles sweep without shaders", palette::GRAY_LIGHT, 0.36f, root);
    // Gradient FILL (WidgetStyle::fill_tex). The ramp is a texture the app
    // generated once; the fill costs its own flush because a texture bind is
    // per-flush. The ramp spans the FILL, so a half-full bar shows the whole
    // ramp inside its own width (bright leading edge).
    {
        ui::WidgetStyle gs{};
        gs.border_color = palette::FOCUS_BLUE;
        gs.border_width = g_border_w;
        gs.corner_r     = g_corner;
        gs.shape        = ui::shape_type::ROUNDED_RECT;
        gs.fill_tex      = g_r.make_gradient_texture(8, 64, 0xFF1E7A4B, 0xFF7BE0A0, true);
        u8 grad_st = g_ui.register_style(gs);
        s_grad_bar            = g_ui.progressbar(20.0f, 250.0f, 400.0f, 28.0f, 0.0f, root, 1.0f, grad_st);
        g_ui.progressbar(20.0f, 290.0f, 400.0f, 28.0f, 0.62f, root, 1.0f, grad_st);
        g_ui.label(20.0f, 330.0f, "gradient fill: ramp spans the fill, own flush (style fill_tex)", palette::GRAY_LIGHT, 0.32f, root);

        // Striped fill (WidgetStyle::stripe_pitch / stripe_color). The phase
        // walks in update(), so the bands drift; render only reads it. Bands
        // are anchored at the fill's left edge, so the pitch stays constant as
        // the value grows instead of stretching with it.
        ui::WidgetStyle ss{};
        ss.border_color  = palette::FOCUS_BLUE;
        ss.border_width  = g_border_w;
        ss.corner_r      = g_corner;
        ss.shape         = ui::shape_type::ROUNDED_RECT;
        ss.stripe_pitch  = 14.0f;
        ss.stripe_color  = 0x30FFFFFF;
        u8 stripe_st = g_ui.register_style(ss);
        s_stripe_bar       = g_ui.progressbar(20.0f, 366.0f, 400.0f, 26.0f, 0.0f, root, 1.0f, stripe_st);

        // Striped AND gradient together: the bands ride over the ramp.
        ui::WidgetStyle sg            = ss;
        sg.fill_tex                   = g_r.make_gradient_texture(8, 64, 0xFF7A4BE0, 0xFF4BE0A0, true);
        sg.stripe_pitch               = 10.0f;
        u8      stripe_grad_st   = g_ui.register_style(sg);
        g_ui.progressbar(440.0f, 366.0f, 200.0f, 26.0f, 0.55f, root, 1.0f, stripe_grad_st);
        // Value-label canary: this bar ends 10pt from the panel's right edge,
        // so there is no room for a label OUTSIDE it - it must fold inside,
        // right-aligned. The one above has room and stays outside.
        g_ui.progressbar(560.0f, 330.0f, 250.0f, 26.0f, 0.45f, root, 1.0f, stripe_grad_st);
        g_ui.label(560.0f, 362.0f, "no room outside -> label folds inside", palette::GRAY_LIGHT, 0.30f, root);
        g_ui.label(20.0f, 398.0f, "striped fill: bands drift in update(), pitch stays fixed (stripe_pitch)", palette::GRAY_LIGHT, 0.30f, root);
    }

    s_anim_c0 = g_ui.progressbar(480.0f, 52.0f, 120.0f, 120.0f, 0.33f, root, 1.0f, st, (u8)ui::shape_type::CIRCLE);
    s_anim_c1 = g_ui.progressbar(480.0f, 184.0f, 120.0f, 120.0f, 0.75f, root, 1.0f, st, (u8)ui::shape_type::CIRCLE);

    // Gradient RING: the same fill_tex field, but a SWEEP gradient (colour
    // follows the ring angle), so the ramp tracks the sweep head instead of
    // running across the bounding box. Fixed value so the tip is deterministic
    // in a screenshot.
    {
        ui::WidgetStyle rs{};
        rs.border_color = palette::FOCUS_BLUE;
        rs.border_width = g_border_w;
        rs.corner_r     = 0.5f;
        rs.shape        = ui::shape_type::CIRCLE;
        rs.fill_tex      = g_r.make_sweep_gradient_texture(64, 0xFF1E7A4B, 0xFFB0FFC8);
        u8 ring_st = g_ui.register_style(rs);
        g_ui.progressbar(640.0f, 52.0f, 120.0f, 120.0f, 0.62f, root, 1.0f, ring_st, (u8)ui::shape_type::CIRCLE);
        g_ui.progressbar(640.0f, 184.0f, 120.0f, 120.0f, 0.30f, root, 1.0f, ring_st, (u8)ui::shape_type::CIRCLE);
        g_ui.label(620.0f, 320.0f, "sweep grad", palette::GRAY_LIGHT, 0.30f, root);
    }
}

void BuildTogglesChecks() noexcept {
    u8 st = GalleryPillStyle(); // a pill toggle wants corner_r 0.5
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Toggles + Checkboxes (D3)", palette::WHITE, 0.50f, root);
    // NOTE: toggle()/checkbox() have no scale param; text size follows the
    // pool scale field (default 1.0), like freecell does for shape.
    u16 t0 = g_ui.toggle(20.0f, 52.0f, 140.0f, 40.0f, "Sound", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, true, OnLabToggle, root, st);
    u16 t1 = g_ui.toggle(20.0f, 102.0f, 140.0f, 40.0f, "Effects", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, false, OnLabToggle, root, st);
    u16 t2 = g_ui.toggle(20.0f, 152.0f, 140.0f, 40.0f, "Music", palette::ACCENT_BLUE, palette::GRAY_DARK, palette::WHITE, false, OnLabToggle, root, st);
    g_ui.pool[t0].scale = 0.45f;
    g_ui.pool[t1].scale = 0.45f;
    g_ui.pool[t2].scale = 0.45f;
    u16 c0 = g_ui.checkbox(330.0f, 56.0f, "Hints", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, true, OnLabToggle, root, st);
    u16 c1 = g_ui.checkbox(330.0f, 106.0f, "Autopilot", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, false, OnLabToggle, root, st);
    g_ui.pool[c0].scale = 0.45f;
    g_ui.pool[c1].scale = 0.45f;
    g_ui.label(20.0f, 210.0f, "watch thumb_pos slide 0<->1 on HUD state", palette::GRAY_LIGHT, 0.36f, root);
}

void BuildSliders() noexcept {
    u8 st = GalleryPillStyle(); // pill: a bar's radius normalizes against its short axis
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Sliders (D4) - drag the thumbs", palette::WHITE, 0.50f, root);
    g_ui.slider(20.0f, 56.0f, 400.0f, 28.0f, 0.0f, OnLabSlider, root, st);
    g_ui.slider(20.0f, 100.0f, 400.0f, 28.0f, 0.5f, OnLabSlider, root, st);
    g_ui.slider(20.0f, 144.0f, 400.0f, 28.0f, 1.0f, OnLabSlider, root, st);
    g_ui.slider(20.0f, 188.0f, 200.0f, 28.0f, 0.25f, OnLabSlider, root, st);
    // Keyboard, which this page never stated. The Slider was the ONE focusable
    // widget with no key handling at all: Tab walked straight through it as if it
    // were a button, and nothing could change its value without a pointer. Arrows
    // now step it by K_SLIDER_KEY_STEP (1%) and Home/End jump to 0 / 100%, both
    // firing on_change exactly like a drag - so "SFX" on the HUD moves for free.
    // In the empty space to the right, not stacked under the sliders: the first
    // version put them at y=252 and landed on top of the thumb_tex caption.
    g_ui.label(460.0f, 56.0f, "keyboard:", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(460.0f, 84.0f, "Tab reaches a slider", 0xFFAAAAFF, 0.34f, root);
    g_ui.label(460.0f, 112.0f, "arrows step it by 1%", 0xFFAAAAFF, 0.34f, root);
    g_ui.label(460.0f, 140.0f, "Home / End: 0 / 100%", 0xFFAAAAFF, 0.34f, root);
    g_ui.label(460.0f, 176.0f, "at a bound a key is a no-op,", palette::GRAY, 0.30f, root);
    g_ui.label(460.0f, 202.0f, "not a spurious change event", palette::GRAY, 0.30f, root);
    g_ui.label(20.0f, 226.0f, "last values scroll on the HUD", palette::GRAY_LIGHT, 0.36f, root);
    // Keyboard, which this page never stated. The Slider was the ONE focusable
    // widget with no key handling at all: Tab walked through it as if it were a
    // button and nothing could change its value without a pointer. Now arrows
    // step it by K_SLIDER_KEY_STEP (1%) and Home/End jump to 0 / 100%, both firing
    // on_change exactly like a drag - so "SFX" on the HUD moves for free.

    // Style-level thumb texture (WidgetStyle::thumb_tex): the thumb is one
    // primitive, so a textured thumb gets its own small flush instead of
    // splitting the whole Pass-2 batch by texture.
    u8 texth_st = GalleryStyle();
    g_ui.styles[texth_st].thumb_tex = g_check_tex;
    g_ui.label(20.0f, 262.0f, "thumb_tex: textured slider/toggle thumbs", palette::GRAY_LIGHT, 0.36f, root);
    g_ui.slider(20.0f, 292.0f, 400.0f, 28.0f, 0.6f, OnLabSlider, root, texth_st);
    g_ui.toggle(20.0f, 336.0f, 120.0f, 36.0f, "Textured", palette::BLUE_DARK, palette::BUTTON_BG, palette::WHITE, true, OnLabButton, root, texth_st);
}

void BuildTextFields() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "TextFields (D5) - click then type", palette::WHITE, 0.50f, root);
    // 0.52, not the 1.0 default: a field is a FIXED-height box and the value is
    // drawn at the widget's own scale, so 1.0 in a 40pt box is a text box with
    // no padding left - and the field clips itself, so the Up/Down font tuner
    // pushed it under the borders. The scale is a trailing arg now; it used to
    // be unreachable, which is why every field on this page was oversized.
    const f32 vscale = 0.52f;
    g_ui.textfield(20.0f, 52.0f, 400.0f, 40.0f, "Player One", palette::GRAY_1D1D1D, palette::WHITE, root, st, vscale);
    g_ui.textfield(20.0f, 104.0f, 400.0f, 40.0f, "", palette::GRAY_1D1D1D, palette::WHITE, root, st, vscale);
    // Clip canary: a value far longer than the field. The field clips its own
    // text (and clamps the caret) instead of letting the string spill over the
    // panel beside it. It is NOT ellipsised - the caret tracks the real
    // advance, so "..." and a caret would disagree about where the text ends.
    g_ui.textfield(20.0f, 156.0f, 260.0f, 40.0f, "a value much wider than this field", palette::GRAY_1D1D1D, palette::WHITE, root, st, vscale);
    // The note sits BELOW the field, not beside it: at x = 300 it started right
    // where the clipped text ends, so it read as more of the value.
    g_ui.label(20.0f, 204.0f, "clipped at the caret, not ellipsised", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(20.0f, 250.0f, "Tab commits and moves focus (Enter commits", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(20.0f, 272.0f, "in place, Escape reverts), cursor blinks", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(20.0f, 294.0f, "via cursor_timer - Up/Down tunes the value", palette::GRAY_LIGHT, 0.30f, root);
}

void BuildStylesShapes() noexcept {
    u8 rounded = GalleryStyle();
    ui::WidgetStyle circle{};
    circle.bg_tex       = TextureHandle::invalid();
    circle.border_color = palette::FOCUS_BLUE;
    circle.border_width = g_border_w;
    circle.corner_r     = 0.5f;
    circle.shape        = ui::shape_type::CIRCLE;
    circle._pad[0] = circle._pad[1] = circle._pad[2] = 0;
    u8 circle_st = g_ui.register_style(circle);
    ui::WidgetStyle sharp{};
    sharp.bg_tex       = TextureHandle::invalid();
    sharp.border_color = 0xFFFFCC00;
    sharp.border_width = g_border_w;
    sharp.corner_r     = 0.0f;
    sharp.shape        = ui::shape_type::RECT;
    sharp._pad[0] = sharp._pad[1] = sharp._pad[2] = 0;
    u8 sharp_st = g_ui.register_style(sharp);
    // Per-side Rect borders (L red / T green / R blue / B yellow) + Ellipse ring.
    ui::WidgetStyle sides{};
    sides.bg_tex    = TextureHandle::invalid();
    sides.corner_r  = 0.0f;
    sides.shape     = ui::shape_type::RECT;
    sides.border_w[ui::BORDER_L] = 0.10f;
    sides.border_c[ui::BORDER_L] = 0xFFFF4444;
    sides.border_w[ui::BORDER_T] = 0.06f;
    sides.border_c[ui::BORDER_T] = 0xFF44FF44;
    sides.border_w[ui::BORDER_R] = 0.10f;
    sides.border_c[ui::BORDER_R] = palette::ACCENT_BLUE;
    sides.border_w[ui::BORDER_B] = 0.06f;
    sides.border_c[ui::BORDER_B] = 0xFFFFFF44;
    u8 sides_st = g_ui.register_style(sides);
    ui::WidgetStyle ell{};
    ell.bg_tex     = TextureHandle::invalid();
    ell.shape      = ui::shape_type::ELLIPSE;
    ell.ring_color = 0xFFFF8844;
    ell.ring_width = 0.06f;
    u8 ell_st  = g_ui.register_style(ell);
    // Style-level background texture: the fill samples a real texture while
    // the style's rounded corner + border still draw (rounded SDF flush).
    u8 tex_st = GalleryTexStyle(g_check_tex);
    // Style-level hard drop shadow: one extra quad in the same batch (no new
    // pipeline, no new draw). shadow_grow is what makes a hard shadow read as
    // depth rather than a second border.
    u8 sh_st = GalleryStyle();
    g_ui.styles[sh_st].shadow_color = 0xA0000000;
    g_ui.styles[sh_st].shadow_dx     = 4.0f;
    g_ui.styles[sh_st].shadow_dy     = 6.0f;
    g_ui.styles[sh_st].shadow_grow   = 4.0f;
    // Gradient canaries. Each is its own style -> its own texture, and Pass 1
    // groups by style texture, so widgets that SHARE a style share a draw.
    u8 g_v  = GalleryGradientStyle(0xFF2A6BE0, 0xFF7A4BE0, true);  // top->bottom
    u8 g_h  = GalleryGradientStyle(0xFF1E7A4B, 0xFF2AB08A, false); // left->right
    u8 g_f  = GalleryGradientStyle(0xFFB04A2A, 0xFFB08A2A, true);  // warm
    u8 g_nb = g_ui.register_style(
        ui::gradient_style(g_r.make_gradient_texture(8, 64, 0xFF33334A, 0xFF6A6AA0, true), g_corner));

    u8 bgst0 = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst0);

    // ── Layout: five caption + row bands, laid out top to bottom ────────
    // Every band is (caption, then widgets) and the numbers below are the ONLY
    // thing that positions anything: nothing was measured, so an overlap is a
    // typo you can see in one place instead of three. Bands: 30/116/202/284/358.
    auto caption = [&](const char *txt, f32 y) noexcept {
        g_ui.label(20.0f, y, txt, palette::GRAY_LIGHT, 0.32f, root);
    };

    g_ui.label(20.0f, 6.0f, "Styles + Shapes (D6) - +/- corner, B border", palette::WHITE, 0.50f, root);

    caption("shapes: corner_r + shape (Rect keeps sharp corners)", 28.0f);
    g_ui.button(20.0f, 48.0f, 170.0f, 44.0f, "Rounded", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, rounded);
    g_ui.button(200.0f, 48.0f, 170.0f, 44.0f, "Rect", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, sharp_st);
    g_ui.button(382.0f, 48.0f, 44.0f, 44.0f, "", palette::BLUE_DARK, palette::WHITE, OnLabButton, root, 0.40f, circle_st);
    g_ui.button(436.0f, 48.0f, 44.0f, 44.0f, "", palette::TOGGLE_GREEN, palette::WHITE, OnLabButton, root, 0.40f, circle_st);
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "corner=%.2f border=%.3f (live: +/- and B)", g_corner, g_border_w);
        g_ui.label(496.0f, 62.0f, buf, palette::GRAY_MID, 0.30f, root);
    }

    caption("borders: per-side Rect (L/T/R/B) + Ellipse ring", 104.0f);
    g_ui.button(20.0f, 124.0f, 200.0f, 44.0f, "PerSide", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, sides_st);
    g_ui.button(232.0f, 124.0f, 220.0f, 44.0f, "Ellipse", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, ell_st);
    g_ui.label(470.0f, 138.0f, "label-fit canaries live on D2", palette::GRAY, 0.30f, root);

    caption("fills: bg_tex (texture + corner + border) then gradient fills", 180.0f);
    g_ui.button(20.0f, 200.0f, 150.0f, 44.0f, "bg_tex", palette::WHITE, 0xFF202020, OnLabButton, root, 0.40f, tex_st);
    g_ui.button(180.0f, 200.0f, 150.0f, 44.0f, "bg_tex 2", palette::WHITE, 0xFF202020, OnLabButton, root, 0.40f, tex_st);
    g_ui.button(350.0f, 200.0f, 120.0f, 44.0f, "vertical", palette::WHITE, palette::WHITE, OnLabButton, root, 0.40f, g_v);
    g_ui.button(480.0f, 200.0f, 120.0f, 44.0f, "horizontal", palette::WHITE, palette::WHITE, OnLabButton, root, 0.40f, g_h);
    g_ui.button(610.0f, 200.0f, 120.0f, 44.0f, "warm", palette::WHITE, palette::WHITE, OnLabButton, root, 0.40f, g_f);

    g_ui.button(20.0f, 256.0f, 150.0f, 44.0f, "no border", palette::WHITE, palette::WHITE, OnLabButton, root, 0.40f, g_nb);
    g_ui.label(190.0f, 288.0f, "gradient = style texture: corner + border survive | material textures on M", palette::GRAY, 0.30f, root);
    // set_text_align + truncation canaries. They are a TEXT feature, so they
    // live here instead of crowding D0's listbox column: a pinned box is the
    // text's LIMIT, and without truncation a long centred / right label spills
    // over both edges with nothing to stop it.
    {
        u16 c1 = g_ui.label(190.0f, 256.0f, "a heading far too long for the box it was given", palette::WHITE, 0.30f, root);
        g_ui.set_text_align(c1, ui::text_align::CENTER, 170.0f);
        u16 c2 = g_ui.label(380.0f, 256.0f, "right-aligned and also too long", palette::GRAY_LIGHT, 0.30f, root);
        g_ui.set_text_align(c2, ui::text_align::RIGHT, 140.0f);
    }

    // A hard shadow only reads where the backdrop is LIGHTER than the shadow
    // itself, so the demo puts both buttons on a light strip - the real use
    // case (a card on a light page). On the dark panel a black shadow is
    // invisible no matter how opaque it is.
    caption("style shadow: hard drop, 1 extra quad - needs a LIGHT backdrop", 318.0f);
    g_ui.panel(12.0f, 340.0f, 400.0f, 66.0f, 0xFFF0F2F6, root, bgst0);
    g_ui.button(26.0f, 352.0f, 170.0f, 44.0f, "Shadowed", 0xFFE6E9EF, 0xFF202028, OnLabButton, root, 0.40f, sh_st);
    g_ui.button(206.0f, 352.0f, 170.0f, 44.0f, "No shadow", 0xFFE6E9EF, 0xFF202028, OnLabButton, root, 0.40f, rounded);
}

void BuildLayout() noexcept {
    // Demos the extended layout system: main/cross alignment (0=Start,
    // 1=Center, 2=End), percent widths, and margins. All containers are
    // root-level; nested containers under plain panels work too now.
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 bg = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Align + Percent + Margin (D7)", palette::WHITE, 0.50f, bg);
    g_ui.label(20.0f, 44.0f, "rows: Start / Center / End + 30/40/20% + margins", palette::GRAY_LIGHT, 0.36f, bg);

    f32 row_w = g_view_w - 80.0f - 40.0f; // inside bg (20px inset each side)
    f32 row_x = 40.0f + 20.0f;
    f32 row_y = PAGE_Y + 84.0f;
    auto demo_row = [&](const char *b1, const char *b2, const char *b3, u8 main_a, u8 cross_a) noexcept {
        u16 row = g_ui.panel(row_x, row_y, row_w, 56.0f, 0xFF222233, UINT16_MAX, st);
        g_ui.set_layout(row, 1, 8, static_cast<u8>(g_spacing));
        g_ui.set_layout_align(row, main_a, cross_a);
        g_ui.button(0.0f, 0.0f, 110.0f, 32.0f, b1, palette::BUTTON_BG, palette::WHITE, OnLabButton, row, 0.36f, st);
        g_ui.button(0.0f, 0.0f, 110.0f, 32.0f, b2, palette::BUTTON_BG, palette::WHITE, OnLabButton, row, 0.36f, st);
        g_ui.button(0.0f, 0.0f, 110.0f, 32.0f, b3, palette::BUTTON_BG, palette::WHITE, OnLabButton, row, 0.36f, st);
        row_y += 68.0f;
    };
    demo_row("A1", "A2", "A3", 0, 0); // Start / Top (legacy look)
    demo_row("B1", "B2", "B3", 1, 1); // Center / Center
    demo_row("C1", "C2", "C3", 2, 2); // End / Bottom

    // Percent + margin row: 30/40/20% of inner width, 6px side margins.
    u16 prow = g_ui.panel(row_x, row_y, row_w, 56.0f, 0xFF222233, UINT16_MAX, st);
    g_ui.set_layout(prow, 1, 8, static_cast<u8>(g_spacing));
    u16 p1 = g_ui.button(0.0f, 0.0f, 110.0f, 40.0f, "30%", palette::BLUE_DARK, palette::WHITE, OnLabButton, prow, 0.36f, st);
    u16 p2 = g_ui.button(0.0f, 0.0f, 110.0f, 40.0f, "40%", palette::BLUE_DARK, palette::WHITE, OnLabButton, prow, 0.36f, st);
    u16 p3 = g_ui.button(0.0f, 0.0f, 110.0f, 40.0f, "20%", palette::BLUE_DARK, palette::WHITE, OnLabButton, prow, 0.36f, st);
    g_ui.set_size_pct(p1, 30, 0);
    g_ui.set_size_pct(p2, 40, 0);
    g_ui.set_size_pct(p3, 20, 0);
    g_ui.set_margin(p1, 0, 6, 0, 6);
    g_ui.set_margin(p2, 0, 6, 0, 6);
    g_ui.set_margin(p3, 0, 6, 0, 6);
}

void BuildFocusNav() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Focus nav (D8) - Tab / Shift-Tab", palette::WHITE, 0.50f, root);
    // Row 1: toggle label draws right of its track — keep the space right
    // of it empty so it never overlaps the textfield (was: label ran into
    // "Third" at x=420).
    g_ui.button(20.0f, 52.0f, 160.0f, 44.0f, "First", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, st);
    g_ui.toggle(200.0f, 52.0f, 200.0f, 44.0f, "Second", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, false, OnLabToggle, root, st);
    g_ui.textfield(20.0f, 112.0f, 300.0f, 44.0f, "Third", palette::GRAY_1D1D1D, palette::WHITE, root, st);
    g_ui.button(340.0f, 112.0f, 160.0f, 44.0f, "Fourth", palette::BUTTON_BG, palette::WHITE, OnLabButton, root, 0.40f, st);
    g_ui.checkbox(20.0f, 172.0f, "Fifth", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, false, OnLabToggle, root, st);
    g_ui.slider(300.0f, 168.0f, 220.0f, 36.0f, 0.5f, OnLabSlider, root, st);
    g_ui.label(20.0f, 226.0f, "green ring = focused widget (watch focus_id)", palette::GRAY_LIGHT, 0.36f, root);
    // Tab order IS pool order, so this row reads 1-6 straight across:
    // button -> toggle -> textfield -> button -> checkbox -> slider, and
    // Shift-Tab walks it back. Buttons used to be skipped here (no
    // WF_FOCUSABLE), which made this page claim a focus order the toolkit
    // did not have.
    // NOTE: two labels, not one - Widget::text is 48 bytes and the factories
    // strncpy, so a longer string is silently cut mid-word.
    g_ui.label(20.0f, 262.0f, "Tab visits all six, buttons included", palette::GRAY, 0.30f, root);
    g_ui.label(20.0f, 286.0f, "set_focusable(id,false) opts a widget out", palette::GRAY, 0.30f, root);

    // ── The three widgets that used to be ONE-WAY DOORS ──
    // Row 1 above could not show the Tab bug, because none of those six widgets
    // claims the arrow keys. These three all do: a focused one eats Up/Down for
    // its own value, selection or active cell, and all three CLAMP rather than
    // wrap. While Tab arrived as MenuDown they therefore also ate Tab - so Tab
    // walked you in and never let you back out, and the focus ring was stuck on
    // whichever of them you reached first. Nothing on this page could reveal it.
    //
    // They are in the ring now so a human can verify the fix with Tab, which is
    // the only way to check a keyboard contract at all. Expected: Tab reaches
    // each one and Tab leaves it again, with the arrows still owned by the widget.
    static const char *const kFocusItems[] = {"alpha", "beta", "gamma"};
    static const char *const kFocusTabs[]  = {"One", "Two", "Three"};
    g_ui.combobox(20.0f, 330.0f, 300.0f, 40.0f, kFocusItems, 3, false, nullptr, root, st);
    g_ui.label(330.0f, 338.0f, "combo: arrows move, Tab leaves", palette::GRAY, 0.28f, root);
    g_ui.listbox(20.0f, 386.0f, 300.0f, 130.0f, kFocusItems, 3, nullptr, root, st);
    g_ui.label(330.0f, 394.0f, "list: arrows select, Tab leaves", palette::GRAY, 0.28f, root);
    g_ui.tabbar(20.0f, 532.0f, 300.0f, 40.0f, kFocusTabs, 3, root, 0, nullptr);
    g_ui.label(330.0f, 540.0f, "tabbar: Left/Right + arrows, Tab leaves", palette::GRAY, 0.28f, root);
    g_ui.label(330.0f, 566.0f, "Tab never opens or pages a widget", palette::GRAY_LIGHT, 0.30f, root);
    // Home / End are on every one of the four value-owning widgets above plus the
    // Slider on D4: first/last row, first/last cell, and 0 / 100%. The Slider is
    // the one that had NO key handling until this round, and it is on this page
    // too because "Tab walks straight through a focusable widget" is the symptom
    // nobody would think to look for.
    const u16 kbd_sl = g_ui.slider(20.0f, 600.0f, 300.0f, 28.0f, 0.5f, OnLabSlider, root, st);
    g_ui.label(330.0f, 606.0f, "slider: arrows 1%, Home/End 0/100%", palette::GRAY, 0.28f, root);
    g_ui.label(330.0f, 630.0f, "Home / End: first / last on all four", palette::GRAY_LIGHT, 0.30f, root);
    (void)kbd_sl;
}

// ─── ScrollView demo (V) ────────────────────────────────────────
// Four viewports in a row, ONE RULE each:
//
//   1 tall CONTENT coordinates, so rows start below the window and only appear
//     once it is scrolled. This is the thing that could not be written before
//     ScrollView: content positioned independently of where it is displayed.
//   2 a second view with its own offset. It used to be captioned "nested: offsets
//     add" and was NOT nested - both are children of the page panel - so the
//     caption described a behaviour the page did not have.
//   3 a view whose content FITS: scroll_max is 0, so no bar is drawn and
//     set_scroll cannot move it.
//   4 clip canaries: a checkbox and a radiobox whose BOXES straddle the
//     viewport's bottom edge. Pass 1.75 draws its own primitive, so it has to
//     clip itself the way every other pass does. They used to sit here with no
//     label at all, which read as two stray squares floating in the view.
//
// Buttons inside are real widgets: tapping one must work at its scrolled
// position, which is the property the offset-in-the-abs-cache design exists for.
void BuildScrollviews() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "ScrollViews (V)", palette::WHITE, 0.50f, root);
    g_ui.label(20.0f, 46.0f, "content coords are independent of the window", palette::GRAY_LIGHT, 0.30f, root);

    static const char *const kRows[12] = {"row 1", "row 2", "row 3", "row 4", "row 5", "row 6",
                                         "row 7", "row 8", "row 9", "row 10", "row 11", "row 12"};
    static u16          g_row_ids[12] = {};

    const f32 view_y = 88.0f;
    const f32 view_h = 250.0f;

    // ── 1: the view that scrolls ──
    g_ui.label(20.0f, 66.0f, "1  content taller than the window", palette::GRAY_LIGHT, 0.28f, root);
    const f32 v1_w   = 300.0f;
    u16    outer  = g_ui.scrollview(20.0f, view_y, v1_w, view_h, root, st);
    // Content starts BELOW the clip band. The band's top inset is
    // border_width * view_h (10pt at the default 0.04), and a row that starts
    // above it has its own top border shaved off - which is exactly what the
    // first row used to look like at content y = 8.
    const f32 top     = 16.0f;
    const f32 pitch   = 40.0f;
    const f32 row_h   = 34.0f;
    const f32 rows_end = top + 12.0f * pitch;
    g_ui.set_scroll_content_h(outer, rows_end + 34.0f);
    for (int i = 0; i < 12; ++i) {
        g_row_ids[i] = g_ui.button(14.0f, top + static_cast<f32>(i) * pitch, v1_w - 46.0f, row_h, kRows[i], palette::BUTTON_BG, palette::WHITE,
                                  OnLabButton, outer, 0.38f, st);
    }
    // BELOW the last row. A label's y is a CONTENT coordinate like any other
    // child - nothing about being a label puts it outside the content - so this
    // used to sit at view_h + 40, which is row 7.
    g_ui.label(14.0f, rows_end + 8.0f, "tap a row: hit-testing scrolls with it", palette::GRAY_LIGHT, 0.28f, outer);

    // ── 2: a second view, its own offset ──
    g_ui.label(336.0f, 66.0f, "2  a second view, own offset", palette::GRAY_LIGHT, 0.28f, root);
    const f32 v2_w = 180.0f;
    u16    inner = g_ui.scrollview(336.0f, view_y, v2_w, view_h, root, st);
    g_ui.set_scroll_content_h(inner, 420.0f);
    // Relative to `inner`, so the x is 12 - the caption used to pass an
    // absolute-looking nx + 8 and drew itself 388pt to the right, clipped away.
    g_ui.label(12.0f, 16.0f, "scrolls on its own", palette::GRAY_LIGHT, 0.26f, inner);
    for (int i = 0; i < 8; ++i) {
        g_ui.button(12.0f, 44.0f + static_cast<f32>(i) * 48.0f, v2_w - 40.0f, 42.0f, kRows[i], palette::GRAY_DARK, palette::WHITE, OnLabButton, inner,
                    0.34f, st);
    }

    // ── 3: content that fits ──
    g_ui.label(532.0f, 66.0f, "3  content that fits", palette::GRAY_LIGHT, 0.28f, root);
    u16 fits = g_ui.scrollview(532.0f, view_y, 150.0f, 150.0f, root, st);
    g_ui.set_scroll_content_h(fits, 120.0f);
    g_ui.set_scroll(fits, 999.0f); // clamped to 0: nothing to scroll
    // INSIDE the view, in its content coords. These were parented to the page
    // panel at nx + 228, so they were drawn straight over the view they describe.
    g_ui.label(16.0f, 20.0f, "no bar,", palette::GRAY_LIGHT, 0.28f, fits);
    g_ui.label(16.0f, 46.0f, "no range", palette::GRAY_LIGHT, 0.28f, fits);

    // ── 4: clip canaries ──
    g_ui.label(698.0f, 66.0f, "4  children are clipped to it", palette::GRAY_LIGHT, 0.28f, root);
    u16 canary = g_ui.scrollview(698.0f, view_y, 200.0f, 150.0f, root, st);
    g_ui.set_scroll_content_h(canary, 400.0f);
    g_ui.label(12.0f, 18.0f, "the two boxes below sit ON", palette::GRAY_LIGHT, 0.26f, canary);
    g_ui.label(12.0f, 38.0f, "the bottom edge and get cut:", palette::GRAY_LIGHT, 0.26f, canary);
    // Box top 120 with a 150-tall window: the band ends at 150 - 0.04*150 = 144,
    // so each 28pt box loses its bottom 4pt while its label (centred in the
    // frame, around y 134) stays inside. Straddling and readable are not
    // exclusive - the label just has to sit higher than the box's centre.
    g_ui.checkbox(12.0f, 120.0f, "cut here", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, true, nullptr, canary, st, 0.26f);
    g_ui.radiobox(104.0f, 120.0f, "and here", palette::ACCENT_BLUE, palette::GRAY_DARK, palette::WHITE, 9, true, nullptr, canary, st, 0.26f);

    // ── what a drag actually does, spelled out ──
    // The page used to say only "wheel, drag or tap the bar", which is why a
    // first-time reader could not tell a press from a scroll: the row squeezes,
    // then the content moves, and nothing on screen says which is which.
    f32 y = view_y + view_h + 26.0f;
    g_ui.label(20.0f, y, "what a drag on a row does:", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(20.0f, y + 24.0f, "press and hold: the row squeezes to 85%", palette::GRAY, 0.30f, root);
    g_ui.label(20.0f, y + 46.0f, "under 6px (the slop): still a tap", palette::GRAY, 0.30f, root);
    g_ui.label(20.0f, y + 68.0f, "past 6px: a scroll, squeeze stops", palette::GRAY, 0.30f, root);
    g_ui.label(20.0f, y + 90.0f, "release after scrolling: no click fires", palette::GRAY, 0.30f, root);
    g_ui.label(20.0f, y + 112.0f, "the content follows the finger 1:1", palette::GRAY, 0.30f, root);
    g_ui.label(20.0f, y + 134.0f, "no inertia: it stays where you let go", palette::GRAY, 0.30f, root);
    // Keyboard, which this page never stated. A ScrollView is a VIEWPORT, not a
    // ListBox: every row is its own control and all of them are in the tab order,
    // because a settings panel where only one row can hold focus is unusable. The
    // consequence is that Tab walks into content below the fold - so focusing
    // scrolls the row into view, minimally, aligning it to whichever edge it came
    // from. Tab into view 1 and watch row 10: the ring is never clipped away.
    g_ui.label(20.0f, y + 160.0f, "Tab into a row: the view follows focus", 0xFFAAAAFF, 0.30f, root);
    g_ui.label(20.0f, y + 184.0f, "every row is focusable - this is not a ListBox", palette::GRAY, 0.30f, root);

    g_ui.label(400.0f, y, "drag the bar, or tap it to page", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, y + 24.0f, "scroll_max = content_h - view_h,", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, y + 46.0f, "so view 3 cannot scroll at all", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, y + 76.0f, "K_SCROLL_BAR_W is a constant (10pt),", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, y + 98.0f, "not a fraction of the view", palette::GRAY_LIGHT, 0.30f, root);
}

// ─── TreeView demo (T) ───────────────────────────────────────────
// A TREE IS A LISTBOX plus a node model. Everything mechanical - scrolling,
// selection highlight, keyboard Up/Dn, wheel - is the listbox's; the only new
// thing is the flatten, which is pure and lives in mm_ui.hpp.
//
// The row callback receives a ROW index, not a node index, and has to map it
// with tree_row_node(). Indexing the node array directly is the bug this demo
// exists to make obvious: rows and nodes are different lengths.
static ui::TreeNode g_tree_nodes[24];
static const char *g_tree_labels[24];
static u16    g_tree_box = UINT16_MAX;

// 0 root
//   1 src/ui         2 engine/render   3 engine/ui
//     4 mm_ui.cpp
//   5 engine/tests
//   6 engine/ui
//     7 mm_ui_wlist.hpp
static void OnLabTreeRow(u16, Renderer &r, SpriteBatch &b, f32 row_x, f32 row_y, f32 row_w, f32 row_h, int item, void *) noexcept {
    static u32 scratch[ui::Manager::K_TREE_ROW_SCRATCH];
    if (g_tree_box == UINT16_MAX || item < 0) {
        return;
    }
    const u32 node  = g_ui.tree_row_node(g_tree_box, static_cast<u32>(item), scratch);
    if (node == UINT32_MAX) {
        return;
    }
    const u32 depth  = g_ui.tree_row_depth(g_tree_box, static_cast<u32>(item), scratch);
    const bool     parent = g_ui.tree_has_children(g_tree_box, node);
    const bool     open   = g_ui.tree_is_expanded(g_tree_box, node);

    // Expander: a triangle in the row's own indent column. Filled when open,
    // outlined when closed, and simply absent on a leaf - the affordance is the
    // difference, so a leaf must not draw one.
    if (parent) {
        const f32 cx = row_x + ui::Manager::tree_expander_x(depth) + ui::Manager::K_TREE_EXPANDER_W * 0.5f;
        const f32 cy = row_y + row_h * 0.5f;
        const f32 s  = 4.0f;
        for (int i = 0; i < 3; ++i) {
            const f32 t = static_cast<f32>(i) / 2.0f;
            if (open) {
                b.add(cx - s + (2.0f * s) * t, cy + s, s, 2.0f, 0.0f, 0xFFAABBCC, LAYER_UI, 0, 0.0f, 0.0f, 0);
            } else {
                b.add(cx + s - static_cast<f32>(i), cy - s, 2.0f, 2.0f + static_cast<f32>(i) * 2.0f, 0.0f, 0xFFAABBCC, LAYER_UI, 0, 0.0f, 0.0f, 0);
            }
        }
    }

    // Drain the expander here, while it is the only thing queued - the same
    // drain-first pattern the rich-row demo uses for its zebra stripe.
    r.flush_sprites(b);
    b.reset();

    // Label indented past the expander strip, through the row helper so the
    // vertical centring matches every other row in the toolkit.
    // Field::x is RELATIVE to row_x (rows::draw computes `row_x + f.x`), so
    // passing an absolute x here indents by twice.
    const f32 tx = ui::Manager::tree_expander_x(depth) + ui::Manager::K_TREE_EXPANDER_W + 4.0f;
    const ui::rows::Field fields[1] = {
        {g_tree_labels[node], tx, row_w - tx - 8.0f, open ? palette::WHITE : palette::GRAY_LIGHT, false},
    };
    ui::rows::draw(r, fields, 1, row_x, row_y, row_h, 0.34f);
}

void BuildTreeviews() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "TreeView (T) - tap the triangle, or tap a row", palette::WHITE, 0.50f, root);

    // Array order IS row order (siblings are a contiguous index range), so the
    // model is written in the order the rows should appear.
    const char *labels[] = {"mmx", "src/ui", "engine/render", "engine/ui", "mm_ui.cpp", "engine/tests", "engine/ui", "mm_ui_wlist.hpp"};
    const i32 parent[]  = {-1, 0, 0, 0, 1, 0, 5, 6};
    const u32 first[]  = {1, 0, 0, 0, 4, 6, 7, 0};
    const u32 kids[]   = {3, 1, 0, 1, 0, 1, 1, 0};
    const u8  flags[]  = {ui::TREE_HAS_CHILDREN, ui::TREE_HAS_CHILDREN, 0, ui::TREE_HAS_CHILDREN, 0,
                               ui::TREE_HAS_CHILDREN, ui::TREE_HAS_CHILDREN, 0};
    constexpr u32 n = sizeof(labels) / sizeof(labels[0]);
    for (u32 i = 0; i < n; ++i) {
        g_tree_labels[i] = labels[i];
        g_tree_nodes[i]  = ui::TreeNode{parent[i], first[i], kids[i], flags[i], {0, 0, 0}};
    }

    static const char *const kItems[8] = {"", "", "", "", "", "", "", ""};
    g_tree_box = g_ui.listbox(20.0f, 56.0f, 300.0f, 250.0f, kItems, 8, OnLabButton, root, st);
    g_ui.set_tree(g_tree_box, g_tree_nodes, n);
    g_ui.set_row_renderer(g_tree_box, OnLabTreeRow, nullptr);
    g_ui.set_tree(g_tree_box, g_tree_nodes, n); // re-attach: metrics are ready
    g_ui.tree_set_expanded(g_tree_box, 0, true);

    g_ui.label(340.0f, 60.0f, "a tree IS a listbox:", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(340.0f, 84.0f, "- scroll, selection, Up/Dn", palette::GRAY, 0.30f, root);
    g_ui.label(340.0f, 106.0f, "and the wheel are the listbox's", palette::GRAY, 0.30f, root);
    g_ui.label(340.0f, 140.0f, "only the flatten is new:", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(340.0f, 164.0f, "- which nodes are visible given", palette::GRAY, 0.30f, root);
    g_ui.label(340.0f, 186.0f, "which ancestors are open", palette::GRAY, 0.30f, root);
    g_ui.label(340.0f, 220.0f, "rows != nodes:", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(340.0f, 244.0f, "the callback maps row -> node", palette::GRAY, 0.30f, root);
    g_ui.label(340.0f, 266.0f, "with tree_row_node()", palette::GRAY, 0.30f, root);
    g_ui.label(20.0f, 318.0f, "array order IS row order - siblings are a contiguous index range", palette::GRAY_LIGHT, 0.30f, root);
}

// ─── Accordion demo (A) ──────────────────────────────────────────
// A section is a HEADER BUTTON plus a content panel. Two of the three sections
// start open so the page shows the settled state on frame one, and each holds a
// ScrollView to make the point that the clip reaches everything nested inside -
// the collapse clips the scroll view, its rows and the bar with no per-pass code.
void BuildAccordions() noexcept {
    u8 st = GalleryStyle();
    u8 bgst = GalleryBgStyle();
    u16 root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Accordions (A) - tap a header", palette::WHITE, 0.50f, root);

    struct Section {
        const char *title;
        f32       content_h;
        bool        open;
        bool        with_scroll;
        bool        with_toggles; // checkbox/radio inside, straddling the bottom
    };
    static const Section kSections[] = {
        {"first section (open, with a ScrollView)", 150.0f, true, true, false},
        // Closed at boot AND holding toggles: collapsing clears the CONTENT's own
        // WF_VISIBLE but not its descendants', so a render pass that reads the
        // own flag leaves a checkbox floating where the section used to be.
        // This section is the only thing that makes that visible - before it,
        // every section held buttons and Pass 1 already tested is_visible().
        // It sits SECOND on purpose: PAGE_Y=210 leaves 410pt of page, so the
        // last section has always been off-screen and a canary there would
        // never be looked at.
        {"second section (CLOSED, with toggles)", 90.0f, false, false, true},
        // Open, holding the SAME straddling toggles: the other half of the pair.
        // Closed proves an empty clip draws nothing; open proves the clip is a
        // real boundary and not just "hide everything" - the boxes must stop at
        // the section band instead of hanging below it. Three sections, same as
        // before: PAGE_Y=210 leaves 410pt of page and a fourth never fit.
        {"third section (open, with toggles)", 110.0f, true, false, true},
    };

    constexpr f32 kHeadH = 30.0f;
    f32          y      = 56.0f;
    int            radio_group = 8;
    for (const Section &sec : kSections) {
        u16 head = g_ui.button(20.0f, y, 340.0f, kHeadH, sec.title, palette::BUTTON_BG, palette::WHITE, nullptr, root, 0.36f, st);
        // Relative to the HEADER (attach reparents it), so (0, header_h) puts it
        // directly underneath.
        u16 body = g_ui.panel(0.0f, kHeadH, 340.0f, sec.content_h, palette::GRAY_1D1D1D, head, bgst);
        g_ui.label(10.0f, 8.0f, "content lives here", palette::GRAY_LIGHT, 0.30f, body);
        if (sec.with_scroll) {
            // A ScrollView inside the section: the collapse has to clip the bar
            // and the rows too, which it does because the clip is resolved in
            // get_clip() and every pass goes through it.
            u16 sv = g_ui.scrollview(10.0f, 30.0f, 320.0f, sec.content_h - 40.0f, body, st);
            g_ui.set_scroll_content_h(sv, 400.0f);
            for (int i = 0; i < 8; ++i) {
                g_ui.button(6.0f, 4.0f + static_cast<f32>(i) * 48.0f, 300.0f, 42.0f, "a row inside the section", 0xFF333333, palette::WHITE,
                            OnLabButton, sv, 0.32f, st);
            }
        }
        g_ui.accordion_attach(head, body, sec.content_h, sec.open);
        if (sec.with_toggles) {
            // Straddling the body's bottom edge, so the clip is exercised as well
            // as the effective visibility: closed, none of this may be visible at
            // all; open, it must stop at the section band.
            //
            // Two constraints fought each other here and the toggles lost. A checkbox
            // label is centred in its 28pt frame, so a box near the section's
            // bottom edge puts its LABEL over the edge and the text was cut in
            // half (only the letter-tops showed) - and moving the boxes up far
            // enough for the label left them hanging over the edge, which reads
            // as "the content overflows" rather than as a clip canary. Two
            // complaints, one cause: I was using a 28pt box as the canary.
            //
            // So the toggles now sit fully INSIDE the band and say what they are,
            // and the clip itself is demonstrated by the ScrollView in the first
            // section, whose rows are cut by the section band for the same
            // reason and are legible about it.
            g_ui.label(10.0f, sec.content_h - 66.0f, "these two are inside the band:", palette::GRAY_LIGHT, 0.30f, body);
            g_ui.checkbox(10.0f, sec.content_h - 40.0f, "inside", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, true, nullptr, body, st, 0.30f);
            // A group per section: radiobox_select() clears every visible radio in
            // the SAME group, so one shared group meant building the third
            // section silently un-selected the second's.
            g_ui.radiobox(90.0f, sec.content_h - 40.0f, "and this", palette::ACCENT_BLUE, palette::GRAY_DARK, palette::WHITE,
                          static_cast<u8>(radio_group++), true, nullptr, body, st, 0.30f);
        }
        // A closed section still RESERVES its content height: the engine animates
        // one section's height and nothing reflows the ones below it, so a closed
        // section that reserved nothing would be overlapped by the next header
        // the moment it opened. The gap is therefore unavoidable - what is
        // avoidable is it reading as "the page lost a block", so the gap says
        // what it is. (This label is parented to the page, NOT to the body: the
        // body is clipped to a zero-height band while closed.)
        if (!sec.open) {
            g_ui.label(30.0f, y + kHeadH + 12.0f, "closed: the space is reserved on purpose,", palette::GRAY, 0.30f, root);
            g_ui.label(30.0f, y + kHeadH + 34.0f, "tap the header and it eases open", palette::GRAY, 0.30f, root);
        }
        y += kHeadH + sec.content_h + 14.0f;
    }

    g_ui.label(400.0f, 60.0f, "the engine owns:", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, 84.0f, "- the height easing (target vs", palette::GRAY, 0.30f, root);
    g_ui.label(400.0f, 106.0f, "  drawn, advanced in update)", palette::GRAY, 0.30f, root);
    g_ui.label(400.0f, 140.0f, "- the clip, in get_clip(), so it", palette::GRAY, 0.30f, root);
    g_ui.label(400.0f, 162.0f, "  reaches the content and a", palette::GRAY, 0.30f, root);
    g_ui.label(400.0f, 184.0f, "  ScrollView inside it", palette::GRAY, 0.30f, root);
    g_ui.label(400.0f, 218.0f, "the band is the HEADER's FRAME +", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, 240.0f, "the drawn height, not its border", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, 262.0f, "band - that sits BESIDE the body", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, 296.0f, "the app owns a header button, a", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, 318.0f, "content panel, and nothing else:", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(400.0f, 340.0f, "attach() needs no callback", palette::GRAY_LIGHT, 0.30f, root);
}

// ─── DataGrid demo (G) ───────────────────────────────────────────
// A sortable table on ONE listbox. 28 rows so the scrollbar is live the whole
// time, which is the point: a header that shifts the row area has to shift the
// SCROLL RANGE with it, or the last row can never be scrolled into view.
//
// The Move column is deliberately NOT in model order and mixes "9" with "12" —
// the header tap sorts numerically (9 before 12), which a string compare gets
// backwards. Score is not sortable, so its diamond stays dim and its tap does
// nothing: an inert column must LOOK inert.
static u16 g_grid = UINT16_MAX;

static const ui::GridColumn kGridCols[] = {
    {"Deal", 0.0f, ui::text_align::LEFT, false},
    {"Moves", 110.0f, ui::text_align::RIGHT, true},
    {"Time", 110.0f, ui::text_align::RIGHT, true},
    {"Score", 110.0f, ui::text_align::RIGHT, true},
    {"Class", 120.0f, ui::text_align::LEFT, false},
};
static constexpr u32 kGridColCount = 5;

static ui::GridRow g_grid_rows[28];
static u8     g_grid_order[28];

// Model order, deliberately NOT sorted: the point of the page is that the
// first header tap produces an order the model did not have.
static void InitGridModel() noexcept {
    static const char *kMoves[28] = {"31",  "9",   "12",  "9",   "100", "7",   "24",  "18",  "5",   "44",
                                     "16",  "27",  "8",   "63",  "22",  "11",  "39",  "6",   "51",  "29",
                                     "14",  "33",  "19",  "47",  "10",  "26",  "41",  "3"};
    static const char *kSeed[28] = {"774636582", "117402441", "902117336", "455820117", "631904228", "288117460", "775410923",
                                    "120998377", "663301884", "509224671", "882715063", "331887420", "744920158", "218663905",
                                    "997331240", "460228715", "582091473", "139774620", "826440915", "395018274", "671339082",
                                    "284117635", "713998046", "540272819", "918336205", "407115382", "652904731", "176338954"};
    static const char *kTime[28] = {"0:31", "1:02", "0:18", "0:44", "2:57", "0:12", "0:39", "1:11", "0:08", "3:04",
                                    "0:26", "1:38", "0:15", "2:21", "0:51", "0:21", "1:47", "0:06", "2:33", "0:58",
                                    "0:33", "1:22", "0:41", "2:09", "0:17", "1:04", "2:46", "0:09"};
    static const char *kScore[28] = {"204", "180", "226", "180", "212", "199", "231", "240", "176", "251",
                                     "218", "245", "171", "260", "223", "163", "238", "155", "249", "229",
                                     "207", "242", "190", "255", "168", "221", "253", "158"};
    static const char *kClass[28] = {"Easy", "Easy", "Medium", "Easy", "Medium", "Easy", "Hard", "Hard", "Easy", "Expert",
                                     "Medium", "Hard", "Easy", "Expert", "Medium", "Easy", "Hard", "Easy", "Expert", "Hard",
                                     "Medium", "Expert", "Easy", "Expert", "Easy", "Medium", "Expert", "Easy"};
    for (u32 i = 0; i < 28; ++i) {
        g_grid_rows[i].cells[0] = kSeed[i];
        g_grid_rows[i].cells[1] = kMoves[i];
        g_grid_rows[i].cells[2] = kTime[i];
        g_grid_rows[i].cells[3] = kScore[i];
        g_grid_rows[i].cells[4] = kClass[i];
        g_grid_rows[i].cells[5] = nullptr;
        g_grid_rows[i].cells[6] = nullptr;
        g_grid_rows[i].cells[7] = nullptr;
        g_grid_order[i]         = static_cast<u8>(i);
    }
}

// The engine owns the header and the sort STATE; the app re-sorts and hands the
// order back. One line of body is why the split exists — a date or a numeric
// key is not something the engine can read out of a `const char *`.
static void OnGridSort(u16 id, void *) noexcept {
    const u8 col = g_ui.grid_sorted_col(id);
    if (col == UINT8_MAX) {
        return;
    }
    ui::grid_sort_order(g_grid_rows, 28, col, g_ui.grid_sort_of(id, col), g_grid_order, 28);
    g_ui.set_grid_order(id, g_grid_order);
}

// Cells via the shared row helper: per-column alignment comes from the column
// model, so a right-aligned number lines up under its own header.
static void GridRowRender(u16 id, Renderer &r, SpriteBatch &, f32 x, f32 y, f32 w, f32 h, int visible, void *) noexcept {
    const ui::ListboxMetrics &lm = g_ui.listbox_metrics[id];
    const ui::GridMetrics     &gm = g_ui.grid_metrics[id];
    const u32 mi             = g_ui.grid_row_source(id, static_cast<u32>(visible));
    const ui::GridRow     &row    = g_grid_rows[mi];
    // row_w is the CONTENT width and does NOT inset for the scrollbar (the
    // documented RowCallback rule), so a grid that lays its columns straight
    // into it runs the last column under the thumb. Reserve it here.
    const f32 sb_w  = (g_ui.listbox_count[id] > lm.visible) ? lm.scrollbar_w : 0.0f;
    ui::rows::Field f[8];
    for (u8 c = 0; c < gm.columns; ++c) {
        const f32 cw = (c + 1 == gm.columns) ? (w - sb_w - gm.x[c]) : gm.w[c];
        f[c].text      = row.cells[c];
        f[c].x         = gm.x[c] + 4.0f;
        f[c].w         = cw - 8.0f;
        f[c].color     = (visible == g_ui.get_listbox_selected(id)) ? palette::WHITE : 0xFFDDDDDDu;
        // Alignment comes from the COLUMN MODEL, not the field: rows::Field only
        // knows left or right-hung, and a grid is exactly where that shows.
        f[c].right     = g_ui.grid_cols[id][c].align == ui::text_align::RIGHT;
    }
    ui::rows::draw(r, f, static_cast<u8>(gm.columns), x, y, h, lm.text_scale);
}

void BuildDatagrids() noexcept {
    InitGridModel();
    u8    st    = GalleryStyle();
    u8    bgst  = GalleryBgStyle();
    const f32 gw   = 700.0f;
    const f32 gh   = 300.0f;
    u16 root   = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "DataGrid (G) - tap a column title to sort", palette::WHITE, 0.50f, root);
    // Under 48 chars: Widget::text is an inline buffer and the factory strncpy's,
    // so a longer sentence is silently cut mid-word (it reads as a layout bug).
    g_ui.label(20.0f, 38.0f, "Moves sorts NUMERICALLY: 9 before 12.", palette::GRAY_LIGHT, 0.30f, root);

    g_grid = g_ui.listbox(20.0f, 64.0f, gw, gh, nullptr, 28, OnGridSort, root, st);
    g_ui.grid_attach(g_grid, kGridCols, kGridColCount);
    g_ui.set_row_renderer(g_grid, GridRowRender);

    // Start sorted by Moves descending, so frame one already shows the numeric
    // order and the header indicator — a table whose header is the only proof
    // of the sort state is a table you cannot read at a glance.
    g_ui.grid_toggle_sort(g_grid, 1);
    g_ui.grid_toggle_sort(g_grid, 1);
    OnGridSort(g_grid, nullptr);

    // Notes under the grid, not beside it: at the lab's font_scale (1.0) a row
    // is ~48px, so a 700px-wide grid leaves no usable column on the right.
    const f32 ny = 64.0f + gh + 18.0f;
    g_ui.label(20.0f, ny, "what the engine owns:", palette::WHITE, 0.32f, root);
    g_ui.label(20.0f, ny + 24.0f, "- the header band and its titles", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(20.0f, ny + 46.0f, "- the column geometry (fixed/flexible)", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(20.0f, ny + 68.0f, "- the sort state + the header marker", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(20.0f, ny + 90.0f, "- the row-area inset, so the bar and", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(20.0f, ny + 112.0f, "  the tap target stay off the header", palette::GRAY_LIGHT, 0.30f, root);

    g_ui.label(420.0f, ny, "what the app owns:", palette::WHITE, 0.32f, root);
    g_ui.label(420.0f, ny + 24.0f, "- the model (rows of cells)", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, ny + 46.0f, "- the cells, via set_row_renderer", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, ny + 68.0f, "- the order: re-sort on the sort", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, ny + 90.0f, "  event, hand back a pointer", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, ny + 112.0f, "(a date column is not a char* sort)", palette::GRAY, 0.30f, root);
}

// ─── Images demo (I) ─────────────────────────────────────────────
// image() and set_material() and set_style() had NO demo anywhere in the repo -
// image_tests.cpp claimed "covered by the mm_07 D6 screenshot", and D6 has never
// created an image. set_material() had zero uses outside a unit test, which is
// how widget_material[] went un-reset in alloc() for so long: nothing could
// surface a bug in a path nothing draws.
//
// The two material buttons at the bottom are also the Pass 1.5 canary: they are
// DISABLED, so their state overlay always draws (no hover needed), and they live
// in two different clipped containers that straddle their edges. Pass 1.5 used to
// scissor per widget and flush once, so both overlays took the LAST rect - which
// is why this page had to exist before that fix could be verified by pixels.
static u16 s_styled    = UINT16_MAX;
static u8  s_plain     = 0;
static u8  s_fancy     = 0;
static bool     s_on_fancy  = true;

static void OnLabImageClick(u16 id, void *) noexcept {
    // The id is the widget that fired, so no file-static handle is needed - and
    // the styles are read back off the manager rather than captured, because a
    // rebuild re-registers them and would leave a captured id dangling.
    if (id == UINT16_MAX) {
        return;
    }
    s_on_fancy = !s_on_fancy;
    g_ui.set_style(id, s_on_fancy ? s_fancy : s_plain);
}

void BuildImages() noexcept {
    u8     st   = GalleryStyle();
    u8     bgst = GalleryBgStyle();
    const f32 pw   = g_view_w - 80.0f;
    u16     root = g_ui.panel(40.0f, PAGE_Y, pw, g_view_h - PAGE_Y - 20.0f, palette::PANEL, UINT16_MAX, bgst);
    g_ui.label(20.0f, 12.0f, "Images (I) - image(), set_material(), set_style()", palette::WHITE, 0.50f, root);

    // image() with no texture: the bg_color is the fill, so this is a flat quad.
    // Pass 1 is the only pass it draws in - the widget has no text and no state.
    g_ui.label(20.0f, 54.0f, "image() = a quad whose bg_color is the tint", palette::WHITE, 0.32f, root);
    g_ui.image(20.0f, 76.0f, 90.0f, 60.0f, root);
    g_ui.label(120.0f, 96.0f, "flat tint", palette::GRAY_LIGHT, 0.30f, root);

    // image() + a material: the same quad, now sampling the M page's checker
    // through the plain sprite pipeline. Material owns the pipeline, so this is
    // the only path that exercises set_material in the whole repo.
    const Material checker = g_r.make_material(g_r.sprite_pipeline, g_check_tex, g_r.default_sampler);
    u16       tex_img = g_ui.image(230.0f, 76.0f, 90.0f, 60.0f, root);
    g_ui.set_material(tex_img, checker);
    g_ui.label(230.0f, 144.0f, "set_material(checker)", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(230.0f, 164.0f, "= a different PIPELINE,", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(230.0f, 184.0f, "not just a different tint", palette::GRAY_LIGHT, 0.30f, root);

    // set_style() at runtime: content insets come from the style, so the next
    // frame's geometry has to follow, which is why it dirties measure().
    s_styled = g_ui.button(350.0f, 76.0f, 200.0f, 44.0f, "restyled", palette::BUTTON_BG, palette::WHITE, OnLabImageClick, root, 0.34f, st);
    s_plain  = st;
    s_fancy  = g_ui.register_style(ui::gradient_style(g_r.white_tex, 0.30f, palette::FOCUS_BLUE, 0.10f));
    g_ui.label(350.0f, 132.0f, "set_style() swaps at runtime", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(350.0f, 152.0f, "tap to toggle plain / gradient", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.set_style(s_styled, s_fancy); // start on the gradient so both are seen

    // ── Pass 1.5 canary ─────────────────────────────────────────
    // Two DISABLED material buttons, each in its own clipped panel and each
    // straddling that panel's bottom edge. Disabled because the 0x50000000 dim
    // overlay draws every frame, so the canary needs no pointer to be seen.
    //
    // It also has to be READABLE as a button, because that is what it checks. The
    // first version used an additive ember material and put the button at y=70 in
    // a 100-tall panel: the glow read as "a picture", and the label (centred in
    // the 60pt frame, so at y=100) landed exactly on the panel's edge and was cut
    // away. Three of the four things you need to see the bug were invisible.
    //
    // What to look for: each button is a CHECKER (the same material as the image
    // above, so "material" is visible), each is DIMMED (disabled), and each dim is
    // clipped to ITS OWN panel. Before the fix both dim overlays took the LAST
    // panel's scissor and the top button's escaped its panel entirely.
    const Material checker2 = g_r.make_material(g_r.sprite_pipeline, g_check_tex, g_r.default_sampler);
    for (int i = 0; i < 2; ++i) {
        const f32 x = 20.0f + static_cast<f32>(i) * 200.0f;
        const f32 y = 240.0f + static_cast<f32>(i) * 120.0f;
        u16    clip = g_ui.panel(x, y, 170.0f, 100.0f, palette::GRAY_1D1D1D, root, bgst);
        g_ui.set_clip(clip, true);
        // y=52 in a 100-tall panel: the label (centred at 82) is inside, and the
        // bottom 12pt of the button is not.
        u16 b = g_ui.button(10.0f, 52.0f, 150.0f, 60.0f, "disabled", palette::WHITE, palette::WHITE, nullptr, clip, 0.32f, st);
        g_ui.set_material(b, checker2);
        g_ui.set_enabled(b, false); // <- the overlay that Pass 1.5 draws
        g_ui.label(x, y + 108.0f, "dim clipped to its own panel", palette::GRAY_LIGHT, 0.30f, root);
    }
    g_ui.label(20.0f, 212.0f, "Pass 1.5 canary: two DISABLED material buttons,", palette::WHITE, 0.32f, root);
    g_ui.label(420.0f, 240.0f, "each in its own clipped panel, each", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, 262.0f, "straddling its edge - so each dim", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, 284.0f, "overlay has to be clipped to the", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, 306.0f, "panel it belongs to.", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, 340.0f, "Pass 1.5 scissored per widget but", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, 362.0f, "flushed ONCE, so both overlays took", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, 384.0f, "the LAST rect - and the top", palette::GRAY_LIGHT, 0.30f, root);
    g_ui.label(420.0f, 406.0f, "button's escaped its panel.", palette::GRAY_LIGHT, 0.30f, root);
}

void BuildStress() noexcept {
    u8 st = GalleryStyle();
    u16 title_root = g_ui.panel(40.0f, PAGE_Y, g_view_w - 80.0f, 44.0f, palette::PANEL, UINT16_MAX, st);
    g_ui.label(20.0f, 8.0f, "Stress (D9) - full pool, every widget type", palette::WHITE, 0.50f, title_root);
    // Fill the pool with a repeating mix of every widget type.
    u16 n = 0;
    f32    x = 20.0f, y = PAGE_Y + 54.0f;
    while (n < ui::Manager::MAX - 4) {
        if (x + 130.0f > g_view_w - 20.0f) {
            x = 20.0f;
            y += 46.0f;
        }
        if (y + 40.0f > g_view_h - 20.0f) {
            break;
        }
        u16 id = UINT16_MAX;
        // NOTE: stress cells use empty text — widget labels (toggle text now
        // draws right of its track) would spill into neighbor cells.
        switch (n % 6) {
        case 0:
            id = g_ui.button(x, y, 120.0f, 36.0f, "Btn", palette::BUTTON_BG, palette::WHITE, OnLabButton, UINT16_MAX, 0.32f, st);
            break;
        case 1:
            id = g_ui.toggle(x, y, 120.0f, 36.0f, "", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, (n % 2) == 0, OnLabToggle, UINT16_MAX,
                             st);
            break;
        case 2:
            // Narrow slider: the engine % label draws right of the track.
            id = g_ui.slider(x, y, 80.0f, 30.0f, 0.5f, OnLabSlider, UINT16_MAX, st);
            break;
        case 3:
            id = g_ui.checkbox(x, y, "", palette::TOGGLE_GREEN, palette::GRAY_DARK, palette::WHITE, (n % 2) == 0, OnLabToggle, UINT16_MAX, st);
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
    case LabMode::Listboxes:
        BuildListboxes();
        break;
    case LabMode::Comboboxes:
        BuildComboboxes();
        break;
    case LabMode::Radioboxes:
        BuildRadioboxes();
        break;
    case LabMode::Progressbars:
        BuildProgressbars();
        break;
    case LabMode::Scrollviews:
        BuildScrollviews();
        break;
    case LabMode::Treeviews:
        BuildTreeviews();
        break;
    case LabMode::Accordions:
        BuildAccordions();
        break;
    case LabMode::Datagrids:
        BuildDatagrids();
        break;
    case LabMode::Images:
        BuildImages();
        break;
    case LabMode::Materials:
        // No demo widgets: RenderMaterials() draws raw batches + embers instead.
        break;
    default:
        break;
    }
    BuildNav(); // persistent [Prev]/[Next] on every page
    printf("[uilab] mode=%s widgets=%u view=%.0fx%.0f\n", ModeName(m), g_ui.count, g_view_w, g_view_h);
}

// ─── Materials demo (key M): one quad per engine-ported lab shader ───
void RenderMaterials(f32 dt) noexcept {
    // Title heading (this page has no widgets, so no title label).
    g_r.draw_text(g_r.default_font, "Materials (M) - engine ports of mm_06 lab shaders", 40.0f, PAGE_Y + 2.0f, palette::WHITE,
                  0.50f);
    // 2 rows x 3 + 1 wide quad, below the heading.
    const f32 qw = 230.0f, qh = 150.0f, gx = 250.0f, gy = 176.0f;
    const f32 ox = (g_view_w - (2.0f * gx + qw)) * 0.5f;
    const f32 oy = 244.0f;
    auto quad_xy = [&](u16 i, f32 &x, f32 &y) noexcept {
        x = ox + qw * 0.5f + static_cast<f32>(i % 3) * gx;
        y = oy + qh * 0.5f + static_cast<f32>(i / 3) * gy;
    };
    auto caption = [&](const char *s, f32 x, f32 y) noexcept {
        f32 tw = 0.0f, th = 0.0f;
        g_r.measure_text(g_r.default_font, s, 0.32f, tw, th);
        g_r.draw_text(g_r.default_font, s, x - tw * 0.5f, y, palette::WHITE, 0.32f);
    };

    // Animated light orbiting overhead (drives normal/cartoon/plastic).
    f32 az = g_time * 0.5f;
    f32 el = 0.85f;
    f32 ce = __builtin_cosf(el);
    f32 lx = ce * __builtin_cosf(az), ly = ce * __builtin_sinf(az), lz = __builtin_sinf(el);

    f32 x = 0.0f, y = 0.0f;
    // Row 0: derive / normal_map / cartoon (checker albedo shows edges).
    quad_xy(0, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw, qh, 0.0f, palette::WHITE, 1);
    g_r.flush_normal_derive(g_batch, g_check_tex, g_r.default_sampler, lx, ly, lz, 1.0f, 0.25f, 0.5f, 2.0f);
    caption("normal_derive", x, y + qh * 0.5f + 16.0f);
    quad_xy(1, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw, qh, 0.0f, palette::WHITE, 1);
    g_r.flush_normal_map(g_batch, g_check_tex, g_sinenrm_tex, g_r.default_sampler, lx, ly, lz, 1.0f, 0.25f, 0.5f);
    caption("normal_map", x, y + qh * 0.5f + 16.0f);
    quad_xy(2, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw, qh, 0.0f, palette::WHITE, 1);
    g_r.flush_cartoon(g_batch, g_check_tex, g_r.default_sampler, lx, ly, lz, 1.0f, 0.25f, 0.5f, 2.0f, 3.0f, 0.35f, 0.85f);
    caption("cartoon", x, y + qh * 0.5f + 16.0f);
    // Row 1: plastic / glow_pulse / gold_border.
    quad_xy(3, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw, qh, 0.0f, palette::WHITE, 1);
    g_r.flush_plastic(g_batch, g_check_tex, g_sinenrm_tex, g_r.default_sampler, lx, ly, lz, 1.0f, 0.25f, 0.5f, 0.6f, 0.5f, 0.4f, 120.0f);
    caption("plastic", x, y + qh * 0.5f + 16.0f);
    quad_xy(4, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw * 1.5f, qh * 1.5f, 0.0f, palette::WHITE, 1);
    g_r.flush_glow_pulse(g_batch, g_check_tex, g_r.default_sampler, 1.2f, 0.12f, 0.025f, 0xFF00CED1u, qw / qh, 1.5f, 1.5f);
    caption("glow_pulse", x, y + qh * 0.75f + 16.0f);
    quad_xy(5, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw * 1.5f, qh * 1.5f, 0.0f, palette::WHITE, 1);
    g_r.flush_gold_border(g_batch, g_check_tex, g_r.default_sampler, 1.0f, 0.035f, 0xFFFFC740u, qw / qh, 1.5f, 1.5f);
    caption("gold_border", x, y + qh * 0.75f + 16.0f);
    // Row 2: gold_stay + ember corner.
    quad_xy(6, x, y);
    g_batch.reset();
    g_batch.add(x, y, qw * 1.5f, qh * 1.5f, 0.0f, palette::WHITE, 1);
    g_r.flush_gold_stay(g_batch, g_check_tex, g_r.default_sampler, 1.0f, 0.05f, 0xFFFFC740u, qw / qh, 1.5f, 1.5f);
    caption("gold_stay", x, y + qh * 0.75f + 16.0f);

    // Embers (EmberPool + additive material) drift in the corner.
    g_embers.update(dt, g_time);
    g_ebatch.reset();
    for (u16 i = 0; i < g_embers.count; ++i) {
        const EmberParticle &e = g_embers.embers[i];
        f32 t = (e.max_life > 0.0f) ? (e.life / e.max_life) : 0.0f;
        g_ebatch.add(e.pos.x, e.pos.y, EmberSize(e), EmberSize(e), 0.0f, EmberColor(t), 2);
    }
    g_r.flush_sprites(g_ebatch, g_r.make_material(g_r.sprite_additive_pipeline, g_ember_tex, g_r.default_sampler));
    caption("embers (EmberPool)", g_view_w - 150.0f, g_view_h - 60.0f);
}

// ─── HUD ─────────────────────────────────────────────────────────────
void DrawHud() noexcept {
    char buf[160];
    f32 y = 8.0f;
    auto line = [&](const char *s) noexcept {
        g_r.draw_text(g_r.default_font, s, 10.0f, y, palette::WHITE, 0.34f);
        y += 20.0f;
    };
    // FIRST, and reporting the PREVIOUS frame's drops (Renderer clears the counter
    // in end_frame). Both halves are load-bearing. The shared text budget is
    // ~1024 glyphs for the WHOLE frame and draw_text drops surplus calls
    // SILENTLY, so whatever a page draws after the cap simply never appears - and
    // the HUD, drawn last, plus the labels a page created last, are exactly what
    // disappears. A report drawn after the overflow is itself part of the overflow;
    // this one is drawn first and reads last frame's number, so it survives.
    if (g_r.text_dropped_calls > 0) {
        snprintf(buf, sizeof(buf), "TEXT OVERFLOW last frame: %u draw_text dropped", g_r.text_dropped_calls);
        line(buf);
    }
    snprintf(buf, sizeof(buf), "UI LAB  mode=%s (%d/%d)  fps=%.0f  widgets=%u/%u freelist=%u", ModeName(g_mode),
             static_cast<int>(g_mode) + 1, static_cast<int>(LabMode::_Count), g_fps_ema, g_ui.count, ui::Manager::MAX,
             g_ui.freelist_count);
    line(buf);
    snprintf(buf, sizeof(buf), "view=%.0fx%.0f r=%ux%u scale=%.2f", g_view_w, g_view_h, g_r.width, g_r.height, g_r.content_scale);
    line(buf);
    // (ids print raw; 65535 = UINT16_MAX = none)
    snprintf(buf, sizeof(buf), "hot=%u active=%u clicked=%u focus=%u editing=%u", g_ui.hot, g_ui.active, g_ui.clicked, g_ui.focus_id,
             g_ui.editing_id);
    line(buf);
    snprintf(buf, sizeof(buf), "bench ms: measure=%.2f layout=%.2f render=%.2f  text %u/%u", g_ms_measure, g_ms_layout, g_ms_render,
             g_r.text_vertex_count, g_r.MAX_TEXT_VERTS);
    line(buf);
    snprintf(buf, sizeof(buf), "tune: font=%.2f spacing=%.0f corner=%.2f border=%.3f", g_font_scale, g_spacing, g_corner, g_border_w);
    line(buf);
    snprintf(buf, sizeof(buf), "last: %s", g_last_event);
    line(buf);
    if (g_slider_n > 0) {
        snprintf(buf, sizeof(buf), "sliders: %.2f %.2f %.2f %.2f", g_slider_seen[0], g_slider_seen[1], g_slider_seen[2], g_slider_seen[3]);
        line(buf);
    }
    u32 total = 0;
    for (u32 c : g_clicks) {
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
    for (f32 &f : g_slider_seen) {
        f = 0.0f;
    }
    BuildPage(g_mode);
    BuildMaterialsResources();
    printf("[uilab] keys: D1-9 pages | D0 listboxes | C combos | O radios | P progress | M materials | Up/Down font | Left/Right spacing | +/- corner | B border | Space pause | R rebuild\n");
}

void game_resize(void *, u32 w, u32 h) {
    if (w > 0) {
        g_view_w = static_cast<f32>(w);
    }
    if (h > 0) {
        g_view_h = static_cast<f32>(h);
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
    s_modal_root = UINT16_MAX; // leaving the page closes the demo dialog
    BuildPage(g_mode);
}

void game_frame(void *, f32 dt, InputState &input) noexcept {
    if (dt > 0.0f) {
        f32 fps = 1.0f / dt;
        g_fps_ema += (fps - g_fps_ema) * 0.05f;
    }
    f32 udt = g_paused ? 0.0f : dt;
    if (!g_paused) {
        g_time += dt;
    }

    // Page/tune hotkeys are skipped while a widget captures text (TextField
    // editing, editable ComboBox open/focused): keystrokes belong to the
    // filter/field — otherwise typing 'm'/'c'/digits jumps pages and wipes
    // the text via rebuild. Up/Down (font scale, no rebuild) stay live.
    bool text_cap = g_ui.is_text_capture();
    if (!text_cap) {
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
        } else if (input.just_pressed(KeyCode::D0)) {
            SetMode(LabMode::Listboxes);
        } else if (input.just_pressed(KeyCode::M)) {
            SetMode(LabMode::Materials);
        } else if (input.just_pressed(KeyCode::C)) {
            SetMode(LabMode::Comboboxes);
        } else if (input.just_pressed(KeyCode::O)) {
            SetMode(LabMode::Radioboxes);
        } else if (input.just_pressed(KeyCode::P)) {
            SetMode(LabMode::Progressbars);
        } else if (input.just_pressed(KeyCode::V)) {
            SetMode(LabMode::Scrollviews);
        } else if (input.just_pressed(KeyCode::T)) {
            SetMode(LabMode::Treeviews);
        } else if (input.just_pressed(KeyCode::A)) {
            SetMode(LabMode::Accordions);
        } else if (input.just_pressed(KeyCode::G)) {
            SetMode(LabMode::Datagrids);
        } else if (input.just_pressed(KeyCode::I)) {
            SetMode(LabMode::Images);
        } else if (input.just_pressed(KeyCode::Space)) {
            g_paused = !g_paused;
        } else if (input.just_pressed(KeyCode::R)) {
            BuildPage(g_mode); // rebuild (re-registers styles, resets widgets)
        } else if (input.just_pressed(KeyCode::B)) {
            g_border_w = (g_border_w > 0.001f) ? 0.0f : 0.08f;
            printf("[uilab] border=%.3f\n", g_border_w);
            BuildPage(g_mode);
        }
    } // !text_cap: Up/Down move AFTER handle(), so a focused widget can claim them first
    // Spacing rebuilds the page (wipes a live filter) — skip while capturing.
    if (input.just_pressed(KeyCode::Right) && !text_cap) {
        g_spacing += 2.0f;
        if (g_spacing > 32.0f) {
            g_spacing = 32.0f;
        }
        printf("[uilab] spacing=%.0f\n", g_spacing);
        BuildPage(g_mode);
    }
    if (input.just_pressed(KeyCode::Left) && !text_cap) {
        g_spacing -= 2.0f;
        if (g_spacing < 0.0f) {
            g_spacing = 0.0f;
        }
        printf("[uilab] spacing=%.0f\n", g_spacing);
        BuildPage(g_mode);
    }
    if (!text_cap) {
        for (u8 i = 0; i < input.text_count; ++i) {
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
    }

    g_ui.handle(input);
    g_ui.update(udt);

    // Up/Down tune the font, but ONLY when no focused widget owns them: a
    // focused ListBox/ComboBox/TabBar consumes them for selection, and a text
    // field for its caret. Binding both meant the listbox page's own hint
    // ("Up/Dn moves selection") ALSO shrank the global font 0.1 per press - and
    // the shrunken text survived every page switch, which reads as "the listbox
    // lost its text". After handle(), because that is when the flag is known.
    if (!g_ui.nav_consumed_this_frame()) {
        if (input.just_pressed(KeyCode::Up)) {
            g_font_scale = g_font_scale < 2.0f ? g_font_scale + 0.1f : 2.0f;
            g_ui.theme.font_scale = g_font_scale;
            printf("[uilab] font_scale=%.2f\n", g_font_scale);
        }
        if (input.just_pressed(KeyCode::Down)) {
            g_font_scale = g_font_scale > 0.5f ? g_font_scale - 0.1f : 0.5f;
            g_ui.theme.font_scale = g_font_scale;
            printf("[uilab] font_scale=%.2f\n", g_font_scale);
        }
    }

    // Progressbars demo: bottom bar chases a sine target (exercises the
    // update() smoothing live; id refreshed on every rebuild). Circles
    // sweep with phase offsets so the arc redraw is visible in motion.
    // The gradient bar is animated too, so its own per-frame flush is visible
    // (and a half-full bar proves the ramp is NOT stretched to the track).
    if (g_mode == LabMode::Progressbars && s_grad_bar != UINT16_MAX) {
        g_ui.set_progress(s_grad_bar, 0.5f + 0.5f * __builtin_sinf(g_time * 1.4f + 0.7f));
    }
    // The striped bar animates too, so the drift + the fixed pitch are both
    // visible in a screenshot pair.
    if (g_mode == LabMode::Progressbars && s_stripe_bar != UINT16_MAX) {
        g_ui.set_progress(s_stripe_bar, 0.5f + 0.5f * __builtin_sinf(g_time * 1.1f + 2.2f));
    }
    if (g_mode == LabMode::Progressbars && s_anim_bar != UINT16_MAX) {
        g_ui.set_progress(s_anim_bar, 0.5f + 0.5f * __builtin_sinf(g_time * 2.0f));
    }
    if (g_mode == LabMode::Progressbars && s_anim_c0 != UINT16_MAX) {
        g_ui.set_progress(s_anim_c0, 0.5f + 0.5f * __builtin_sinf(g_time * 1.4f));
    }
    if (g_mode == LabMode::Progressbars && s_anim_c1 != UINT16_MAX) {
        // Dwell waveform (6s cycle): ramp up, HOLD at full (the displayed
        // value lands, so the completion sparkle pops), ramp down, hold
        // empty. A plain sine never quite lands (smoothing lag), so it
        // would never trigger the burst.
        f32 ph = fmodf(g_time, 6.0f);
        f32 v1;
        if (ph < 2.0f) {
            f32 u = ph / 2.0f;
            v1      = u * u * (3.0f - 2.0f * u);
        } else if (ph < 3.0f) {
            v1 = 1.0f;
        } else if (ph < 5.0f) {
            f32 u = (ph - 3.0f) / 2.0f;
            v1      = 1.0f - u * u * (3.0f - 2.0f * u);
        } else {
            v1 = 0.0f;
        }
        g_ui.set_progress(s_anim_c1, v1);
    }

    g_r.advance_time(udt);

    f32 t0 = NowMs();
    if (g_mode == LabMode::Materials) {
        // Materials demo: raw batches through the engine-ported lab
        // pipelines + EmberPool. No widgets on this page.
        g_ui.measure(g_r);
        f32 t1 = NowMs();
        g_ui.layout(g_r);
        f32 t2 = NowMs();

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
        f32 t3 = NowMs();

        // Nav buttons live in the widget pool even on this page.
        g_batch.reset();
        g_ui.render(g_r, g_batch);

        g_batch.reset();
        f32 hud_w = g_view_w - 16.0f;
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
    f32 t1 = NowMs();
    g_ui.layout(g_r); // MUST run every frame: positions HBox/VBox children
    f32 t2 = NowMs();

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
    g_ui.render(g_r, g_batch);
    f32 t3 = NowMs();

    // HUD backing strip: full width so long lines never clip off the rect.
    g_batch.reset();
    f32 hud_w = g_view_w - 16.0f;
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

extern "C" AppCallbacks markmos_main(int argc, char **argv) {
    // Optional initial page: ./mm_07_ui_lab d7 (also useful for screenshots)
    if (argc > 1 && argv && argv[1] && argv[1][0] == 'd' && argv[1][1] >= '1' && argv[1][1] <= '9' && argv[1][2] == '\0') {
        g_mode = static_cast<LabMode>(argv[1][1] - '1');
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'd' && argv[1][1] == '0' && argv[1][2] == '\0') {
        g_mode = LabMode::Listboxes;
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'm' && argv[1][1] == '\0') {
        g_mode = LabMode::Materials;
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'c' && argv[1][1] == '\0') {
        g_mode = LabMode::Comboboxes;
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'o' && argv[1][1] == '\0') {
        g_mode = LabMode::Radioboxes;
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'p' && argv[1][1] == '\0') {
        g_mode = LabMode::Progressbars;
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'v' && argv[1][1] == '\0') {
        g_mode = LabMode::Scrollviews;
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 't' && argv[1][1] == '\0') {
        g_mode = LabMode::Treeviews;
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'a' && argv[1][1] == '\0') {
        g_mode = LabMode::Accordions;
    } else if (argc > 1 && argv && argv[1] && argv[1][0] == 'g' && argv[1][1] == '\0') {
        g_mode = LabMode::Datagrids;
    }
    else if (argc > 1 && argv && argv[1] && argv[1][0] == 'i' && argv[1][1] == '\0') {
        g_mode = LabMode::Images;
    }
    // The lab asks for a TALL window. The historical default (900x640) leaves
    // 410pt of page below the 196pt HUD, which is less than several demo pages
    // need - the Accordion page's last section has always been below the fold, so
    // a clip canary placed there would never have been looked at. AppCallbacks
    // defaults both fields to 0, which keeps the 900x640 frame for every other
    // app, so this touches nothing but this lab.
    return {.user_data = nullptr,
            // Declaration order matters even with designators: C++ requires them
            // in the order the members are declared (title/width/height come
            // before init/frame/resize/cleanup here).
            .title   = "UI Lab",
            .width   = 1000,
            .height  = 900,
            .init    = game_init,
            .frame   = game_frame,
            .resize  = game_resize,
            .cleanup = game_cleanup};
}

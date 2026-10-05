// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Shader Lab — visual playground for existing engine shaders + hot-reload.
// Scope: macOS/Metal only. Vulkan shows embedded shaders, lab-file mode disabled.
//
// ─── HOW TO USE ─────────────────────────────────────────────────────
// Run (from repo root):
//   cmake -S . -B build_debug && cmake --build build_debug --target mm_06_shader_lab
//   ./build_debug/engine/mm_06_shader_lab
//
// Keys (D0-D9 = number-row digit keys, NOT numpad; see KeyCode in
// input/mm_input_event.hpp, macOS mapping in app/mm_app_mac.mm):
//   D1 sprite | D2 grayscale | D3 dissolve | D4 outline | D5 color grade
//   D0 lab dissolve file (engine/shaders/lab/lab.frag.msl)
//   D6 derive normal in-shader (lab_derive.frag.msl, Sobel on checker)
//   D7 procedural normal texture (lab_proc.frag.msl, sine heightfield CPU)
//   D8 PNG normal map (lab_png.frag.msl + lab_normal.png on disk)
//   D9 card (lab_card.png albedo sRGB + lab_card_normal.png, reuses png pipeline)
//   C cartoon (lab_cartoon.frag.msl on the same card, bands via typed +/-)
//   P plastic (lab_plastic.frag.msl: gloss + clearcoat + fresnel rim)
//   G glow pulse (lab_glow.frag.msl on the same card, teal ring expand+fade)
//   Y gold border (lab_gold.frag.msl on the same card, gold border breathe)
//   S stay (lab_stay.frag.msl: gold border stays on + gentle pulse)
//   X sunburst (lab_sun.frag.msl: slow-rotating gold rays, Up/Dn speed,
//     Left/Right ray count, +/- intensity)
//   A aura (lab_aura.frag.msl: breathing inner light + warm-white edge
//     frame pulse + spicules, Up/Dn breath speed, Left/Right spicule
//     count, +/- intensity; SPK/OPA on-screen steppers for filament
//     count + filament opacity)
//   R force-reload current lab file | Space pause time
//   Q/E previous/next shader page (same as the on-screen < PREV / NEXT >
//     buttons, bottom corners — click or tap them). Every page also shows
//     its effect keys on screen (bottom-center help + top-left page title).
//   Arrows: progress/edge in base modes 0-5; LIGHT az/el in normal modes
//     (D6/D7/D8/D9/C/P); duration/expand in glow G; speed/width in gold Y
//     and stay S; speed/rays in sun X; anchor in fire mode (D9/P + F).
//     +/- intensity in glow/gold/stay/sun modes.
//   F fire light toggle (D9/P only): 24 embers orbit an anchor, the anchor
//     drives the light dir (card center - anchor + height); arrows move the
//     anchor, +/- adjusts height. Cartoon bands: type + / - (text input).
//   W firework show page: enters the dedicated night-sky page (rockets
//     auto-launch on interval) plus one rocket right now. V enters the
//     page, or cycles the interval 1s → 2s → 3s when already there.
//     (Sky shader in engine/shaders/lab/lab_firework.frag.msl, Metal only
//     for the sky — CPU sparks render on any backend.)
//   H firework hue mode: single → dual (half/half) → rainbow (per spark).
//   J/L firework aim (launch x, % of width); I/K burst apex (% of height,
//     power derived: vy coasts exactly to the apex).
//   T firework tune mode: U/O selects gravity/size/count,
//     typed -/= adjusts (intercepted before other modes' +/-).
//   B firework spark shape: cycles blob → sparkle4 (4-point
//     star) → ring → cross; applies to the next launch.
//   N firework burst pattern: cycles sphere → ring → willow →
//     palm → saturn → f64-ring → chrysanthemum → crossette → heart →
//     strobe; applies to the next launch.
//
// Hot-reload workflow:
//   1. Press D0/D6/D7/D8/C/P/G/Y/S/X to enter a lab-file mode.
//   2. Edit the corresponding .msl in engine/shaders/lab/, save.
//   3. Pipeline rebuilds automatically within ~0.25s (mtime poll).
//   4. If the shader has a compile error, the old pipeline keeps
//      rendering and the Metal error prints to the console
//      ("FS COMPILE ERROR ...") — fix and save again.
//   5. D8 also hot-reloads lab_normal.png (texture re-upload; recreates
//      the GPU texture when the image size changes).
//
// Lab file rules (must hold or compile fails):
//   - Entry names are per-file: lab_fragment_main, lab_derive_main,
//     lab_proc_main, lab_png_main, lab_cartoon_main, lab_plastic_main,
//     lab_glow_main, lab_gold_main, lab_stay_main, lab_sun_main
//     (looked up by name).
//   - VSOutput MUST match sprite_vertex_msl (position/uv/color).
//   - Params blocks at [[buffer(2)]] use float4 members ONLY:
//     LabParams {progress, edge, _pad, _pad, rgba},
//     LabNormalParams {light_dir(xyz+w), misc(ambient, spec, hscale, spare)},
//     LabCartoonParams adds toon(bands, ink_thresh, ink_strength, spare),
//     LabPlasticParams adds plastic(clearcoat, fresnel, wrap, shininess).
//     (Metal packs vectors at 16-byte boundaries — packed scalars before
//     a vector read back as garbage, e.g. the green-glow bug.)
//   - Normal-map texture (D7/D8) is bound by the host with logical index 2
//     which lands on [[texture(1)]]/[[sampler(1)]] — Metal backend quirk:
//     logical index 1 aliases slot 0, never use it for a second texture.
//   - PNG normal maps must be tangent-space RGB/RGBA8 uploaded UNORM
//     (linear data, never sRGB).
//   - Metal only. There is intentionally no Vulkan/SPIR-V path here.
//
// ─── EXPECTED VIEW ────────────────────────────────────────────────
// Default (dissolve, progress=0.35): window "Markmos" 900x640, dark navy
// background, 6 sharp rectangles in a centered 3x2 grid (row 0:
// red/green/blue, row 1: yellow/magenta/white). Each quad shows a checker
// pattern with ~35% dissolved holes and an orange glowing edge per hole.
// (Solid quads = sprite mode, press D1.)
// Every page shows an overlay: top-left "N/17 MODE" title, < PREV / NEXT >
// buttons at the bottom corners (click/tap, or Q/E keys), and a 4-line key
// hint bottom-center (2 mode-specific lines + 2 global lines).
//
// Normal modes (6/7/8): same grid, but each quad is LIT — bright on the
// side facing the light, dark on the far side, specular dot near the
// light. D6 shows hard beveled-tile shading (checker edges only);
// D7 smooth sine bumps; D8 follows lab_normal.png (flat-lit when missing).
// D9 shows one big king_of_hearts (white tint — tint multiplies albedo)
// with emboss relief on the print; arrows move the light.
// C shows the same card as cartoon: flat bands (default 3, +/- to change)
// + black ink on every print edge; arrows move the light.
// P shows the same card as glossy plastic: soft wrapped diffuse, tight
// highlight, fresnel clearcoat + cool rim; arrows move the light.
// F (D9/P) adds ~24 additive fire embers around an anchor; shading follows
// the anchor as the light source — drag it with arrows around the card.
// W (any mode) enters the firework show page: dark night background, one
// rocket per press plus auto-launch every 1/2/3s (V cycles); B cycles the
// spark shape of the next burst: soft blob, 4-point sparkle star, ring,
// cross.
// N cycles the burst layout: sphere shell, flat ring, drooping willow
// (sheds trail puffs), upward palm fan, saturn (shell + tilted ring),
// f64 concentric rings, chrysanthemum (all sparks trail), crossette
// (parents split into 4 mid-flight), aimed heart, strobe pistil
// (hue shell + blinking white core).
// X shows the same card with slow-rotating gold sunburst rays (default
// 0.05Hz ≈ 20s/rev); Up/Down = speed, Left/Right = ray count, +/- intensity.
// A shows the same card breathing warm inner light (0 → max → normal);
// as the light rises a warm-white frame hugs the edge with an orbiting
// pulse, and same-colored spicules fire outward in time with the pulse.
// Arrows move the light; shading must track immediately.
//
// Notes:
//   - SpriteBatch::add(x, y, ...) takes CENTER coords (quads emit ±half-size).
//   - World coords == window points, origin top-left (matching ortho_2d).
//   - The generic 64-byte params block reinterprets its prefix per shader:
//     grayscale=amount, outline=color+thickness, color_grade=BCS+identity hue,
//     normal=light_dir(xyz+w)+misc(ambient,spec,hscale,spare).
// ────────────────────────────────────────────────────────────────────

#include "../app/mm_app.hpp"
#include "../core/mm_arena.hpp"
#include "../math/mm_math.h"
#include "../math/mm_mat4.h"
#include "../render/mm_shader_registry.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#if defined(USE_METAL_BACKEND)
#include "../rhi/mm_metal_backend.hpp"
#elif defined(USE_VULKAN_BACKEND)
#include "../rhi/mm_vulkan_backend.hpp"
#endif
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace {

// stb_image (implementation linked from mmx_engine) — declared locally to
// keep this example free of the VFS/texture-loader headers.
extern "C" {
unsigned char *stbi_load_from_memory(unsigned char const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
void stbi_image_free(void *retval_from_stbi_load);
}

enum class LabMode : u8 { Sprite, Grayscale, Dissolve, Outline, ColorGrade, LabFile, NormalDerive, NormalProc, NormalPng, CardNormal, CardCartoon, CardPlastic, CardGlow, CardGold, CardStay, CardSun, CardAura, Firework, _Count };

bool IsNormalMode(LabMode m) noexcept {
    return m == LabMode::NormalDerive || m == LabMode::NormalProc || m == LabMode::NormalPng || m == LabMode::CardNormal ||
           m == LabMode::CardCartoon || m == LabMode::CardPlastic;
}

// D9 card look state (cartoon bands live here too — same albedo).
static f32 g_cartoon_bands = 3.0f;

// Fire light (D9/P only): ember particles orbiting an anchor; the anchor
// drives the light direction (card center - anchor, + height).
static constexpr u16 FIRE_N = 24;
struct Ember {
    f32 x, y, vx, vy, life, max_life, size, seed;
};
static Ember g_embers[FIRE_N];
static SpriteBatch g_fire_batch;
static PipelineHandle g_glow_pipe;
static bool g_glow_valid = false;
static TextureHandle g_glow_tex;
static bool g_fire_on = false;
static f32 g_anchor_x = 0.0f;
static f32 g_anchor_y = 0.0f;
static f32 g_light_h = 300.0f; // light height above card plane (points)
static f32 g_card_cx = 0.0f;
static f32 g_card_cy = 0.0f;
static u32 g_rng = 12345u;

// Firework show (W key, any mode): one press = one rocket from the bottom
// edge; at its apex it bursts into tinted sparks (CPU, additive glow pipe)
// under a hot-reload night-sky quad (lab_firework.frag.msl — Metal only
// for the sky; sparks render on any backend).
static constexpr u16 FW_ROCKETS = 4;
static constexpr u16 FW_SPARKS = 256;
static constexpr u16 FW_SHAPES = 4; // blob, 4-point sparkle, ring, cross
struct Rocket {
    bool active = false;
    f32 x = 0.0f, y = 0.0f, vy = 0.0f, target_y = 0.0f, hue = 0.0f;
};
struct Spark {
    bool active = false;
    f32 x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f, life = 0.0f, max_life = 1.0f, size = 10.0f;
    u32 color = 0xFFFFFFFFu;
    u8 shape = 0;
    f32 grav = 260.0f; // per-spark gravity (willow droops harder, rings f32)
    bool trail = false;  // willow/chrysanthemum shed short fading trail puffs
    u8 tick = 0;
    f32 fuse = 0.0f;  // crossette: seconds until this spark splits (>0 = will split)
    bool strobe = false; // strobe pistil: inner sparks blink via tick
};
static Rocket g_rockets[FW_ROCKETS];
static Spark g_sparks[FW_SPARKS];
static SpriteBatch g_fw_batch[FW_SHAPES]; // one additive batch per spark shape
static TextureHandle g_spark_tex[FW_SHAPES]; // one procedural texture per shape
static u16 g_fw_shape = 0; // shape of the next burst (B cycles it)
static constexpr const char *kFwShapeNames[FW_SHAPES] = {"blob", "sparkle4", "ring", "cross"};
static constexpr u16 FW_PATTERNS = 10; // sphere, ring, willow, palm,
                                            // saturn, f64-ring, chrysanthemum,
                                            // crossette, heart, strobe
static u16 g_fw_pattern = 0; // burst layout of the next rocket (N cycles it)
static constexpr const char *kFwPatternNames[FW_PATTERNS] = {"sphere",   "ring",  "willow", "palm",   "saturn",
                                                             "f64",   "chrys", "cross",  "heart",  "strobe"};
// Phase 2: show-page auto-launch, multi-hue bursts, aim/power, live tuning.
static bool g_fw_auto = false; // true only on the Firework page (set by SetMode)
static f32 g_fw_auto_t = 0.0f;
static constexpr f32 kFwIntervals[3] = {1.0f, 2.0f, 3.0f}; // V cycles the auto-show period
static u16 g_fw_interval_sel = 0;
static u16 g_fw_hue_mode = 0; // 0 single, 1 dual (half/half), 2 rainbow
static constexpr const char *kFwHueNames[3] = {"single", "dual", "rainbow"};
static f32 g_fw_aim_x = 0.5f; // launch position, fraction of width (J/L)
static f32 g_fw_apex = 0.30f; // burst height, fraction of height (I/K)
static bool g_fw_tune = false;
static u16 g_fw_tune_sel = 0; // 0 gravity, 1 size, 2 count
static constexpr const char *kFwTuneNames[3] = {"gravity", "size", "count"};
static f32 g_fw_grav = 260.0f;      // tune: 60..600
static f32 g_fw_size_scale = 1.0f;  // tune: 0.5..2.0
static f32 g_fw_count_scale = 1.0f; // tune: 0.5..2.0
static SpriteBatch g_fw_sky_batch; // one fullscreen sky quad
static PipelineHandle g_fw_pipe;
static bool g_fw_valid = false;
static BufferHandle g_fw_param_ub;
static f32 g_fw_flash_u = 0.5f, g_fw_flash_v = 0.3f, g_fw_flash_i = 0.0f;
static f32 g_fw_sky_a = 0.0f;
static u32 g_fw_hue = 0;
static f32 g_fw_poll_t = 0.0f;
static constexpr u32 kFwPalette[5] = {0xFFFFCC44u, 0xFFFF4444u, 0xFF44FFDDu, 0xFFAA66FFu, 0xFF66FF88u};

f32 Rand01() noexcept {
    g_rng = g_rng * 1664525u + 1013904223u;
    return static_cast<f32>(g_rng >> 8) / 16777216.0f;
}

bool FireActive() noexcept; // defined after g_mode declaration below

// Defined below SetMode (needs backend resources); forward-declared here.
bool LoadPngNormalTexture() noexcept;
bool LoadCardAlbedo() noexcept;
bool LoadCardNormal() noexcept;
// CPU-only batch layout; forward-declared (defined after the texture builders).
void LayoutQuads() noexcept;

static constexpr size_t ARENA_SIZE = 2 * 1024 * 1024;
static constexpr f32 POLL_INTERVAL = 0.25f;

static SpriteBatch g_batch;
static BufferHandle g_vb, g_cam_ub, g_param_ub;
static PipelineHandle g_pipeline;
static TextureHandle g_texture;
static SamplerHandle g_sampler;
static bool g_pipe_valid = false;
alignas(64) static char g_arena_buf[ARENA_SIZE];
static FrameArena g_arena;

static LabMode g_mode = LabMode::Dissolve;
bool FireActive() noexcept {
    return g_fire_on && (g_mode == LabMode::CardNormal || g_mode == LabMode::CardPlastic);
}
static f32 g_time = 0.0f;
static bool g_paused = false;
static f32 g_progress = 0.35f;
static f32 g_edge_width = 0.08f;
// Glow pulse (G mode): teal outer ring, expand + fade loop.
static f32 g_glow_dur = 1.0f;      // pulse duration, seconds (Up/Down)
static f32 g_glow_expand = 0.12f;  // max ring travel, card-uv (Left/Right)
static f32 g_glow_intensity = 1.5f; // glow strength (typed +/-)
// Gold border (Y mode): static gold band, breathing alpha (no travel).
static f32 g_gold_speed = 1.0f;     // pulse speed, Hz (Up/Down)
static f32 g_gold_width = 0.035f;   // band half-width, card-uv (Left/Right)
static f32 g_gold_intensity = 1.5f; // glow strength (typed +/-)
// Stay mode (S): gold border stays on + gentle pulse.
static f32 g_stay_speed = 1.0f;     // pulse speed, Hz (Up/Down)
static f32 g_stay_width = 0.035f;   // band half-width, card-uv (Left/Right)
static f32 g_stay_intensity = 1.0f; // glow strength (typed +/-)
// Sunburst (X): slow-rotating rays around the card center.
static f32 g_sun_speed = 0.05f;     // rotation speed, Hz (Up/Down, ~20s/rev default)
static f32 g_sun_rays = 12.0f;      // ray count (Left/Right)
static f32 g_sun_intensity = 1.0f;  // ray strength (typed +/-)
// Aura (A): breathing inner light + edge frame pulse + spicules.
static f32 g_aura_speed = 0.25f;     // breath speed, Hz (Up/Down, ~4s/cycle default)
static f32 g_aura_spikes = 12.0f;    // filament density (Left/Right, SPK steppers)
static f32 g_aura_intensity = 1.0f;  // aura strength (typed +/-)
static f32 g_aura_opacity = 0.9f;    // filament opacity, 0..2 (OPA steppers)
// Quad is k× the card size so the outer half of the band stays on-quad
// (a card-sized quad would clip it). Must stay in sync with the k remap
// in lab_glow.frag.msl; margin/side = (k-1)/2 = 0.25.
static constexpr f32 GLOW_QUAD_K = 1.5f;
static f32 g_light_az = 0.7f; // radians, arrows in normal modes
static f32 g_light_el = 0.85f;
static f32 g_view_w = 1280.0f;
static f32 g_view_h = 800.0f;

// ─── UI overlay: on-screen Prev/Next buttons + per-mode key hints ───
// 5x7 procedural font (row-major, bit4 = left pixel), baked to a 128x64
// texture: 16 cols x 8 rows of 8x8 cells, glyph 5x7 at cell offset (1,0)
// (1px empty margin all around for the shared linear sampler). A solid
// white 8x8 cell at (0,56) serves button backgrounds via add_frame
// (plain add() would sample the whole font atlas — full-texture UVs).
// Help strings must stay inside kUiFontChars (upper case + digits only).
static constexpr const char kUiFontChars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789<>+-/.:()|";
static constexpr u8 kUiFont5x7[][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // C
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}, // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, // I
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
    {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}, // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
    {0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x04}, // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1F, 0x11}, // W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // Z
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x07, 0x0C, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}, // 9
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, // <
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, // >
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, // +
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, // -
    {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}, // /
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, // .
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}, // :
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, // (
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, // )
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // |
};
static_assert(sizeof(kUiFont5x7) / sizeof(kUiFont5x7[0]) == sizeof(kUiFontChars) - 1, "font glyphs must match kUiFontChars");
static constexpr u16 UI_FONT_TEX_W = 128;
static constexpr u16 UI_FONT_TEX_H = 64;
static constexpr f32 UI_GLYPH_ADV = 7.0f; // px per char at scale 1 (5 glyph + 2 space)
static constexpr f32 UI_TEXT_SCALE = 2.0f;  // 5x7 px -> 10x14 px, 14px advance
static TextureHandle g_font_tex;
static PipelineHandle g_ui_pipe;
static bool g_ui_valid = false;
static SpriteBatch g_ui_batch;

int UiGlyphIndex(char c) noexcept {
    for (size_t i = 0; kUiFontChars[i] != '\0'; ++i) {
        if (kUiFontChars[i] == c) {
            return static_cast<int>(i);
        }
    }
    return 0; // unknown → space
}

void BuildFontTexture() noexcept {
    static u32 px[UI_FONT_TEX_W * UI_FONT_TEX_H];
    for (u32 i = 0; i < UI_FONT_TEX_W * UI_FONT_TEX_H; ++i) {
        px[i] = 0x00000000u;
    }
    size_t glyphs = sizeof(kUiFontChars) - 1; // exclude NUL
    for (size_t gi = 0; gi < glyphs; ++gi) {
        u16 col = static_cast<u16>(gi % 16);
        u16 row = static_cast<u16>(gi / 16);
        for (u16 gy = 0; gy < 7; ++gy) {
            u8 bits = kUiFont5x7[gi][gy];
            for (u16 gx = 0; gx < 5; ++gx) {
                if ((bits >> (4 - gx)) & 1u) {
                    u32 x = col * 8u + 1u + gx;
                    u32 y = row * 8u + gy;
                    px[y * UI_FONT_TEX_W + x] = 0xFFFFFFFFu;
                }
            }
        }
    }
    for (u32 y = 56; y < 64; ++y) { // solid white cell for button backgrounds
        for (u32 x = 0; x < 8; ++x) {
            px[y * UI_FONT_TEX_W + x] = 0xFFFFFFFFu;
        }
    }
    auto &bk = *g_backend;
    bk.update_texture(g_font_tex, px, 0, 0, UI_FONT_TEX_W, UI_FONT_TEX_H, 0, 0);
}

// Forward-declared (defined with the other pipeline builders below).
void FillPipelineDesc(PipelineDesc &pd, const ShaderDesc &vs, const void *fs_code, size_t fs_size, const char *fs_entry) noexcept;

// Alpha-blend textured-quad pipeline (embedded sprite shaders —
// FillPipelineDesc defaults are already SrcAlpha/OneMinusSrcAlpha,
// unlike the additive glow pipe).
bool BuildUiPipeline() noexcept {
    auto &bk = *g_backend;
    auto vs = shader::sprite_vertex();
    auto fs = shader::sprite_fragment();
    PipelineDesc pd;
    FillPipelineDesc(pd, vs, fs.code, fs.code_size, fs.entry);
    auto pr = bk.create_pipeline(pd);
    if (!pr) {
        return false;
    }
    g_ui_pipe = *pr;
    g_ui_valid = true;
    return true;
}

SpriteFrame UiGlyphFrame(int gi) noexcept {
    SpriteFrame fr{};
    fr.texture = g_font_tex;
    fr.x = static_cast<u16>((gi % 16) * 8 + 1);
    fr.y = static_cast<u16>((gi / 16) * 8);
    fr.w = 5;
    fr.h = 7;
    fr.tex_w = UI_FONT_TEX_W;
    fr.tex_h = UI_FONT_TEX_H;
    fr.pivot_x = 0.5f;
    fr.pivot_y = 0.5f;
    return fr;
}

f32 UiTextWidth(const char *s, f32 scale) noexcept {
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    if (n == 0) {
        return 0.0f;
    }
    return (static_cast<f32>(n) * UI_GLYPH_ADV - 2.0f) * scale;
}

// x,y = top-left of the text run (screen px, origin top-left).
void UiText(f32 x, f32 y, const char *s, f32 scale, u32 color) noexcept {
    f32 cx = x;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        SpriteFrame fr = UiGlyphFrame(UiGlyphIndex(s[i]));
        f32 gw = 5.0f * scale;
        f32 gh = 7.0f * scale;
        g_ui_batch.add_frame(cx + gw * 0.5f, y + gh * 0.5f, gw, gh, 0.0f, color, 0, fr);
        cx += UI_GLYPH_ADV * scale;
    }
}

void UiSolid(f32 cx, f32 cy, f32 w, f32 h, u32 color, f32 radius) noexcept {
    SpriteFrame fr{};
    fr.texture = g_font_tex;
    fr.x = 0;
    fr.y = 56;
    fr.w = 8;
    fr.h = 8;
    fr.tex_w = UI_FONT_TEX_W;
    fr.tex_h = UI_FONT_TEX_H;
    fr.pivot_x = 0.5f;
    fr.pivot_y = 0.5f;
    g_ui_batch.add_frame(cx, cy, w, h, 0.0f, color, 0, fr, radius);
}

static bool UiPointIn(f32 x, f32 y, f32 rx, f32 ry, f32 rw, f32 rh) noexcept {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// On-screen stepper row: label + value + [-][+] buttons.
// rx,ry = top-left (screen px). Returns -1/0/+1 on click.
int UiStepper(f32 rx, f32 ry, const char *label, const char *val, InputState &input) noexcept {
    constexpr f32 W = 190.0f;
    constexpr f32 H = 34.0f;
    constexpr f32 B = 34.0f;
    f32 label_h = 7.0f * UI_TEXT_SCALE;
    UiSolid(rx + W * 0.5f, ry + H * 0.5f, W, H, 0xB0282828u, 0.15f);
    UiText(rx + 10.0f, ry + (H - label_h) * 0.5f, label, UI_TEXT_SCALE, 0xFFFFFFFFu);
    f32 vw = UiTextWidth(val, UI_TEXT_SCALE);
    UiText(rx + W - 2.0f * B - 8.0f - vw, ry + (H - label_h) * 0.5f, val, UI_TEXT_SCALE, 0xFFDDDDDDu);
    f32 my = ry + (H - B) * 0.5f;
    f32 mx = rx + W - 2.0f * B;
    f32 px = rx + W - B;
    bool mhov = UiPointIn(input.mouse_x, input.mouse_y, mx, my, B, B);
    bool phov = UiPointIn(input.mouse_x, input.mouse_y, px, my, B, B);
    UiSolid(mx + B * 0.5f, my + B * 0.5f, B - 4.0f, B - 4.0f, mhov ? 0xE0505050u : 0xB0282828u, 0.15f);
    UiSolid(px + B * 0.5f, my + B * 0.5f, B - 4.0f, B - 4.0f, phov ? 0xE0505050u : 0xB0282828u, 0.15f);
    f32 mw = UiTextWidth("-", UI_TEXT_SCALE);
    f32 pw = UiTextWidth("+", UI_TEXT_SCALE);
    UiText(mx + (B - mw) * 0.5f, my + (B - label_h) * 0.5f, "-", UI_TEXT_SCALE, 0xFFFFFFFFu);
    UiText(px + (B - pw) * 0.5f, my + (B - label_h) * 0.5f, "+", UI_TEXT_SCALE, 0xFFFFFFFFu);
    for (u8 i = 0; i < input.action_count; ++i) {
        if (input.actions[i] != InputAction::Select) {
            continue;
        }
        if (UiPointIn(input.action_x, input.action_y, mx, my, B, B)) {
            return -1;
        }
        if (UiPointIn(input.action_x, input.action_y, px, my, B, B)) {
            return 1;
        }
    }
    return 0;
}

// Lab-file hot-reload state (Metal only) — one slot per editable file.
static constexpr size_t LAB_MAX = 16384;
struct LabFile {
    char code[LAB_MAX];
    size_t size = 0;
    long mtime = 0;
    char path[256]{};
    bool found = false;
};
struct LabSlot {
    const char *filename;
    const char *entry;
    LabMode mode;
};
static constexpr LabSlot kLabSlots[] = {
    {"lab.frag.msl", "lab_fragment_main", LabMode::LabFile},
    {"lab_derive.frag.msl", "lab_derive_main", LabMode::NormalDerive},
    {"lab_proc.frag.msl", "lab_proc_main", LabMode::NormalProc},
    {"lab_png.frag.msl", "lab_png_main", LabMode::NormalPng},
    {"lab_cartoon.frag.msl", "lab_cartoon_main", LabMode::CardCartoon},
    {"lab_plastic.frag.msl", "lab_plastic_main", LabMode::CardPlastic},
    {"lab_glow.frag.msl", "lab_glow_main", LabMode::CardGlow},
    {"lab_gold.frag.msl", "lab_gold_main", LabMode::CardGold},
    {"lab_stay.frag.msl", "lab_stay_main", LabMode::CardStay},
    {"lab_sun.frag.msl", "lab_sun_main", LabMode::CardSun},
    {"lab_aura.frag.msl", "lab_aura_main", LabMode::CardAura},
};
static constexpr size_t kLabSlotCount = sizeof(kLabSlots) / sizeof(kLabSlots[0]);
static LabFile g_lab_files[kLabSlotCount];
static LabFile g_fw_file; // firework sky source, polled independent of LabMode
static f32 g_poll_t = 0.0f;

// Normal-map texture (D7 procedural / D8 PNG), UNORM linear data.
static TextureHandle g_normal_tex;
static u16 g_normal_w = 0;
static u16 g_normal_h = 0;
static bool g_normal_valid = false;
static char g_png_path[256]{};
static long g_png_mtime = 0;
static bool g_png_found = false;
static constexpr size_t PNG_MAX = 262144;
static constexpr u16 NRM_SIZE = 128;

const char *ModeName(LabMode m) noexcept {
    switch (m) {
    case LabMode::Sprite: return "sprite";
    case LabMode::Grayscale: return "grayscale";
    case LabMode::Dissolve: return "dissolve";
    case LabMode::Outline: return "outline";
    case LabMode::ColorGrade: return "color_grade";
    case LabMode::LabFile: return "lab_file";
    case LabMode::NormalDerive: return "normal_derive";
    case LabMode::NormalProc: return "normal_proc";
    case LabMode::NormalPng: return "normal_png";
    case LabMode::CardNormal: return "card";
    case LabMode::CardCartoon: return "cartoon";
    case LabMode::CardPlastic: return "plastic";
    case LabMode::CardGlow: return "glow";
    case LabMode::CardGold: return "gold";
    case LabMode::CardStay: return "stay";
    case LabMode::CardSun: return "sun";
    case LabMode::CardAura: return "aura";
    case LabMode::Firework: return "firework";
    default: return "?";
    }
}

int LabSlotForMode(LabMode m) noexcept {
    if (m == LabMode::CardNormal) {
        m = LabMode::NormalPng; // D9 reuses the lab_png pipeline
    }
    for (size_t i = 0; i < kLabSlotCount; ++i) {
        if (kLabSlots[i].mode == m) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool TryStat(const char *path, long &out_mtime) noexcept {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    out_mtime = static_cast<long>(st.st_mtime);
    return true;
}

// Search the lab directory from likely working directories
// (repo root, build dir, or the lab dir itself).
bool LocateInLabDir(const char *filename, char *out_path, size_t cap, long *out_mtime) noexcept {
    static const char *kDirs[] = {
        "engine/shaders/lab/",
        "../engine/shaders/lab/",
        "../../engine/shaders/lab/",
        "",
    };
    for (const char *d : kDirs) {
        size_t dn = strlen(d);
        size_t fn = strlen(filename);
        if (dn + fn + 1 > cap) {
            continue;
        }
        char tmp[256];
        if (dn + fn + 1 > sizeof(tmp)) {
            continue;
        }
        memcpy(tmp, d, dn);
        memcpy(tmp + dn, filename, fn + 1);
        long mt = 0;
        if (TryStat(tmp, mt)) {
            memcpy(out_path, tmp, dn + fn + 1);
            if (out_mtime) {
                *out_mtime = mt;
            }
            return true;
        }
    }
    return false;
}

bool ReadLabFile(LabFile &f) noexcept {
    struct stat st;
    if (stat(f.path, &st) != 0) {
        return false;
    }
    if (st.st_size <= 0 || static_cast<size_t>(st.st_size) >= LAB_MAX) {
        return false;
    }
    FILE *fp = fopen(f.path, "rb");
    if (!fp) {
        return false;
    }
    size_t n = fread(f.code, 1, LAB_MAX - 1, fp);
    fclose(fp);
    if (n == 0) {
        return false;
    }
    f.code[n] = '\0';
    f.size = n + 1; // Metal source needs NUL terminator inside code_size
    f.mtime = static_cast<long>(st.st_mtime);
    return true;
}

void FillPipelineDesc(PipelineDesc &pd, const ShaderDesc &vs, const void *fs_code, size_t fs_size, const char *fs_entry) noexcept {
    VertexAttribute va[3] = {
        {0, PixelFormat::R16G16_FLOAT, 0, sizeof(SpriteVertex)},
        {1, PixelFormat::R16G16_FLOAT, 4, sizeof(SpriteVertex)},
        {2, PixelFormat::R8G8B8A8_UNORM, 8, sizeof(SpriteVertex)},
    };
    pd = {};
    pd.vertex_shader = vs;
    pd.fragment_shader = {ShaderStage::Fragment, fs_code, fs_size, fs_entry};
    pd.prim_type = PrimitiveType::Triangle;
    pd.cull_mode = CullMode::None;
    pd.src_blend = BlendFactor::SrcAlpha;
    pd.dst_blend = BlendFactor::OneMinusSrcAlpha;
    pd.blend_op = BlendOp::Add;
    pd.color_formats[0] = PixelFormat::B8G8R8A8_SRGB;
    pd.color_count = 1;
    for (u8 i = 0; i < 3; ++i) {
        pd.vertex_attrs[i] = va[i];
    }
    pd.vertex_attr_count = 3;
}

// Build a new pipeline first; swap only on success so a broken shader
// never blacks out the screen. Returns true when swapped.
bool RebuildPipeline(const void *fs_code, size_t fs_size, const char *fs_entry) noexcept {    auto &bk = *g_backend;
    PipelineDesc pd;
    FillPipelineDesc(pd, shader::sprite_vertex(), fs_code, fs_size, fs_entry);
    auto pr = bk.create_pipeline(pd);
    if (!pr) {
        return false;
    }
    if (g_pipe_valid) {
        bk.destroy_pipeline(g_pipeline);
    }
    g_pipeline = *pr;
    g_pipe_valid = true;
    return true;
}

// Fixed additive glow pipeline for fire embers (embedded sprite shaders,
// SrcAlpha/One blend — no hot-reload needed, the look comes from colors).
bool BuildGlowPipeline() noexcept {
    auto &bk = *g_backend;
    auto vs = shader::sprite_vertex();
    auto fs = shader::sprite_fragment();
    PipelineDesc pd;
    FillPipelineDesc(pd, vs, fs.code, fs.code_size, fs.entry);
    pd.dst_blend = BlendFactor::One;
    auto pr = bk.create_pipeline(pd);
    if (!pr) {
        return false;
    }
    g_glow_pipe = *pr;
    g_glow_valid = true;
    return true;
}

// Soft radial glow texture (white core -> transparent edge), tinted per
// ember via vertex color. 64x64 like the checker.
void BuildGlowTexture() noexcept {
    static constexpr u32 N = 64;
    static u32 pixels[N * N];
    for (u32 y = 0; y < N; ++y) {
        for (u32 x = 0; x < N; ++x) {
            f32 dx = (static_cast<f32>(x) + 0.5f - N * 0.5f) / (N * 0.5f);
            f32 dy = (static_cast<f32>(y) + 0.5f - N * 0.5f) / (N * 0.5f);
            f32 r = __builtin_sqrtf(dx * dx + dy * dy);
            f32 a = 1.0f - r;
            if (a < 0.0f) {
                a = 0.0f;
            }
            a *= a; // tighter falloff
            u32 ai = static_cast<u32>(a * 255.0f + 0.5f);
            pixels[y * N + x] = (ai << 24) | 0x00FFFFFFu;
        }
    }
    auto &bk = *g_backend;
    bk.update_texture(g_glow_tex, pixels, 0, 0, N, N, 0, 0);
}

void RespawnEmber(Ember &e) noexcept {
    f32 a = Rand01() * mm_math::MM_TWO_PI;
    f32 r = Rand01() * 30.0f;
    e.x = g_anchor_x + __builtin_cosf(a) * r;
    e.y = g_anchor_y + __builtin_sinf(a) * r;
    e.vx = (Rand01() - 0.5f) * 20.0f;
    e.vy = -40.0f - Rand01() * 40.0f;
    e.max_life = 1.0f + Rand01() * 1.5f;
    e.life = e.max_life;
    e.size = 10.0f + Rand01() * 18.0f;
    e.seed = Rand01() * mm_math::MM_TWO_PI;
}

void InitEmbers() noexcept {
    for (u16 i = 0; i < FIRE_N; ++i) {
        RespawnEmber(g_embers[i]);
        g_embers[i].life = Rand01() * g_embers[i].max_life; // stagger phases
    }
}

// Advance embers + refill the fire batch. Center fade: young = yellow-white,
// old = deep red, alpha follows remaining life.
void UpdateFire(f32 dt) noexcept {
    g_fire_batch.reset();
    for (u16 i = 0; i < FIRE_N; ++i) {
        Ember &e = g_embers[i];
        e.life -= dt;
        if (e.life <= 0.0f) {
            RespawnEmber(e);
        }
        e.x += (e.vx + __builtin_sinf(g_time * 3.0f + e.seed) * 20.0f) * dt;
        e.y += e.vy * dt;
        f32 t = e.life / e.max_life; // 1 = newborn
        u32 col;
        if (t > 0.5f) {
            col = 0xFFFFAA33u; // yellow-orange core
        } else {
            col = 0xFFFF4400u; // deep red ember
        }
        u32 alpha = static_cast<u32>(t * 255.0f);
        col = (col & 0x00FFFFFFu) | (alpha << 24);
        f32 s = e.size * (0.5f + 0.5f * t);
        g_fire_batch.add(e.x, e.y, s, s, 0.0f, col, 2);
    }
}

bool FireworkAlive() noexcept {
    if (g_fw_flash_i > 0.02f || g_fw_sky_a > 0.01f) {
        return true;
    }
    for (u16 i = 0; i < FW_ROCKETS; ++i) {
        if (g_rockets[i].active) {
            return true;
        }
    }
    for (u16 i = 0; i < FW_SPARKS; ++i) {
        if (g_sparks[i].active) {
            return true;
        }
    }
    return false;
}

void BurstRocket(Rocket &r) noexcept {
    u32 col_a = kFwPalette[static_cast<size_t>(r.hue) % 5];
    u32 col_b = kFwPalette[(static_cast<size_t>(r.hue) + 2) % 5]; // contrasting half
    u8 shape = static_cast<u8>(g_fw_shape);
    u16 want = 52;
    if (g_fw_pattern == 1) {
        want = 44; // ring: fewer, even circle
    } else if (g_fw_pattern == 2) {
        want = 30; // willow: few long drooping trails
    } else if (g_fw_pattern == 3) {
        want = 40; // palm: upward fan
    } else if (g_fw_pattern == 4) {
        want = 64; // saturn: 40 shell + 24 ellipse ring
    } else if (g_fw_pattern == 5) {
        want = 56; // f64-ring: two concentric circles
    } else if (g_fw_pattern == 6) {
        want = 46; // chrysanthemum: sphere, every spark trails
    } else if (g_fw_pattern == 7) {
        want = 8; // crossette: few big parents that split mid-flight
    } else if (g_fw_pattern == 8) {
        want = 44; // heart: aimed parametric curve
    } else if (g_fw_pattern == 9) {
        want = 54; // strobe: 36 shell + 18 blinking pistil
    }
    want = static_cast<u16>(static_cast<f32>(want) * g_fw_count_scale);
    if (want < 8) {
        want = 8;
    }
    if (want > FW_SPARKS) {
        want = FW_SPARKS;
    }
    u16 n = 0;
    for (u16 i = 0; i < FW_SPARKS && n < want; ++i) {
        Spark &s = g_sparks[i];
        if (s.active) {
            continue;
        }
        s.active = true;
        s.x = r.x;
        s.y = r.y;
        s.color = col_a;
        if (g_fw_hue_mode == 1) {
            s.color = (n < want / 2) ? col_a : col_b; // dual: half/half
        } else if (g_fw_hue_mode == 2) {
            s.color = kFwPalette[(static_cast<size_t>(n) + static_cast<size_t>(r.hue)) % 5]; // rainbow
        }
        s.shape = shape;
        s.trail = false;
        s.tick = 0;
        s.fuse = 0.0f;
        s.strobe = false;
        if (g_fw_pattern == 1) {
            // Ring: even angles, near-equal speed so the circle holds.
            f32 a = (static_cast<f32>(n) / want) * mm_math::MM_TWO_PI + (Rand01() - 0.5f) * 0.1f;
            f32 sp = 215.0f + Rand01() * 25.0f;
            s.vx = __builtin_cosf(a) * sp;
            s.vy = __builtin_sinf(a) * sp;
            s.max_life = 1.4f + Rand01() * 0.6f;
            s.size = 8.0f + Rand01() * 8.0f;
            s.grav = 90.0f;
        } else if (g_fw_pattern == 2) {
            // Willow: slow upward cone, long life, heavy gravity + trails.
            f32 a = -mm_math::MM_TWO_PI * 0.25f + (Rand01() - 0.5f) * 2.2f;
            f32 sp = 140.0f + Rand01() * 160.0f;
            s.vx = __builtin_cosf(a) * sp;
            s.vy = __builtin_sinf(a) * sp;
            s.max_life = 2.2f + Rand01() * 1.0f;
            s.size = 10.0f + Rand01() * 10.0f;
            s.grav = 420.0f;
            s.trail = true;
        } else if (g_fw_pattern == 3) {
            // Palm: fast narrow upward fan, hard arc, short life.
            f32 a = -mm_math::MM_TWO_PI * 0.25f + (Rand01() - 0.5f) * 1.2f;
            f32 sp = 260.0f + Rand01() * 180.0f;
            s.vx = __builtin_cosf(a) * sp;
            s.vy = __builtin_sinf(a) * sp;
            s.max_life = 0.9f + Rand01() * 0.6f;
            s.size = 10.0f + Rand01() * 14.0f;
            s.grav = 520.0f;
        } else if (g_fw_pattern == 4) {
            // Saturn: round shell + flat tilted ellipse ring around it.
            if (n < 40) {
                f32 a = Rand01() * mm_math::MM_TWO_PI;
                f32 sp = 150.0f + Rand01() * 150.0f;
                s.vx = __builtin_cosf(a) * sp;
                s.vy = __builtin_sinf(a) * sp;
                s.max_life = 1.4f + Rand01() * 0.8f;
                s.size = 8.0f + Rand01() * 10.0f;
                s.grav = 200.0f;
            } else {
                f32 a = (static_cast<f32>(n - 40) / 24.0f) * mm_math::MM_TWO_PI;
                s.vx = __builtin_cosf(a) * 300.0f;
                s.vy = __builtin_sinf(a) * 300.0f * 0.35f; // squashed = tilted ring
                s.max_life = 1.6f + Rand01() * 0.6f;
                s.size = 7.0f + Rand01() * 7.0f;
                s.grav = 120.0f;
            }
        } else if (g_fw_pattern == 5) {
            // Double-ring: two concentric circles (alternate fast/slow).
            f32 a = (static_cast<f32>(n) / want) * mm_math::MM_TWO_PI;
            f32 sp = (n % 2 == 0) ? 230.0f : 130.0f;
            sp += (Rand01() - 0.5f) * 10.0f;
            s.vx = __builtin_cosf(a) * sp;
            s.vy = __builtin_sinf(a) * sp;
            s.max_life = 1.5f + Rand01() * 0.6f;
            s.size = 8.0f + Rand01() * 8.0f;
            s.grav = 110.0f;
        } else if (g_fw_pattern == 6) {
            // Chrysanthemum: sphere where every spark sheds a trail.
            f32 a = Rand01() * mm_math::MM_TWO_PI;
            f32 sp = 130.0f + Rand01() * 200.0f;
            s.vx = __builtin_cosf(a) * sp;
            s.vy = __builtin_sinf(a) * sp;
            s.max_life = 1.6f + Rand01() * 1.0f;
            s.size = 9.0f + Rand01() * 11.0f;
            s.grav = 300.0f;
            s.trail = true;
        } else if (g_fw_pattern == 7) {
            // Crossette: big slow parents; each splits into 4 mid-flight.
            f32 a = Rand01() * mm_math::MM_TWO_PI;
            f32 sp = 150.0f + Rand01() * 110.0f;
            s.vx = __builtin_cosf(a) * sp;
            s.vy = __builtin_sinf(a) * sp;
            s.max_life = 2.0f;
            s.size = 16.0f + Rand01() * 6.0f;
            s.grav = 200.0f;
            s.fuse = 0.6f + Rand01() * 0.4f;
        } else if (g_fw_pattern == 8) {
            // Heart: aimed parametric curve (upright on screen, y-down).
            // x = 16sin^3(t), y = -(13cos t - 5cos 2t - 2cos 3t - cos 4t).
            f32 t = (static_cast<f32>(n) / want) * mm_math::MM_TWO_PI;
            f32 st = __builtin_sinf(t);
            f32 hx = 16.0f * st * st * st;
            f32 hy = -(13.0f * __builtin_cosf(t) - 5.0f * __builtin_cosf(2.0f * t) - 2.0f * __builtin_cosf(3.0f * t) - __builtin_cosf(4.0f * t));
            s.x = r.x + hx * 8.0f;
            s.y = r.y - 30.0f + hy * 8.0f;
            f32 dx = s.x - r.x;
            f32 dy = s.y - (r.y - 30.0f);
            f32 dl = __builtin_sqrtf(dx * dx + dy * dy) + 1e-3f;
            s.vx = dx / dl * 25.0f + (Rand01() - 0.5f) * 20.0f;
            s.vy = dy / dl * 25.0f + (Rand01() - 0.5f) * 20.0f;
            s.max_life = 1.6f + Rand01() * 0.6f;
            s.size = 8.0f + Rand01() * 6.0f;
            s.grav = 50.0f; // gentle, so the shape reads before it sags
        } else if (g_fw_pattern == 9) {
            // Strobe pistil: hue shell + white blinking core.
            if (n < 36) {
                f32 a = Rand01() * mm_math::MM_TWO_PI;
                f32 sp = 150.0f + Rand01() * 150.0f;
                s.vx = __builtin_cosf(a) * sp;
                s.vy = __builtin_sinf(a) * sp;
                s.max_life = 1.4f + Rand01() * 0.6f;
                s.size = 8.0f + Rand01() * 10.0f;
                s.grav = 220.0f;
            } else {
                f32 a = Rand01() * mm_math::MM_TWO_PI;
                f32 sp = 30.0f + Rand01() * 50.0f;
                s.vx = __builtin_cosf(a) * sp;
                s.vy = __builtin_sinf(a) * sp;
                s.max_life = 1.8f + Rand01() * 0.8f;
                s.size = 8.0f + Rand01() * 6.0f;
                s.grav = 120.0f;
                s.color = 0xFFFFFFFFu;
                s.strobe = true;
            }
        } else {
            // Sphere: uniform shell (classic peony).
            f32 a = Rand01() * mm_math::MM_TWO_PI;
            f32 sp = 120.0f + Rand01() * 220.0f;
            s.vx = __builtin_cosf(a) * sp;
            s.vy = __builtin_sinf(a) * sp;
            s.max_life = 1.2f + Rand01() * 1.0f;
            s.size = 8.0f + Rand01() * 14.0f;
            s.grav = 260.0f;
        }
        s.life = s.max_life;
        s.size *= g_fw_size_scale;
        ++n;
    }
    g_fw_flash_u = r.x / (g_view_w > 1.0f ? g_view_w : 1.0f);
    g_fw_flash_v = r.y / (g_view_h > 1.0f ? g_view_h : 1.0f);
    g_fw_flash_i = 1.0f;
    r.active = false;
}

bool SpawnSpark(f32 x, f32 y, f32 vx, f32 vy, f32 life, f32 size, u32 color, u8 shape, f32 grav,
                bool trail, f32 fuse, bool strobe) noexcept {
    for (u16 i = 0; i < FW_SPARKS; ++i) {
        Spark &s = g_sparks[i];
        if (s.active) {
            continue;
        }
        s.active = true;
        s.x = x;
        s.y = y;
        s.vx = vx;
        s.vy = vy;
        s.max_life = life;
        s.life = life;
        s.size = size;
        s.color = color;
        s.shape = shape;
        s.grav = grav;
        s.trail = trail;
        s.tick = 0;
        s.fuse = fuse;
        s.strobe = strobe;
        return true;
    }
    return false;
}

void SpawnTrail(f32 x, f32 y) noexcept {
    (void)SpawnSpark(x, y, (Rand01() - 0.5f) * 30.0f, 60.0f + Rand01() * 60.0f, 0.25f + Rand01() * 0.2f, 6.0f + Rand01() * 8.0f,
                     0xFFFFDD88u, 0, 260.0f, false, 0.0f, false);
}

// Crossette split: parent dies into 4 children at 90° offsets, keeping the
// parent's heading as reference. Children never re-split (fuse = 0).
void SplitSpark(const Spark &p) noexcept {
    f32 base = __builtin_atan2f(p.vy, p.vx);
    f32 plen = __builtin_sqrtf(p.vx * p.vx + p.vy * p.vy);
    f32 sp = plen * 0.6f + 50.0f;
    for (int k = 0; k < 4; ++k) {
        f32 a = base + static_cast<f32>(k) * (mm_math::MM_TWO_PI * 0.25f) + 0.785f;
        (void)SpawnSpark(p.x, p.y, __builtin_cosf(a) * sp, __builtin_sinf(a) * sp, 0.7f + Rand01() * 0.3f, p.size * 0.6f, p.color, p.shape,
                         200.0f, false, 0.0f, false);
    }
}

// Procedural spark shapes (64x64 white, tinted per-spark via vertex color):
// 0 blob (soft radial), 1 sparkle4 (4-point star: core + thin axis rays),
// 2 ring (soft annulus), 3 cross (thick plus, no core).
void BuildSparkTextures() noexcept {
    static constexpr u32 N = 64;
    static u32 pixels[N * N];
    auto &bk = *g_backend;
    for (u16 sh = 0; sh < FW_SHAPES; ++sh) {
        for (u32 y = 0; y < N; ++y) {
            for (u32 x = 0; x < N; ++x) {
                f32 dx = (static_cast<f32>(x) + 0.5f - N * 0.5f) / (N * 0.5f);
                f32 dy = (static_cast<f32>(y) + 0.5f - N * 0.5f) / (N * 0.5f);
                f32 r = __builtin_sqrtf(dx * dx + dy * dy);
                f32 ax = __builtin_fabsf(dx) < __builtin_fabsf(dy) ? __builtin_fabsf(dx) : __builtin_fabsf(dy); // dist to nearer axis
                f32 a = 0.0f;
                if (sh == 0) {
                    a = 1.0f - r;
                    if (a < 0.0f) {
                        a = 0.0f;
                    }
                    a *= a;
                } else if (sh == 1) {
                    f32 core = 1.0f - r;
                    if (core < 0.0f) {
                        core = 0.0f;
                    }
                    core *= core;
                    f32 ray = 1.0f - ax * 4.0f;
                    if (ray < 0.0f) {
                        ray = 0.0f;
                    }
                    ray = ray * ray * ray;
                    f32 diag = (1.0f - r * 2.0f) * 0.3f;
                    if (diag < 0.0f) {
                        diag = 0.0f;
                    }
                    a = core * 0.7f + ray * 0.9f + diag;
                    if (a > 1.0f) {
                        a = 1.0f;
                    }
                } else if (sh == 2) {
                    f32 d = __builtin_fabsf(r - 0.55f);
                    a = 1.0f - d / 0.16f;
                    if (a < 0.0f) {
                        a = 0.0f;
                    }
                    a *= a;
                    if (r > 1.0f) {
                        a = 0.0f;
                    }
                } else {
                    f32 arm = 1.0f - ax * 2.2f;
                    if (arm < 0.0f) {
                        arm = 0.0f;
                    }
                    f32 m = 1.0f - r;
                    if (m < 0.0f) {
                        m = 0.0f;
                    }
                    a = arm * m;
                    a *= a;
                }
                u32 ai = static_cast<u32>(a * 255.0f + 0.5f);
                pixels[y * N + x] = (ai << 24) | 0x00FFFFFFu;
            }
        }
        bk.update_texture(g_spark_tex[sh], pixels, 0, 0, N, N, 0, 0);
    }
}

f32 FwTuneValue(u16 sel) noexcept {
    if (sel == 1) {
        return g_fw_size_scale;
    }
    if (sel == 2) {
        return g_fw_count_scale;
    }
    return g_fw_grav;
}

void FwTuneAdjust(int dir) noexcept {
    f32 step = (dir > 0) ? 1.0f : -1.0f;
    if (g_fw_tune_sel == 0) {
        g_fw_grav += step * 20.0f;
        if (g_fw_grav < 60.0f) {
            g_fw_grav = 60.0f;
        }
        if (g_fw_grav > 600.0f) {
            g_fw_grav = 600.0f;
        }
    } else if (g_fw_tune_sel == 1) {
        g_fw_size_scale += step * 0.1f;
        if (g_fw_size_scale < 0.5f) {
            g_fw_size_scale = 0.5f;
        }
        if (g_fw_size_scale > 2.0f) {
            g_fw_size_scale = 2.0f;
        }
    } else if (g_fw_tune_sel == 2) {
        g_fw_count_scale += step * 0.1f;
        if (g_fw_count_scale < 0.5f) {
            g_fw_count_scale = 0.5f;
        }
        if (g_fw_count_scale > 2.0f) {
            g_fw_count_scale = 2.0f;
        }
    }
    printf("[lab] tune %s=%.2f\n", kFwTuneNames[g_fw_tune_sel], FwTuneValue(g_fw_tune_sel));
}

void LaunchFirework(bool quiet) noexcept {
    for (u16 i = 0; i < FW_ROCKETS; ++i) {
        Rocket &r = g_rockets[i];
        if (r.active) {
            continue;
        }
        r.active = true;
        r.x = g_view_w * g_fw_aim_x + (Rand01() - 0.5f) * g_view_w * 0.06f;
        if (r.x < 8.0f) {
            r.x = 8.0f;
        }
        if (r.x > g_view_w - 8.0f) {
            r.x = g_view_w - 8.0f;
        }
        r.y = g_view_h + 12.0f;
        r.target_y = g_view_h * g_fw_apex;
        // Power from aim: velocity that coasts exactly to the apex.
        r.vy = -__builtin_sqrtf(2.0f * g_fw_grav * (r.y - r.target_y));
        if (r.vy > -300.0f) {
            r.vy = -300.0f;
        }
        r.hue = static_cast<f32>(g_fw_hue % 5);
        if (!quiet) {
            printf("[lab] firework launched (hue=%u)\n", g_fw_hue % 5);
        }
        ++g_fw_hue;
        return;
    }
    if (!quiet) {
        printf("[lab] firework busy (all %u rockets aloft)\n", FW_ROCKETS);
    }
}

// Rockets climb + trail; sparks fall with gravity + drag. The batch is
// refilled every frame (even at dt=0) so pause freezes the show in place.
void UpdateFirework(f32 dt) noexcept {
    for (u16 sh = 0; sh < FW_SHAPES; ++sh) {
        g_fw_batch[sh].reset();
    }
    g_fw_flash_i *= std::expf(-3.0f * dt);
    if (g_fw_flash_i < 0.01f) {
        g_fw_flash_i = 0.0f;
    }
    for (u16 i = 0; i < FW_ROCKETS; ++i) {
        Rocket &r = g_rockets[i];
        if (!r.active) {
            continue;
        }
        r.vy += g_fw_grav * dt;
        r.y += r.vy * dt;
        SpawnTrail(r.x, r.y);
        g_fw_batch[0].add(r.x, r.y, 14.0f, 14.0f, 0.0f, 0xFFFFFFFFu, 2);
        if (r.vy >= -40.0f || r.y <= r.target_y) {
            BurstRocket(r);
        }
    }
    f32 drag = std::expf(-1.2f * dt);
    f32 gscale = g_fw_grav / 260.0f; // tune gravity scales pattern gravities
    for (u16 i = 0; i < FW_SPARKS; ++i) {
        Spark &s = g_sparks[i];
        if (!s.active) {
            continue;
        }
        s.life -= dt;
        if (s.life <= 0.0f) {
            s.active = false;
            continue;
        }
        if (s.fuse > 0.0f) {
            s.fuse -= dt;
            if (s.fuse <= 0.0f) {
                SplitSpark(s); // crossette: parent dies into 4 children
                s.active = false;
                continue;
            }
        }
        s.vy += s.grav * gscale * dt;
        s.vx *= drag;
        s.vy *= drag;
        s.x += s.vx * dt;
        s.y += s.vy * dt;
        if (s.trail && ((s.tick++ & 7) == 0)) {
            // Willow droop: shed a short fading puff at quarter velocity.
            (void)SpawnSpark(s.x, s.y, s.vx * 0.1f, s.vy * 0.1f, 0.4f, s.size * 0.55f, s.color, 0, 60.0f, false, 0.0f,
                             false);
        }
        f32 t = s.life / s.max_life; // 1 = newborn
        f32 fade = t;
        if (s.strobe) {
            // Strobe pistil: hard blink at ~7.5Hz (tick advances per frame).
            fade *= (((s.tick++ >> 2) & 1) != 0) ? 1.0f : 0.12f;
        }
        u32 alpha = static_cast<u32>(fade * 255.0f);
        u32 col = (s.color & 0x00FFFFFFu) | (alpha << 24);
        f32 sz = s.size * (0.5f + 0.5f * t);
        g_fw_batch[s.shape % FW_SHAPES].add(s.x, s.y, sz, sz, 0.0f, col, 2);
    }
    // Sky alpha chases show activity (rockets/sparks/flash only — NOT
    // itself, or it would latch on forever).
    bool show = g_fw_flash_i > 0.02f;
    if (!show) {
        for (u16 i = 0; i < FW_ROCKETS && !show; ++i) {
            show = g_rockets[i].active;
        }
    }
    if (!show) {
        for (u16 i = 0; i < FW_SPARKS && !show; ++i) {
            show = g_sparks[i].active;
        }
    }
    f32 target = show ? 1.0f : 0.0f;
    f32 rate = 2.5f * dt;
    if (g_fw_sky_a < target) {
        g_fw_sky_a += rate;
        if (g_fw_sky_a > target) {
            g_fw_sky_a = target;
        }
    } else {
        g_fw_sky_a -= rate;
        if (g_fw_sky_a < target) {
            g_fw_sky_a = target;
        }
    }
}

void LayoutFirework() noexcept {
    g_fw_sky_batch.reset();
    g_fw_sky_batch.add(g_view_w * 0.5f, g_view_h * 0.5f, g_view_w, g_view_h, 0.0f, 0xFFFFFFFFu, 0);
}

struct GenericParams; // defined below with WriteParams
void WriteFireworkParams(GenericParams &p) noexcept;

#if defined(USE_METAL_BACKEND)
// Build the sky pipeline from the hot-reload file; swap only on success.
bool BuildFireworkPipeline() noexcept {
    auto &bk = *g_backend;
    PipelineDesc pd;
    FillPipelineDesc(pd, shader::sprite_vertex(), g_fw_file.code, g_fw_file.size, "lab_firework_main");
    auto pr = bk.create_pipeline(pd);
    if (!pr) {
        return false;
    }
    if (g_fw_valid) {
        bk.destroy_pipeline(g_fw_pipe);
    }
    g_fw_pipe = *pr;
    g_fw_valid = true;
    return true;
}
#endif

bool SelectEmbedded(LabMode m) noexcept {
    switch (m) {
    case LabMode::Sprite: {
        auto fs = shader::sprite_fragment();
        return RebuildPipeline(fs.code, fs.code_size, fs.entry);
    }
    case LabMode::Grayscale: {
        auto fs = shader::grayscale_fragment();
        return RebuildPipeline(fs.code, fs.code_size, fs.entry);
    }
    case LabMode::Dissolve: {
        auto fs = shader::dissolve_fragment();
        return RebuildPipeline(fs.code, fs.code_size, fs.entry);
    }
    case LabMode::Outline: {
        auto fs = shader::sprite_outline_fragment();
        return RebuildPipeline(fs.code, fs.code_size, fs.entry);
    }
    case LabMode::ColorGrade: {
        auto fs = shader::color_grade_fragment();
        return RebuildPipeline(fs.code, fs.code_size, fs.entry);
    }
    default: return false;
    }
}

void SetMode(LabMode m) noexcept {
    if (m == LabMode::Firework) {
        // Dedicated show page: no main-pipeline quad (LayoutQuads clears the
        // batch → dark night background), rockets auto-launch on interval.
        g_mode = m;
        LayoutQuads();
        g_fw_auto = true;
        g_fw_auto_t = 0.0f;
        printf("[lab] mode=%s every %.0fs (V cycles 1/2/3, W launches now)\n", ModeName(g_mode), kFwIntervals[g_fw_interval_sel]);
        return;
    }
    int slot = LabSlotForMode(m);
    if (slot >= 0) {
#if !defined(USE_METAL_BACKEND)
        printf("[lab] lab-file mode is Metal-only; staying on %s\n", ModeName(g_mode));
        return;
#else
        LabFile &f = g_lab_files[static_cast<size_t>(slot)];
        if (!f.found) {
            printf("[lab] %s not found; staying on %s\n", kLabSlots[static_cast<size_t>(slot)].filename, ModeName(g_mode));
            return;
        }
        if (!ReadLabFile(f) || !RebuildPipeline(f.code, f.size, kLabSlots[static_cast<size_t>(slot)].entry)) {
            printf("[lab] lab file load/compile failed; staying on %s\n", ModeName(g_mode));
            return;
        }
#endif
    } else {
        if (!SelectEmbedded(m)) {
            printf("[lab] pipeline build failed for %s; keeping %s\n", ModeName(m), ModeName(g_mode));
            return;
        }
    }
    g_mode = m;
    LayoutQuads(); // D9 uses a single card quad, others use the 3x2 grid
    g_fw_auto = (m == LabMode::Firework); // auto-show lives on the show page only
    if (g_fw_auto) {
        g_fw_auto_t = 0.0f;
    }
    if (m == LabMode::NormalPng) {
        LoadPngNormalTexture(); // (re)load texture for this visit
    }
    if (m == LabMode::CardNormal || m == LabMode::CardPlastic) {
        // D9 reuses the lab_png pipeline, P has its own; both need
        // card albedo + card normal.
        if (!LoadCardAlbedo() || !LoadCardNormal()) {
            printf("[lab] card assets failed; staying on %s\n", ModeName(g_mode));
            return;
        }
    }
    if (m == LabMode::CardCartoon || m == LabMode::CardGlow || m == LabMode::CardGold || m == LabMode::CardStay || m == LabMode::CardSun || m == LabMode::CardAura) {
        // Own pipeline (lab_cartoon / lab_glow / lab_gold / lab_stay slot);
        // needs card albedo only.
        if (!LoadCardAlbedo()) {
            printf("[lab] card albedo failed; staying on %s\n", ModeName(g_mode));
            return;
        }
    }
    if (IsNormalMode(g_mode)) {
        printf("[lab] mode=%s az=%.2f el=%.2f (arrows move light)\n", ModeName(g_mode), g_light_az, g_light_el);
    } else if (g_mode == LabMode::CardGlow) {
        printf("[lab] mode=%s dur=%.2fs expand=%.3f intensity=%.2f (arrows=dur/expand, +-=intensity)\n", ModeName(g_mode), g_glow_dur,
               g_glow_expand, g_glow_intensity);
    } else if (g_mode == LabMode::CardGold) {
        printf("[lab] mode=%s speed=%.2fHz width=%.3f intensity=%.2f (arrows=speed/width, +-=intensity)\n", ModeName(g_mode), g_gold_speed,
               g_gold_width, g_gold_intensity);
    } else if (g_mode == LabMode::CardStay) {
        printf("[lab] mode=%s speed=%.2fHz width=%.3f intensity=%.2f (arrows=speed/width, +-=intensity)\n", ModeName(g_mode), g_stay_speed,
               g_stay_width, g_stay_intensity);
    } else if (g_mode == LabMode::CardSun) {
        printf("[lab] mode=%s speed=%.3fHz rays=%.0f intensity=%.2f (arrows=speed/rays, +-=intensity)\n", ModeName(g_mode), g_sun_speed,
               g_sun_rays, g_sun_intensity);
    } else if (g_mode == LabMode::CardAura) {
        printf("[lab] mode=%s speed=%.2fHz spikes=%.0f intensity=%.2f (arrows=speed/spikes, +-=intensity)\n", ModeName(g_mode), g_aura_speed,
               g_aura_spikes, g_aura_intensity);
    } else {
        printf("[lab] mode=%s progress=%.2f edge=%.3f\n", ModeName(g_mode), g_progress, g_edge_width);
    }
}

// 64-byte generic params block. MSL packing rule: a float4/float3 member
// starts at a 16-byte boundary, so LabParams/DissolveParams on the GPU read
// {progress, edge_width, _pad, _pad, rgba[4]} — NOT a packed rgba at [2..5].
// (Packing rgba at [2..5] shows a green glow: the GPU actually samples
// [4..7] as the color.) Same padding applies to ColorGrade's float3x3
// (each column is float3 + 4-byte pad).
struct GenericParams {
    f32 v[16];
};

void WriteFireworkParams(GenericParams &p) noexcept {
    for (f32 &f : p.v) {
        f = 0.0f;
    }
    // LabFireworkParams: timing(time, twinkle, spare, spare),
    // flash(u, v, intensity, radius), misc(aspect, stars, vignette, sky_a).
    p.v[0] = g_time;
    p.v[1] = 3.0f;
    p.v[4] = g_fw_flash_u;
    p.v[5] = g_fw_flash_v;
    p.v[6] = g_fw_flash_i;
    p.v[7] = 0.18f;
    p.v[8] = g_view_w / (g_view_h > 1.0f ? g_view_h : 1.0f);
    p.v[9] = 1.0f;
    p.v[10] = 0.5f;
    p.v[11] = g_fw_sky_a;
}

void WriteParams(GenericParams &p) noexcept {
    for (f32 &f : p.v) {
        f = 0.0f;
    }
    switch (g_mode) {
    case LabMode::Dissolve:
    case LabMode::LabFile:
        // LabParams/DissolveParams: progress, edge_width, _pad[2], edge_color(rgba)
        p.v[0] = g_progress;
        p.v[1] = g_edge_width;
        p.v[2] = 0.0f;
        p.v[3] = 0.0f;
        p.v[4] = 1.0f; // orange glow
        p.v[5] = 0.45f;
        p.v[6] = 0.05f;
        p.v[7] = 1.0f;
        break;
    case LabMode::Grayscale:
        // GrayscaleParams: amount, pad[3]
        p.v[0] = g_progress;
        break;
    case LabMode::Outline:
        // OutlineParams: color[4], thickness, pad[3]
        p.v[0] = 1.0f;
        p.v[1] = 0.35f;
        p.v[2] = 0.0f;
        p.v[3] = 1.0f;
        p.v[4] = 0.002f + g_progress * 0.02f;
        break;
    case LabMode::ColorGrade:
        // ColorGrade: brightness, contrast, saturation, hue 3x3 column-major
        // (each float3 column padded to 16 bytes: [4..6], [8..10], [12..14])
        p.v[0] = 1.0f;
        p.v[1] = 1.0f;
        p.v[2] = 1.0f - g_progress;
        p.v[3] = 0.0f;
        p.v[4] = 1.0f;
        p.v[5] = 0.0f;
        p.v[6] = 0.0f;
        p.v[7] = 0.0f;
        p.v[8] = 0.0f;
        p.v[9] = 1.0f;
        p.v[10] = 0.0f;
        p.v[11] = 0.0f;
        p.v[12] = 0.0f;
        p.v[13] = 0.0f;
        p.v[14] = 1.0f;
        p.v[15] = 0.0f;
        break;
    default:
        if (IsNormalMode(g_mode)) {
            // LabNormalParams: light_dir(xyz+w), misc(ambient, spec, hscale, spare)
            if (FireActive()) {
                // Light comes from the fire anchor (screen points) toward the
                // card center, lifted by g_light_h — tangent-space approx.
                f32 dx = g_card_cx - g_anchor_x;
                f32 dy = g_card_cy - g_anchor_y;
                f32 dz = g_light_h;
                f32 inv = 1.0f / __builtin_sqrtf(dx * dx + dy * dy + dz * dz);
                p.v[0] = dx * inv;
                p.v[1] = dy * inv;
                p.v[2] = dz * inv;
            } else {
                f32 ce = __builtin_cosf(g_light_el);
                p.v[0] = ce * __builtin_cosf(g_light_az);
                p.v[1] = ce * __builtin_sinf(g_light_az);
                p.v[2] = __builtin_sinf(g_light_el);
            }
            p.v[3] = 1.0f; // intensity
            p.v[4] = 0.25f; // ambient
            p.v[5] = 0.5f; // spec strength
            p.v[6] = 2.0f; // height_scale (derive mode only)
            p.v[7] = 0.0f;
            if (g_mode == LabMode::CardCartoon) {
                // LabCartoonParams.toon: bands, ink threshold, ink strength, spare
                p.v[8] = g_cartoon_bands;
                p.v[9] = 0.35f;
                p.v[10] = 0.85f;
                p.v[11] = 0.0f;
            }
            if (g_mode == LabMode::CardPlastic) {
                // LabPlasticParams.plastic: clearcoat, fresnel, wrap, shininess
                p.v[8] = 0.6f;
                p.v[9] = 0.5f;
                p.v[10] = 0.4f;
                p.v[11] = 120.0f;
            }
        }
        break;
    case LabMode::CardGlow: {
        // LabGlowParams: timing(time, duration, expand, ring_w),
        // glow(rgba teal), misc(aspect, intensity, quad_k, spare).
        // (float4 members ONLY — Metal 16-byte packing.)
        p.v[0] = g_time;
        p.v[1] = g_glow_dur;
        p.v[2] = g_glow_expand;
        p.v[3] = 0.025f; // ring thickness (edit shader file for finer tune)
        p.v[4] = 0.0f;   // teal K_TEAL 0xFF00CED1
        p.v[5] = 0.8078f;
        p.v[6] = 0.8196f;
        p.v[7] = 0.9f;
        p.v[8] = 500.0f / 726.0f; // lab_card.png aspect
        p.v[9] = g_glow_intensity;
        p.v[10] = GLOW_QUAD_K; // quad scale (uv remap in shader)
        p.v[11] = 0.0f;
        break;
    }
    case LabMode::CardGold: {
        // LabGoldParams: timing(time, speed_hz, band_width, spare),
        // glow(rgba gold), misc(aspect, intensity, quad_k, spare).
        p.v[0] = g_time;
        p.v[1] = g_gold_speed;
        p.v[2] = g_gold_width;
        p.v[3] = 0.0f;
        p.v[4] = 1.0f; // gold
        p.v[5] = 0.78f;
        p.v[6] = 0.25f;
        p.v[7] = 0.95f;
        p.v[8] = 500.0f / 726.0f; // lab_card.png aspect
        p.v[9] = g_gold_intensity;
        p.v[10] = GLOW_QUAD_K;
        p.v[11] = 0.0f;
        break;
    }
    case LabMode::CardStay: {
        // LabStayParams: timing(time, speed_hz, band_width, spare),
        // glow(rgba gold), misc(aspect, intensity, quad_k, spare).
        p.v[0] = g_time;
        p.v[1] = g_stay_speed;
        p.v[2] = g_stay_width;
        p.v[3] = 0.0f;
        p.v[4] = 1.0f; // gold
        p.v[5] = 0.78f;
        p.v[6] = 0.25f;
        p.v[7] = 0.95f;
        p.v[8] = 500.0f / 726.0f; // lab_card.png aspect
        p.v[9] = g_stay_intensity;
        p.v[10] = GLOW_QUAD_K;
        p.v[11] = 0.0f;
        break;
    }
    case LabMode::CardSun: {
        // LabSunParams: timing(time, speed_hz, ray_count, sharpness),
        // color(rgba gold), misc(aspect, intensity, quad_k, fade).
        p.v[0] = g_time;
        p.v[1] = g_sun_speed;
        p.v[2] = g_sun_rays;
        p.v[3] = 3.0f; // ray sharpness (edit shader file for finer tune)
        p.v[4] = 1.0f; // warm gold
        p.v[5] = 0.78f;
        p.v[6] = 0.25f;
        p.v[7] = 0.95f;
        p.v[8] = 500.0f / 726.0f; // lab_card.png aspect
        p.v[9] = g_sun_intensity;
        p.v[10] = GLOW_QUAD_K;
        p.v[11] = 6.0f; // radial fade power
        break;
    }
    case LabMode::CardAura: {
        // LabAuraParams: timing(time, breath_hz, density, spike_len),
        // color(rgba warm white), misc(aspect, intensity, quad_k, frame_w),
        // tune2(filament_opacity, spare, spare, spare).
        p.v[0] = g_time;
        p.v[1] = g_aura_speed;
        p.v[2] = g_aura_spikes;
        p.v[3] = 0.30f; // filament length, card-uv (edit shader for finer tune)
        p.v[4] = 1.0f;  // warm white-yellow
        p.v[5] = 0.95f;
        p.v[6] = 0.80f;
        p.v[7] = 1.0f;
        p.v[8] = 500.0f / 726.0f; // lab_card.png aspect
        p.v[9] = g_aura_intensity;
        p.v[10] = GLOW_QUAD_K;
        p.v[11] = 0.02f; // frame width, card-uv
        p.v[12] = g_aura_opacity;
        p.v[13] = 0.0f;
        p.v[14] = 0.0f;
        p.v[15] = 0.0f;
        break;
    }
    }
}

void BuildCheckerTexture() noexcept {
    static constexpr u32 N = 64;
    static u32 pixels[N * N];
    for (u32 y = 0; y < N; ++y) {
        for (u32 x = 0; x < N; ++x) {
            bool even = ((x / 8) + (y / 8)) % 2 == 0;
            pixels[y * N + x] = even ? 0xFFFFFFFFu : 0xFFB0B0B0u;
        }
    }
    auto &bk = *g_backend;
    bk.update_texture(g_texture, pixels, 0, 0, N, N, 0, 0);
}

// (Re)create a texture at w*h (UNORM or sRGB). Destroys the old one first
// so reloads with a different image size stay correct.
bool EnsureTexture(TextureHandle &tex, u16 &tw, u16 &th, bool &valid, u16 w, u16 h, PixelFormat fmt) noexcept {
    auto &bk = *g_backend;
    if (valid) {
        bk.destroy_texture(tex);
        valid = false;
    }
    TextureDesc td = {TextureType::Tex2D, fmt, w, h, 1, 1, 1};
    auto tr = bk.create_texture(td);
    if (!tr) {
        return false;
    }
    tex = *tr;
    tw = w;
    th = h;
    valid = true;
    return true;
}

bool CreateNormalTexture(u16 w, u16 h) noexcept {
    return EnsureTexture(g_normal_tex, g_normal_w, g_normal_h, g_normal_valid, w, h, PixelFormat::R8G8B8A8_UNORM);
}

// Locate + read + stb-decode a PNG from the lab dir. Caller owns *out_px
// (stbi_image_free) on success. Records path/mtime when requested.
bool DecodeLabPng(const char *filename, int *out_w, int *out_h, unsigned char **out_px, char *out_path, size_t path_cap,
                  long *out_mtime) noexcept {
    static unsigned char file_buf[PNG_MAX];
    char found[256];
    long mt = 0;
    if (!LocateInLabDir(filename, found, sizeof(found), &mt)) {
        return false;
    }
    FILE *fp = fopen(found, "rb");
    if (!fp) {
        return false;
    }
    struct stat st;
    size_t fsize = 0;
    if (stat(found, &st) == 0 && st.st_size > 0 && static_cast<size_t>(st.st_size) <= PNG_MAX) {
        fsize = fread(file_buf, 1, static_cast<size_t>(st.st_size), fp);
    }
    fclose(fp);
    if (fsize == 0) {
        return false;
    }
    int w = 0, h = 0, ch = 0;
    unsigned char *px = stbi_load_from_memory(file_buf, static_cast<int>(fsize), &w, &h, &ch, 4);
    if (!px || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        if (px) {
            stbi_image_free(px);
        }
        return false;
    }
    *out_w = w;
    *out_h = h;
    *out_px = px;
    if (out_path && path_cap > 0) {
        size_t n = strlen(found);
        if (n + 1 <= path_cap) {
            memcpy(out_path, found, n + 1);
        }
    }
    if (out_mtime) {
        *out_mtime = mt;
    }
    return true;
}

void UploadFlatNormal() noexcept {
    static u32 flat[NRM_SIZE * NRM_SIZE];
    for (u32 i = 0; i < NRM_SIZE * NRM_SIZE; ++i) {
        flat[i] = 0xFFFF8080u; // RGBA (128,128,255,255) = +z, ABGR byte order
    }
    if ((!g_normal_valid || g_normal_w != NRM_SIZE || g_normal_h != NRM_SIZE) && !CreateNormalTexture(NRM_SIZE, NRM_SIZE)) {
        return;
    }
    auto &bk = *g_backend;
    (void)bk.update_texture(g_normal_tex, flat, 0, 0, g_normal_w, g_normal_h, 0, 0);
}

// D7: sine-heightfield normal map generated on the CPU (smooth bumps).
void BuildProceduralNormalTexture() noexcept {
    static f32 height[NRM_SIZE * NRM_SIZE];
    static u32 px[NRM_SIZE * NRM_SIZE];
    for (u16 y = 0; y < NRM_SIZE; ++y) {
        for (u16 x = 0; x < NRM_SIZE; ++x) {
            f32 u = static_cast<f32>(x) / NRM_SIZE;
            f32 v = static_cast<f32>(y) / NRM_SIZE;
            height[y * NRM_SIZE + x] = __builtin_sinf(u * mm_math::MM_TWO_PI * 3.0f) * __builtin_sinf(v * mm_math::MM_TWO_PI * 3.0f);
        }
    }
    auto h_at = [&](int x, int y) noexcept -> f32 {
        x = (x + NRM_SIZE) % NRM_SIZE;
        y = (y + NRM_SIZE) % NRM_SIZE;
        return height[static_cast<u32>(y) * NRM_SIZE + static_cast<u32>(x)];
    };
    static constexpr f32 kStrength = 2.0f;
    for (u16 y = 0; y < NRM_SIZE; ++y) {
        for (u16 x = 0; x < NRM_SIZE; ++x) {
            f32 dhdx = (h_at(x + 1, y) - h_at(x - 1, y)) * 0.5f;
            f32 dhdy = (h_at(x, y + 1) - h_at(x, y - 1)) * 0.5f;
            f32 nx = -dhdx * kStrength;
            f32 ny = -dhdy * kStrength;
            f32 nz = 1.0f;
            f32 inv = 1.0f / __builtin_sqrtf(nx * nx + ny * ny + nz * nz);
            auto enc = [&](f32 f) noexcept -> u32 {
                int q = static_cast<int>((f * 0.5f + 0.5f) * 255.0f + 0.5f);
                if (q < 0) {
                    q = 0;
                }
                if (q > 255) {
                    q = 255;
                }
                return static_cast<u32>(q);
            };
            u32 r = enc(nx * inv);
            u32 g = enc(ny * inv);
            u32 b = enc(nz * inv);
            px[y * NRM_SIZE + x] = (0xFFu << 24) | (b << 16) | (g << 8) | r;
        }
    }
    if ((!g_normal_valid || g_normal_w != NRM_SIZE || g_normal_h != NRM_SIZE) && !CreateNormalTexture(NRM_SIZE, NRM_SIZE)) {
        return;
    }
    auto &bk = *g_backend;
    (void)bk.update_texture(g_normal_tex, px, 0, 0, g_normal_w, g_normal_h, 0, 0);
}

// D8: load lab_normal.png from disk (linear UNORM data). Missing/unreadable
// file falls back to a flat normal so the mode still renders.
bool LoadPngNormalTexture() noexcept {
    int w = 0, h = 0;
    unsigned char *px = nullptr;
    if (!DecodeLabPng("lab_normal.png", &w, &h, &px, g_png_path, sizeof(g_png_path), &g_png_mtime)) {
        printf("[lab] lab_normal.png not found/unreadable — flat normal fallback\n");
        UploadFlatNormal();
        g_png_found = false;
        return true;
    }
    g_png_found = true;
    if (!g_normal_valid || g_normal_w != static_cast<u16>(w) || g_normal_h != static_cast<u16>(h)) {
        if (!CreateNormalTexture(static_cast<u16>(w), static_cast<u16>(h))) {
            stbi_image_free(px);
            return false;
        }
    }
    auto &bk = *g_backend;
    (void)bk.update_texture(g_normal_tex, px, 0, 0, static_cast<u32>(w), static_cast<u32>(h), 0, 0);
    stbi_image_free(px);
    printf("[lab] normal png: %s (%dx%d)\n", g_png_path, w, h);
    return true;
}

// D9 card albedo (sRGB — card art must not go through UNORM).
static TextureHandle g_card_tex;
static u16 g_card_w = 0;
static u16 g_card_h = 0;
static bool g_card_valid = false;
static char g_card_nrm_path[256]{};
static long g_card_nrm_mtime = 0;
static bool g_card_nrm_found = false;

bool LoadCardAlbedo() noexcept {
    int w = 0, h = 0;
    unsigned char *px = nullptr;
    if (!DecodeLabPng("lab_card.png", &w, &h, &px, nullptr, 0, nullptr)) {
        printf("[lab] lab_card.png not found/unreadable\n");
        return false;
    }
    bool ok = EnsureTexture(g_card_tex, g_card_w, g_card_h, g_card_valid, static_cast<u16>(w), static_cast<u16>(h),
                            PixelFormat::R8G8B8A8_SRGB);
    if (ok) {
        auto &bk = *g_backend;
        (void)bk.update_texture(g_card_tex, px, 0, 0, static_cast<u32>(w), static_cast<u32>(h), 0, 0);
        printf("[lab] card albedo: (%dx%d sRGB)\n", w, h);
    }
    stbi_image_free(px);
    return ok;
}

// D9 card normal map (UNORM linear). Falls back to flat like D8.
bool LoadCardNormal() noexcept {
    int w = 0, h = 0;
    unsigned char *px = nullptr;
    if (!DecodeLabPng("lab_card_normal.png", &w, &h, &px, g_card_nrm_path, sizeof(g_card_nrm_path), &g_card_nrm_mtime)) {
        printf("[lab] lab_card_normal.png not found/unreadable — flat normal fallback\n");
        UploadFlatNormal();
        g_card_nrm_found = false;
        return true;
    }
    g_card_nrm_found = true;
    if (!g_normal_valid || g_normal_w != static_cast<u16>(w) || g_normal_h != static_cast<u16>(h)) {
        if (!CreateNormalTexture(static_cast<u16>(w), static_cast<u16>(h))) {
            stbi_image_free(px);
            return false;
        }
    }
    auto &bk = *g_backend;
    (void)bk.update_texture(g_normal_tex, px, 0, 0, static_cast<u32>(w), static_cast<u32>(h), 0, 0);
    stbi_image_free(px);
    printf("[lab] card normal: %s (%dx%d)\n", g_card_nrm_path, w, h);
    return true;
}

void LayoutQuads() noexcept {
    static const u32 kTints[6] = {0xFFFF5555u, 0xFF55FF55u, 0xFF5555FFu, 0xFFFFFF55u, 0xFFFF55FFu, 0xFFFFFFFFu};
    g_batch.reset();
    if (g_mode == LabMode::Firework) {
        return; // show page: empty batch → dark night background, sky + sparks only
    }
    if (g_mode == LabMode::CardGlow || g_mode == LabMode::CardGold || g_mode == LabMode::CardStay || g_mode == LabMode::CardSun || g_mode == LabMode::CardAura) {
        // Glow/gold quad is GLOW_QUAD_K× the card (margin for the outer
        // ring/band — a card-sized quad would clip it). The shader remaps uv
        // back to card space via misc.z, so the card art renders at quad / k.
        // Size the QUAD to the view (same footprint as the D9 card), so the
        // whole effect stays on-screen.
        f32 quad_h = g_view_h - 80.0f;
        if (quad_h < 200.0f) {
            quad_h = 200.0f;
        }
        f32 quad_w = quad_h * 500.0f / 726.0f;
        g_card_cx = g_view_w * 0.5f;
        g_card_cy = g_view_h * 0.5f;
        g_batch.add(g_card_cx, g_card_cy, quad_w, quad_h, 0.0f, 0xFFFFFFFFu, 1);
        return;
    }
    if (g_mode == LabMode::CardNormal || g_mode == LabMode::CardCartoon || g_mode == LabMode::CardPlastic) {
        // One big card, white tint (tint multiplies albedo — colors must stay true).
        // Aspect from lab_card.png (500x726); fit height with margin.
        f32 qh = g_view_h - 80.0f;
        if (qh < 200.0f) {
            qh = 200.0f;
        }
        f32 qw = qh * 500.0f / 726.0f;
        g_card_cx = g_view_w * 0.5f;
        g_card_cy = g_view_h * 0.5f;
        g_batch.add(g_card_cx, g_card_cy, qw, qh, 0.0f, 0xFFFFFFFFu, 1);
        return;
    }
    f32 qw = 260.0f, qh = 180.0f, gx = 280.0f, gy = 200.0f;
    f32 ox = (g_view_w - (2.0f * gx + qw)) * 0.5f;
    f32 oy = (g_view_h - (gy + qh)) * 0.5f;
    for (u16 i = 0; i < 6; ++i) {
        // add() takes CENTER coords (generate_vertices emits ±half-size)
        f32 x = ox + qw * 0.5f + static_cast<f32>(i % 3) * gx;
        f32 y = oy + qh * 0.5f + static_cast<f32>(i / 3) * gy;
        g_batch.add(x, y, qw, qh, 0.0f, kTints[i], 1);
    }
}

void game_init(void *) {
    g_batch.init();
    LayoutQuads();

    auto &bk = *g_backend;
    BufferDesc vb_desc = {.type = BufferType::Vertex, .size = MAX_VERTS * sizeof(SpriteVertex), .stride = sizeof(SpriteVertex), .cpu_visible = true};
    if (auto vr = bk.create_buffer(vb_desc)) {
        g_vb = *vr;
    }
    BufferDesc ub_desc = {.type = BufferType::Uniform, .size = 64, .stride = 0, .cpu_visible = true};
    if (auto ur = bk.create_buffer(ub_desc)) {
        g_cam_ub = *ur;
    }
    if (auto pr2 = bk.create_buffer(ub_desc)) {
        g_param_ub = *pr2;
    }
    TextureDesc td = {TextureType::Tex2D, PixelFormat::R8G8B8A8_UNORM, 64, 64, 1, 1, 1};
    if (auto tr = bk.create_texture(td)) {
        g_texture = *tr;
    }
    SamplerDesc sd = {SamplerFilter::Linear, SamplerFilter::Linear, SamplerFilter::Linear, SamplerAddress::ClampToEdge,
                      SamplerAddress::ClampToEdge, SamplerAddress::ClampToEdge, CompareOp::Never, 1.0f};
    if (auto sr = bk.create_sampler(sd)) {
        g_sampler = *sr;
    }
    BuildCheckerTexture();
    BuildProceduralNormalTexture(); // D7 ready immediately; D8 loads PNG lazily per poll
    g_arena.init(g_arena_buf, ARENA_SIZE);

    // Fire light resources (D9/P): radial glow texture + additive pipeline.
    {
        TextureDesc gd = {TextureType::Tex2D, PixelFormat::R8G8B8A8_UNORM, 64, 64, 1, 1, 1};
        if (auto gr = bk.create_texture(gd)) {
            g_glow_tex = *gr;
        }
    }
    BuildGlowTexture();
    BuildGlowPipeline();
    g_fire_batch.init();
    g_anchor_x = g_view_w * 0.5f - 140.0f;
    g_anchor_y = g_view_h * 0.5f - 140.0f;
    InitEmbers();

    // Firework show (W): spark batches + shape textures + sky batch + params.
    for (u16 sh = 0; sh < FW_SHAPES; ++sh) {
        g_fw_batch[sh].init();
        TextureDesc sd = {TextureType::Tex2D, PixelFormat::R8G8B8A8_UNORM, 64, 64, 1, 1, 1};
        if (auto sr = bk.create_texture(sd)) {
            g_spark_tex[sh] = *sr;
        }
    }
    BuildSparkTextures();
    g_fw_sky_batch.init();
    LayoutFirework();
    BufferDesc fw_ub = {.type = BufferType::Uniform, .size = 64, .stride = 0, .cpu_visible = true};
    if (auto fr = bk.create_buffer(fw_ub)) {
        g_fw_param_ub = *fr;
    }
#if defined(USE_METAL_BACKEND)
    g_fw_file.found = LocateInLabDir("lab_firework.frag.msl", g_fw_file.path, sizeof(g_fw_file.path), &g_fw_file.mtime);
    printf("[lab] %-20s %s\n", "lab_firework.frag.msl", g_fw_file.found ? g_fw_file.path : "not-found");
    if (g_fw_file.found && ReadLabFile(g_fw_file) && BuildFireworkPipeline()) {
        printf("[lab] firework sky ready (W launches a rocket)\n");
    }
#endif

    // UI overlay: 5x7 font texture + alpha-blend pipeline for the on-screen
    // Prev/Next buttons and per-mode key hints (own batch, drawn last).
    {
        TextureDesc fd = {TextureType::Tex2D, PixelFormat::R8G8B8A8_UNORM, UI_FONT_TEX_W, UI_FONT_TEX_H, 1, 1, 1};
        if (auto fr = bk.create_texture(fd)) {
            g_font_tex = *fr;
        }
    }
    BuildFontTexture();
    BuildUiPipeline();
    g_ui_batch.init();

#if defined(USE_METAL_BACKEND)
    for (size_t i = 0; i < kLabSlotCount; ++i) {
        LabFile &f = g_lab_files[i];
        f.found = LocateInLabDir(kLabSlots[i].filename, f.path, sizeof(f.path), &f.mtime);
        printf("[lab] %-20s %s\n", kLabSlots[i].filename, f.found ? f.path : "not-found");
    }
#endif
    SelectEmbedded(g_mode);
    printf("[lab] keys: D1-5 embedded | D0 lab | D6 derive | D7 procedural | D8 png | D9 card | C cartoon | P plastic | G glow | Y gold | S stay | X sun | A aura | F fire | W show page | V 1/2/3s | H hue | J/L aim | I/K apex | T tune | B shape | N pattern | Q/E page | R reload | Space pause\n");
    printf("[lab] mode=%s pipe_valid=%d\n", ModeName(g_mode), g_pipe_valid ? 1 : 0);
}

void game_resize(void *, u32 w, u32 h) {
    if (w > 0) {
        g_view_w = static_cast<f32>(w);
    }
    if (h > 0) {
        g_view_h = static_cast<f32>(h);
    }
    LayoutQuads();
    LayoutFirework();
}

struct ModeHelp {
    const char *l1; // mode-specific params
    const char *l2; // mode-specific actions
};
// LabMode order: Sprite, Grayscale, Dissolve, Outline, ColorGrade, LabFile,
// NormalDerive, NormalProc, NormalPng, CardNormal, CardCartoon, CardPlastic,
// CardGlow, CardGold, CardStay, CardSun, CardAura, Firework.
// L3/L4 are global (show page + paging).
static constexpr ModeHelp kModeHelp[] = {
    {"UP/DN PROGRESS LF/RT EDGE", "R RELOAD SPACE PAUSE"},
    {"UP/DN PROGRESS LF/RT EDGE", "R RELOAD SPACE PAUSE"},
    {"UP/DN PROGRESS LF/RT EDGE", "R RELOAD SPACE PAUSE"},
    {"UP/DN PROGRESS LF/RT EDGE", "R RELOAD SPACE PAUSE"},
    {"UP/DN PROGRESS LF/RT EDGE", "R RELOAD SPACE PAUSE"},
    {"UP/DN PROGRESS LF/RT EDGE", "EDIT MSL LIVE R RELOAD"},
    {"ARROWS LIGHT AZ/EL", "R RELOAD SPACE PAUSE"},
    {"ARROWS LIGHT AZ/EL", "R RELOAD SPACE PAUSE"},
    {"ARROWS LIGHT AZ/EL", "EDIT PNG LIVE R RELOAD"},
    {"F FIRE ARROWS ANCHOR", "+/- HEIGHT R RELOAD"},
    {"ARROWS LIGHT +/- BANDS", "R RELOAD SPACE PAUSE"},
    {"ARROWS LIGHT AZ/EL", "F FIRE R RELOAD"},
    {"UP/DN DUR LF/RT EXPAND", "+/- INTENSITY R RELOAD"},
    {"UP/DN SPEED LF/RT WIDTH", "+/- INTENSITY R RELOAD"},
    {"UP/DN SPEED LF/RT WIDTH", "+/- INTENSITY R RELOAD"},
    {"UP/DN SPEED LF/RT RAYS", "+/- INTENSITY R RELOAD"},
    {"UP/DN SPEED LF/RT SPIKES", "+/- INTENSITY R RELOAD"},
    {"V INTERVAL W LAUNCH B SHAPE N BURST", "H HUE J/L AIM I/K APEX T TUNE"},
};
static constexpr const char *kUiHelpL3 = "W FIREWORK PAGE";
static constexpr const char *kUiHelpL4 = "Q/E PAGE";
static constexpr f32 UI_BTN_W = 108.0f;
static constexpr f32 UI_BTN_H = 40.0f;

void ModeStep(int dir) noexcept {
    int n = static_cast<int>(LabMode::_Count);
    int m = (static_cast<int>(g_mode) + dir + n) % n;
    SetMode(static_cast<LabMode>(m));
}

// Clicks (Select actions from touch or mouse) + hover + help text.
// Called every frame from game_frame; fills g_ui_batch for the UI pass.
void UiOverlay(InputState &input) noexcept {
    g_ui_batch.reset();
    // Title top-left: "3/15 GLOW" (upper-cased copy — the font is caps-only,
    // and '_' is not covered so it becomes a space).
    char title[32];
    size_t ti = 0;
    ti += static_cast<size_t>(snprintf(title, sizeof(title), "%d/%d ", static_cast<int>(g_mode) + 1, static_cast<int>(LabMode::_Count)));
    for (const char *p = ModeName(g_mode); *p != '\0' && ti + 1 < sizeof(title); ++p) {
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 32);
        } else if (c == '_') {
            c = ' ';
        }
        title[ti++] = c;
    }
    title[ti] = '\0';
    UiText(16.0f, 16.0f, title, UI_TEXT_SCALE, 0xFFFFFFFFu);
    // Prev/Next buttons, bottom corners.
    f32 by = g_view_h - 16.0f - UI_BTN_H;
    f32 prev_x = 16.0f;
    f32 next_x = g_view_w - 16.0f - UI_BTN_W;
    bool prev_hover = input.mouse_x >= prev_x && input.mouse_x < prev_x + UI_BTN_W && input.mouse_y >= by && input.mouse_y < by + UI_BTN_H;
    bool next_hover = input.mouse_x >= next_x && input.mouse_x < next_x + UI_BTN_W && input.mouse_y >= by && input.mouse_y < by + UI_BTN_H;
    for (u8 i = 0; i < input.action_count; ++i) {
        if (input.actions[i] != InputAction::Select) {
            continue;
        }
        f32 ax = input.action_x;
        f32 ay = input.action_y;
        if (ax >= prev_x && ax < prev_x + UI_BTN_W && ay >= by && ay < by + UI_BTN_H) {
            ModeStep(-1);
        } else if (ax >= next_x && ax < next_x + UI_BTN_W && ay >= by && ay < by + UI_BTN_H) {
            ModeStep(1);
        }
    }
    UiSolid(prev_x + UI_BTN_W * 0.5f, by + UI_BTN_H * 0.5f, UI_BTN_W, UI_BTN_H, prev_hover ? 0xE0505050u : 0xB0282828u, 0.15f);
    UiSolid(next_x + UI_BTN_W * 0.5f, by + UI_BTN_H * 0.5f, UI_BTN_W, UI_BTN_H, next_hover ? 0xE0505050u : 0xB0282828u, 0.15f);
    const char *prev_label = "< PREV";
    const char *next_label = "NEXT >";
    f32 label_h = 7.0f * UI_TEXT_SCALE;
    UiText(prev_x + (UI_BTN_W - UiTextWidth(prev_label, UI_TEXT_SCALE)) * 0.5f, by + (UI_BTN_H - label_h) * 0.5f, prev_label,
           UI_TEXT_SCALE, 0xFFFFFFFFu);
    UiText(next_x + (UI_BTN_W - UiTextWidth(next_label, UI_TEXT_SCALE)) * 0.5f, by + (UI_BTN_H - label_h) * 0.5f, next_label,
           UI_TEXT_SCALE, 0xFFFFFFFFu);
    // Help block, bottom-center on a translucent strip.
    const ModeHelp &mh = kModeHelp[static_cast<size_t>(g_mode)];
    const char *lines[4] = {mh.l1, mh.l2, kUiHelpL3, kUiHelpL4};
    f32 line_h = 20.0f;
    f32 strip_w = 0.0f;
    for (u8 li = 0; li < 4; ++li) {
        f32 w = UiTextWidth(lines[li], UI_TEXT_SCALE);
        if (w > strip_w) {
            strip_w = w;
        }
    }
    strip_w += 24.0f;
    if (strip_w > g_view_w - 2.0f * (UI_BTN_W + 32.0f)) {
        strip_w = g_view_w - 2.0f * (UI_BTN_W + 32.0f); // narrow window: text may clip, buttons win
    }
    f32 strip_h = 4.0f * line_h + 12.0f;
    f32 strip_y = g_view_h - 16.0f - strip_h;
    UiSolid(g_view_w * 0.5f, strip_y + strip_h * 0.5f, strip_w, strip_h, 0x80000000u, 0.05f);
    for (u8 li = 0; li < 4; ++li) {
        f32 w = UiTextWidth(lines[li], UI_TEXT_SCALE);
        UiText(g_view_w * 0.5f - w * 0.5f, strip_y + 6.0f + static_cast<f32>(li) * line_h, lines[li], UI_TEXT_SCALE, 0xFFDDDDDDu);
    }
    // Aura page steppers (top-right): filament count + filament opacity.
    if (g_mode == LabMode::CardAura) {
        char val[16];
        snprintf(val, sizeof(val), "%d", static_cast<int>(g_aura_spikes));
        int dspk = UiStepper(g_view_w - 16.0f - 190.0f, 56.0f, "SPK", val, input);
        if (dspk != 0) {
            g_aura_spikes += static_cast<f32>(dspk) * 4.0f;
            if (g_aura_spikes < 8.0f) {
                g_aura_spikes = 8.0f;
            }
            if (g_aura_spikes > 64.0f) {
                g_aura_spikes = 64.0f;
            }
            printf("[lab] aura spikes=%.0f\n", g_aura_spikes);
        }
        snprintf(val, sizeof(val), "%.1f", g_aura_opacity);
        int dopa = UiStepper(g_view_w - 16.0f - 190.0f, 56.0f + 42.0f, "OPA", val, input);
        if (dopa != 0) {
            g_aura_opacity += static_cast<f32>(dopa) * 0.1f;
            if (g_aura_opacity < 0.0f) {
                g_aura_opacity = 0.0f;
            }
            if (g_aura_opacity > 2.0f) {
                g_aura_opacity = 2.0f;
            }
            printf("[lab] aura opacity=%.1f\n", g_aura_opacity);
        }
    }
}

void game_frame(void *, f32 dt, InputState &input) {
    if (!g_paused) {
        g_time += dt;
    }
    if (input.just_pressed(KeyCode::D1)) {
        SetMode(LabMode::Sprite);
    } else if (input.just_pressed(KeyCode::D2)) {
        SetMode(LabMode::Grayscale);
    } else if (input.just_pressed(KeyCode::D3)) {
        SetMode(LabMode::Dissolve);
    } else if (input.just_pressed(KeyCode::D4)) {
        SetMode(LabMode::Outline);
    } else if (input.just_pressed(KeyCode::D5)) {
        SetMode(LabMode::ColorGrade);
    } else if (input.just_pressed(KeyCode::D0)) {
        SetMode(LabMode::LabFile);
    } else if (input.just_pressed(KeyCode::D6)) {
        SetMode(LabMode::NormalDerive);
    } else if (input.just_pressed(KeyCode::D7)) {
        SetMode(LabMode::NormalProc);
    } else if (input.just_pressed(KeyCode::D8)) {
        SetMode(LabMode::NormalPng);
    } else if (input.just_pressed(KeyCode::D9)) {
        SetMode(LabMode::CardNormal);
    } else if (input.just_pressed(KeyCode::C)) {
        SetMode(LabMode::CardCartoon);
    } else if (input.just_pressed(KeyCode::P)) {
        SetMode(LabMode::CardPlastic);
    } else if (input.just_pressed(KeyCode::G)) {
        SetMode(LabMode::CardGlow);
    } else if (input.just_pressed(KeyCode::Y)) {
        SetMode(LabMode::CardGold);
    } else if (input.just_pressed(KeyCode::S)) {
        SetMode(LabMode::CardStay);
    } else if (input.just_pressed(KeyCode::X)) {
        SetMode(LabMode::CardSun);
    } else if (input.just_pressed(KeyCode::A)) {
        SetMode(LabMode::CardAura);
    } else if (input.just_pressed(KeyCode::F)) {
        if (g_mode == LabMode::CardNormal || g_mode == LabMode::CardPlastic) {
            g_fire_on = !g_fire_on;
            if (g_fire_on) {
                // Start the anchor upper-left of the card so light rakes across.
                g_anchor_x = g_card_cx - 140.0f;
                g_anchor_y = g_card_cy - 140.0f;
                InitEmbers();
            }
            printf("[lab] fire %s (arrows move anchor, +/- height)\n", g_fire_on ? "ON" : "OFF");
        } else {
            printf("[lab] fire works in D9/P card modes only\n");
        }
    } else if (input.just_pressed(KeyCode::W)) {
        if (g_mode != LabMode::Firework) {
            SetMode(LabMode::Firework); // dedicated show page (auto-launch on)
        }
        LaunchFirework(false); // plus one rocket right now
    } else if (input.just_pressed(KeyCode::V)) {
        if (g_mode != LabMode::Firework) {
            SetMode(LabMode::Firework); // V always lands on the show page
        } else {
            g_fw_interval_sel = static_cast<u16>((g_fw_interval_sel + 1) % 3);
        }
        printf("[lab] firework show every %.0fs (V cycles 1/2/3)\n", kFwIntervals[g_fw_interval_sel]);
    } else if (input.just_pressed(KeyCode::H)) {
        g_fw_hue_mode = static_cast<u16>((g_fw_hue_mode + 1) % 3);
        printf("[lab] firework hue=%s (next burst)\n", kFwHueNames[g_fw_hue_mode]);
    } else if (input.just_pressed(KeyCode::J)) {
        g_fw_aim_x -= 0.05f;
        if (g_fw_aim_x < 0.05f) {
            g_fw_aim_x = 0.05f;
        }
        printf("[lab] firework aim x=%.0f%%\n", g_fw_aim_x * 100.0f);
    } else if (input.just_pressed(KeyCode::L)) {
        g_fw_aim_x += 0.05f;
        if (g_fw_aim_x > 0.95f) {
            g_fw_aim_x = 0.95f;
        }
        printf("[lab] firework aim x=%.0f%%\n", g_fw_aim_x * 100.0f);
    } else if (input.just_pressed(KeyCode::I)) {
        g_fw_apex -= 0.05f;
        if (g_fw_apex < 0.10f) {
            g_fw_apex = 0.10f;
        }
        printf("[lab] firework apex=%.0f%% height\n", g_fw_apex * 100.0f);
    } else if (input.just_pressed(KeyCode::K)) {
        g_fw_apex += 0.05f;
        if (g_fw_apex > 0.60f) {
            g_fw_apex = 0.60f;
        }
        printf("[lab] firework apex=%.0f%% height\n", g_fw_apex * 100.0f);
    } else if (input.just_pressed(KeyCode::T)) {
        g_fw_tune = !g_fw_tune;
        printf("[lab] firework tune %s (%s=%.2f, U/O select, -=/+ adjust)\n", g_fw_tune ? "ON" : "OFF",
               kFwTuneNames[g_fw_tune_sel], FwTuneValue(g_fw_tune_sel));
    } else if (input.just_pressed(KeyCode::U)) {
        if (!g_fw_tune) {
            printf("[lab] U selects tune param (press T first)\n");
        } else {
            g_fw_tune_sel = static_cast<u16>((g_fw_tune_sel + 2) % 3);
            printf("[lab] tune %s=%.2f\n", kFwTuneNames[g_fw_tune_sel], FwTuneValue(g_fw_tune_sel));
        }
    } else if (input.just_pressed(KeyCode::O)) {
        if (!g_fw_tune) {
            printf("[lab] O selects tune param (press T first)\n");
        } else {
            g_fw_tune_sel = static_cast<u16>((g_fw_tune_sel + 1) % 3);
            printf("[lab] tune %s=%.2f\n", kFwTuneNames[g_fw_tune_sel], FwTuneValue(g_fw_tune_sel));
        }
    } else if (input.just_pressed(KeyCode::B)) {
        g_fw_shape = static_cast<u16>((g_fw_shape + 1) % FW_SHAPES);
        printf("[lab] firework shape=%s (next launch)\n", kFwShapeNames[g_fw_shape]);
    } else if (input.just_pressed(KeyCode::N)) {
        g_fw_pattern = static_cast<u16>((g_fw_pattern + 1) % FW_PATTERNS);
        printf("[lab] firework pattern=%s (next launch)\n", kFwPatternNames[g_fw_pattern]);
    } else if (input.just_pressed(KeyCode::R)) {
        if (LabSlotForMode(g_mode) >= 0) {
            SetMode(g_mode); // force re-read current lab file
        }
    } else if (input.just_pressed(KeyCode::Space)) {
        g_paused = !g_paused;
    } else if (input.just_pressed(KeyCode::Q)) {
        ModeStep(-1); // same as the on-screen < PREV button
    } else if (input.just_pressed(KeyCode::E)) {
        ModeStep(1); // same as the on-screen NEXT > button
    }
    bool normal_mode = IsNormalMode(g_mode);
    bool fire_ctl = FireActive();
    // Cartoon bands / glow/gold intensity via typed +/- (KeyCode has no
    // +/- keys; the platform delivers printable chars through text_input —
    // macOS mm_app_mac.mm). In fire mode the same keys adjust light
    // height instead.
    bool glow_mode = (g_mode == LabMode::CardGlow);
    bool gold_mode = (g_mode == LabMode::CardGold);
    bool stay_mode = (g_mode == LabMode::CardStay);
    bool sun_mode = (g_mode == LabMode::CardSun);
    bool aura_mode = (g_mode == LabMode::CardAura);
    if (g_mode == LabMode::CardCartoon || glow_mode || gold_mode || stay_mode || sun_mode || aura_mode || fire_ctl || g_fw_tune) {
        for (u8 i = 0; i < input.text_count; ++i) {
            char c = input.text_input[i];
            if (c == '+' || c == '=') {
                if (g_fw_tune) {
                    FwTuneAdjust(1);
                } else if (stay_mode) {
                    g_stay_intensity += 0.1f;
                    if (g_stay_intensity > 3.0f) {
                        g_stay_intensity = 3.0f;
                    }
                    printf("[lab] stay intensity=%.2f\n", g_stay_intensity);
                } else if (sun_mode) {
                    g_sun_intensity += 0.1f;
                    if (g_sun_intensity > 3.0f) {
                        g_sun_intensity = 3.0f;
                    }
                    printf("[lab] sun intensity=%.2f\n", g_sun_intensity);
                } else if (aura_mode) {
                    g_aura_intensity += 0.1f;
                    if (g_aura_intensity > 3.0f) {
                        g_aura_intensity = 3.0f;
                    }
                    printf("[lab] aura intensity=%.2f\n", g_aura_intensity);
                } else if (gold_mode) {
                    g_gold_intensity += 0.1f;
                    if (g_gold_intensity > 3.0f) {
                        g_gold_intensity = 3.0f;
                    }
                    printf("[lab] gold intensity=%.2f\n", g_gold_intensity);
                } else if (glow_mode) {
                    g_glow_intensity += 0.1f;
                    if (g_glow_intensity > 3.0f) {
                        g_glow_intensity = 3.0f;
                    }
                    printf("[lab] glow intensity=%.2f\n", g_glow_intensity);
                } else if (g_mode == LabMode::CardCartoon) {
                    g_cartoon_bands += 1.0f;
                    if (g_cartoon_bands > 5.0f) {
                        g_cartoon_bands = 5.0f;
                    }
                    printf("[lab] bands=%.0f\n", g_cartoon_bands);
                } else {
                    g_light_h += 25.0f;
                    if (g_light_h > 800.0f) {
                        g_light_h = 800.0f;
                    }
                    printf("[lab] light height=%.0f\n", g_light_h);
                }
            } else if (c == '-' || c == '_') {
                if (g_fw_tune) {
                    FwTuneAdjust(-1);
                } else if (stay_mode) {
                    g_stay_intensity -= 0.1f;
                    if (g_stay_intensity < 0.0f) {
                        g_stay_intensity = 0.0f;
                    }
                    printf("[lab] stay intensity=%.2f\n", g_stay_intensity);
                } else if (sun_mode) {
                    g_sun_intensity -= 0.1f;
                    if (g_sun_intensity < 0.0f) {
                        g_sun_intensity = 0.0f;
                    }
                    printf("[lab] sun intensity=%.2f\n", g_sun_intensity);
                } else if (aura_mode) {
                    g_aura_intensity -= 0.1f;
                    if (g_aura_intensity < 0.0f) {
                        g_aura_intensity = 0.0f;
                    }
                    printf("[lab] aura intensity=%.2f\n", g_aura_intensity);
                } else if (gold_mode) {
                    g_gold_intensity -= 0.1f;
                    if (g_gold_intensity < 0.0f) {
                        g_gold_intensity = 0.0f;
                    }
                    printf("[lab] gold intensity=%.2f\n", g_gold_intensity);
                } else if (glow_mode) {
                    g_glow_intensity -= 0.1f;
                    if (g_glow_intensity < 0.0f) {
                        g_glow_intensity = 0.0f;
                    }
                    printf("[lab] glow intensity=%.2f\n", g_glow_intensity);
                } else if (g_mode == LabMode::CardCartoon) {
                    g_cartoon_bands -= 1.0f;
                    if (g_cartoon_bands < 2.0f) {
                        g_cartoon_bands = 2.0f;
                    }
                    printf("[lab] bands=%.0f\n", g_cartoon_bands);
                } else {
                    g_light_h -= 25.0f;
                    if (g_light_h < 100.0f) {
                        g_light_h = 100.0f;
                    }
                    printf("[lab] light height=%.0f\n", g_light_h);
                }
            }
        }
    }
    if (input.just_pressed(KeyCode::Up)) {
        if (fire_ctl) {
            g_anchor_y -= 20.0f;
            printf("[lab] anchor=(%.0f,%.0f)\n", g_anchor_x, g_anchor_y);
        } else if (glow_mode) {
            g_glow_dur += 0.1f;
            if (g_glow_dur > 2.5f) {
                g_glow_dur = 2.5f;
            }
            printf("[lab] glow dur=%.2fs\n", g_glow_dur);
        } else if (gold_mode) {
            g_gold_speed += 0.1f;
            if (g_gold_speed > 3.0f) {
                g_gold_speed = 3.0f;
            }
            printf("[lab] gold speed=%.2fHz\n", g_gold_speed);
        } else if (stay_mode) {
            g_stay_speed += 0.1f;
            if (g_stay_speed > 3.0f) {
                g_stay_speed = 3.0f;
            }
            printf("[lab] stay speed=%.2fHz\n", g_stay_speed);
        } else if (sun_mode) {
            g_sun_speed += 0.01f;
            if (g_sun_speed > 0.5f) {
                g_sun_speed = 0.5f;
            }
            printf("[lab] sun speed=%.3fHz\n", g_sun_speed);
        } else if (aura_mode) {
            g_aura_speed += 0.05f;
            if (g_aura_speed > 1.0f) {
                g_aura_speed = 1.0f;
            }
            printf("[lab] aura speed=%.2fHz\n", g_aura_speed);
        } else if (normal_mode) {
            g_light_el += 0.1f;
            if (g_light_el > 1.55f) {
                g_light_el = 1.55f;
            }
            printf("[lab] light el=%.2f\n", g_light_el);
        } else {
            g_progress += 0.05f;
            if (g_progress > 1.0f) {
                g_progress = 1.0f;
            }
            printf("[lab] progress=%.2f\n", g_progress);
        }
    }
    if (input.just_pressed(KeyCode::Down)) {
        if (fire_ctl) {
            g_anchor_y += 20.0f;
            printf("[lab] anchor=(%.0f,%.0f)\n", g_anchor_x, g_anchor_y);
        } else if (glow_mode) {
            g_glow_dur -= 0.1f;
            if (g_glow_dur < 0.4f) {
                g_glow_dur = 0.4f;
            }
            printf("[lab] glow dur=%.2fs\n", g_glow_dur);
        } else if (gold_mode) {
            g_gold_speed -= 0.1f;
            if (g_gold_speed < 0.2f) {
                g_gold_speed = 0.2f;
            }
            printf("[lab] gold speed=%.2fHz\n", g_gold_speed);
        } else if (stay_mode) {
            g_stay_speed -= 0.1f;
            if (g_stay_speed < 0.2f) {
                g_stay_speed = 0.2f;
            }
            printf("[lab] stay speed=%.2fHz\n", g_stay_speed);
        } else if (sun_mode) {
            g_sun_speed -= 0.01f;
            if (g_sun_speed < 0.005f) {
                g_sun_speed = 0.005f;
            }
            printf("[lab] sun speed=%.3fHz\n", g_sun_speed);
        } else if (aura_mode) {
            g_aura_speed -= 0.05f;
            if (g_aura_speed < 0.05f) {
                g_aura_speed = 0.05f;
            }
            printf("[lab] aura speed=%.2fHz\n", g_aura_speed);
        } else if (normal_mode) {
            g_light_el -= 0.1f;
            if (g_light_el < 0.05f) {
                g_light_el = 0.05f;
            }
            printf("[lab] light el=%.2f\n", g_light_el);
        } else {
            g_progress -= 0.05f;
            if (g_progress < 0.0f) {
                g_progress = 0.0f;
            }
            printf("[lab] progress=%.2f\n", g_progress);
        }
    }
    if (input.just_pressed(KeyCode::Right)) {
        if (fire_ctl) {
            g_anchor_x += 20.0f;
            printf("[lab] anchor=(%.0f,%.0f)\n", g_anchor_x, g_anchor_y);
        } else if (glow_mode) {
            g_glow_expand += 0.01f;
            if (g_glow_expand > 0.30f) {
                g_glow_expand = 0.30f;
            }
            printf("[lab] glow expand=%.3f\n", g_glow_expand);
        } else if (gold_mode) {
            g_gold_width += 0.005f;
            if (g_gold_width > 0.12f) {
                g_gold_width = 0.12f;
            }
            printf("[lab] gold width=%.3f\n", g_gold_width);
        } else if (stay_mode) {
            g_stay_width += 0.005f;
            if (g_stay_width > 0.12f) {
                g_stay_width = 0.12f;
            }
            printf("[lab] stay width=%.3f\n", g_stay_width);
        } else if (sun_mode) {
            g_sun_rays += 1.0f;
            if (g_sun_rays > 24.0f) {
                g_sun_rays = 24.0f;
            }
            printf("[lab] sun rays=%.0f\n", g_sun_rays);
        } else if (aura_mode) {
            g_aura_spikes += 4.0f;
            if (g_aura_spikes > 64.0f) {
                g_aura_spikes = 64.0f;
            }
            printf("[lab] aura spikes=%.0f\n", g_aura_spikes);
        } else if (normal_mode) {
            g_light_az += 0.1f;
            printf("[lab] light az=%.2f\n", g_light_az);
        } else {
            g_edge_width += 0.01f;
            if (g_edge_width > 0.30f) {
                g_edge_width = 0.30f;
            }
            printf("[lab] edge=%.3f\n", g_edge_width);
        }
    }
    if (input.just_pressed(KeyCode::Left)) {
        if (fire_ctl) {
            g_anchor_x -= 20.0f;
            printf("[lab] anchor=(%.0f,%.0f)\n", g_anchor_x, g_anchor_y);
        } else if (glow_mode) {
            g_glow_expand -= 0.01f;
            if (g_glow_expand < 0.02f) {
                g_glow_expand = 0.02f;
            }
            printf("[lab] glow expand=%.3f\n", g_glow_expand);
        } else if (gold_mode) {
            g_gold_width -= 0.005f;
            if (g_gold_width < 0.005f) {
                g_gold_width = 0.005f;
            }
            printf("[lab] gold width=%.3f\n", g_gold_width);
        } else if (stay_mode) {
            g_stay_width -= 0.005f;
            if (g_stay_width < 0.005f) {
                g_stay_width = 0.005f;
            }
            printf("[lab] stay width=%.3f\n", g_stay_width);
        } else if (sun_mode) {
            g_sun_rays -= 1.0f;
            if (g_sun_rays < 3.0f) {
                g_sun_rays = 3.0f;
            }
            printf("[lab] sun rays=%.0f\n", g_sun_rays);
        } else if (aura_mode) {
            g_aura_spikes -= 4.0f;
            if (g_aura_spikes < 8.0f) {
                g_aura_spikes = 8.0f;
            }
            printf("[lab] aura spikes=%.0f\n", g_aura_spikes);
        } else if (normal_mode) {
            g_light_az -= 0.1f;
            printf("[lab] light az=%.2f\n", g_light_az);
        } else {
            g_edge_width -= 0.01f;
            if (g_edge_width < 0.005f) {
                g_edge_width = 0.005f;
            }
            printf("[lab] edge=%.3f\n", g_edge_width);
        }
    }

#if defined(USE_METAL_BACKEND)
    // Hot-reload poll for the active lab-file slot (shader .msl)
    int slot = LabSlotForMode(g_mode);
    if (slot >= 0) {
        g_poll_t += dt;
        if (g_poll_t >= POLL_INTERVAL) {
            g_poll_t = 0.0f;
            LabFile &f = g_lab_files[static_cast<size_t>(slot)];
            struct stat st;
            if (f.found && stat(f.path, &st) == 0 && static_cast<long>(st.st_mtime) != f.mtime) {
                if (ReadLabFile(f)) {
                    if (RebuildPipeline(f.code, f.size, kLabSlots[static_cast<size_t>(slot)].entry)) {
                        printf("[lab] reloaded %s (%zu bytes)\n", f.path, f.size);
                    } else {
                        printf("[lab] compile failed; kept previous pipeline (see FS COMPILE ERROR above)\n");
                    }
                }
            }
            // D8 also polls the PNG texture file
            if (g_mode == LabMode::NormalPng && g_png_found) {
                struct stat pst;
                if (stat(g_png_path, &pst) == 0 && static_cast<long>(pst.st_mtime) != g_png_mtime) {
                    printf("[lab] png changed — reloading %s\n", g_png_path);
                    LoadPngNormalTexture();
                }
            }
            // D9/P poll the card normal map (albedo is static per visit)
            if ((g_mode == LabMode::CardNormal || g_mode == LabMode::CardPlastic) && g_card_nrm_found) {
                struct stat cst;
                if (stat(g_card_nrm_path, &cst) == 0 && static_cast<long>(cst.st_mtime) != g_card_nrm_mtime) {
                    printf("[lab] card normal changed — reloading %s\n", g_card_nrm_path);
                    LoadCardNormal();
                }
            }
        }
    }
    // Firework sky shader polls independent of LabMode (an in-flight show
    // finishes rendering even after leaving the page).
    g_fw_poll_t += dt;
    if (g_fw_poll_t >= POLL_INTERVAL) {
        g_fw_poll_t = 0.0f;
        struct stat fst;
        if (g_fw_file.found && stat(g_fw_file.path, &fst) == 0 && static_cast<long>(fst.st_mtime) != g_fw_file.mtime) {
            if (ReadLabFile(g_fw_file)) {
                if (BuildFireworkPipeline()) {
                    printf("[lab] reloaded %s (%zu bytes)\n", g_fw_file.path, g_fw_file.size);
                } else {
                    printf("[lab] firework compile failed; kept previous sky (see FS COMPILE ERROR above)\n");
                }
            }
        }
    }
#endif

    if (!g_pipe_valid) {
        return;
    }
    if (FireActive()) {
        UpdateFire(g_paused ? 0.0f : dt);
    }
    if (g_fw_auto && !g_paused) {
        g_fw_auto_t += dt;
        if (g_fw_auto_t >= kFwIntervals[g_fw_interval_sel]) {
            g_fw_auto_t = 0.0f;
            LaunchFirework(true); // quiet: no console spam on auto launches
        }
    }
    UpdateFirework(g_paused ? 0.0f : dt);
    UiOverlay(input); // clicks + hover + help text into g_ui_batch (drawn in the UI pass below)
    auto &bk = *g_backend;
    g_arena.reset();

    mm_math::mat4 cam = mm_math::mat4::ortho(0.0f, g_view_w, g_view_h, 0.0f, -1.0f, 1.0f);
    (void)bk.update_buffer(g_cam_ub, cam.data(), 0, sizeof(mm_math::mat4));
    GenericParams params;
    WriteParams(params);
    (void)bk.update_buffer(g_param_ub, &params, 0, sizeof(GenericParams));

    u32 vert_count = 0;
    u32 max = static_cast<u32>(g_batch.count) * VERTS_PER_QUAD;
    if (max > 0) {
        auto *verts = g_arena.alloc_array<SpriteVertex>(max);
        if (verts) {
            // World coords == screen points (origin top-left); camera stays at 0.
            vert_count = g_batch.generate_vertices(verts, max, 0.0f, 0.0f, 1.0f);
            if (vert_count > 0) {
                (void)bk.update_buffer(g_vb, verts, 0, vert_count * sizeof(SpriteVertex));
            }
        }
    }
    PassDesc pass{};
    if (g_mode == LabMode::Firework) {
        pass.clear_color[0] = 0.02f; // night sky page (empty batch)
        pass.clear_color[1] = 0.02f;
        pass.clear_color[2] = 0.05f;
    } else {
        pass.clear_color[0] = 0.07f;
        pass.clear_color[1] = 0.09f;
        pass.clear_color[2] = 0.12f;
    }
    pass.clear_color[3] = 1.0f;
    pass.clear_depth = 1.0f;
    pass.clear_stencil = 0;
    pass.color_load = LoadOp::Clear;
    pass.depth_load = LoadOp::DontCare;
    pass.color_store = StoreOp::Store;
    pass.depth_store = StoreOp::DontCare;
    if (!bk.begin_pass(pass)) {
        return;
    }
    if (vert_count > 0) {
        (void)bk.bind_pipeline(g_pipeline);
        BufferHandle bufs[2] = {g_vb, g_cam_ub};
        (void)bk.bind_vertex_buffers(bufs, 2, nullptr, nullptr);
        (void)bk.bind_uniform_buffer(g_cam_ub, 0);   // [[buffer(1)]] camera
        (void)bk.bind_uniform_buffer(g_param_ub, 1); // [[buffer(2)]] params
        // Slot 0: checker in grid modes, card art in D9/C/P. Slot 1 (logical 2):
        // normal map in D7/D8/D9/P — see Metal backend quirk note above.
        // (Cartoon samples albedo only; no slot-1 bind needed.)
        bool use_card = (g_mode == LabMode::CardNormal || g_mode == LabMode::CardCartoon || g_mode == LabMode::CardPlastic || g_mode == LabMode::CardGlow || g_mode == LabMode::CardGold || g_mode == LabMode::CardStay || g_mode == LabMode::CardSun || g_mode == LabMode::CardAura) && g_card_valid;
        (void)bk.bind_fragment_texture(use_card ? g_card_tex : g_texture, 0);
        (void)bk.bind_fragment_sampler(g_sampler, 0);
        if ((g_mode == LabMode::NormalProc || g_mode == LabMode::NormalPng || use_card) && g_normal_valid) {
            // Logical index 2 lands on [[texture(1)]]/[[sampler(1)]]
            // (Metal backend quirk — index 1 aliases slot 0).
            (void)bk.bind_fragment_texture(g_normal_tex, 2);
            (void)bk.bind_fragment_sampler(g_sampler, 2);
        }
        (void)bk.draw(vert_count, 1, 0, 0);
    }
    // Fire embers on top (additive glow), appended after the card verts.
    u32 fcount = 0;
    if (FireActive() && g_glow_valid && g_fire_batch.count > 0) {
        u32 fmax = static_cast<u32>(g_fire_batch.count) * VERTS_PER_QUAD;
        auto *fverts = g_arena.alloc_array<SpriteVertex>(fmax);
        if (fverts) {
            fcount = g_fire_batch.generate_vertices(fverts, fmax, 0.0f, 0.0f, 1.0f);
            if (fcount > 0) {
                u32 base_bytes = vert_count * sizeof(SpriteVertex);
                (void)bk.update_buffer(g_vb, fverts, base_bytes, fcount * sizeof(SpriteVertex));
                (void)bk.bind_pipeline(g_glow_pipe);
                BufferHandle fbufs[2] = {g_vb, g_cam_ub};
                (void)bk.bind_vertex_buffers(fbufs, 2, nullptr, nullptr);
                (void)bk.bind_uniform_buffer(g_cam_ub, 0);
                (void)bk.bind_fragment_texture(g_glow_tex, 0);
                (void)bk.bind_fragment_sampler(g_sampler, 0);
                (void)bk.draw(fcount, 1, vert_count, 0);
            }
        }
    }
    // Firework show (W, any mode): night-sky quad first (alpha blend over
    // the scene), then additive sparks on top. Appended after earlier verts.
    // fw_tail tracks the GPU vertex tail so the UI pass below lands exactly.
    u32 fw_tail = vert_count + fcount;
    if (FireworkAlive()) {
        GenericParams fwp;
        WriteFireworkParams(fwp);
        (void)bk.update_buffer(g_fw_param_ub, &fwp, 0, sizeof(GenericParams));
        if (g_fw_valid && g_fw_sky_a > 0.01f && g_fw_sky_batch.count > 0) {
            u32 skmax = static_cast<u32>(g_fw_sky_batch.count) * VERTS_PER_QUAD;
            auto *skverts = g_arena.alloc_array<SpriteVertex>(skmax);
            if (skverts) {
                u32 skcount = g_fw_sky_batch.generate_vertices(skverts, skmax, 0.0f, 0.0f, 1.0f);
                if (skcount > 0) {
                    u32 base_bytes = fw_tail * sizeof(SpriteVertex);
                    (void)bk.update_buffer(g_vb, skverts, base_bytes, skcount * sizeof(SpriteVertex));
                    (void)bk.bind_pipeline(g_fw_pipe);
                    BufferHandle skbufs[2] = {g_vb, g_cam_ub};
                    (void)bk.bind_vertex_buffers(skbufs, 2, nullptr, nullptr);
                    (void)bk.bind_uniform_buffer(g_cam_ub, 0);   // [[buffer(1)]] camera
                    (void)bk.bind_uniform_buffer(g_fw_param_ub, 1); // [[buffer(2)]] params
                    (void)bk.bind_fragment_texture(g_texture, 0);
                    (void)bk.bind_fragment_sampler(g_sampler, 0);
                    (void)bk.draw(skcount, 1, fw_tail, 0);
                    fw_tail += skcount;
                }
            }
        }
        // Sparks, one additive draw per shape (own procedural texture).
        if (g_glow_valid) {
            for (u16 sh = 0; sh < FW_SHAPES; ++sh) {
                if (g_fw_batch[sh].count == 0) {
                    continue;
                }
                u32 wmax = static_cast<u32>(g_fw_batch[sh].count) * VERTS_PER_QUAD;
                auto *wverts = g_arena.alloc_array<SpriteVertex>(wmax);
                if (!wverts) {
                    continue;
                }
                u32 wcount = g_fw_batch[sh].generate_vertices(wverts, wmax, 0.0f, 0.0f, 1.0f);
                if (wcount == 0) {
                    continue;
                }
                u32 base_bytes = fw_tail * sizeof(SpriteVertex);
                (void)bk.update_buffer(g_vb, wverts, base_bytes, wcount * sizeof(SpriteVertex));
                (void)bk.bind_pipeline(g_glow_pipe);
                BufferHandle wbufs[2] = {g_vb, g_cam_ub};
                (void)bk.bind_vertex_buffers(wbufs, 2, nullptr, nullptr);
                (void)bk.bind_uniform_buffer(g_cam_ub, 0);
                (void)bk.bind_fragment_texture(g_spark_tex[sh], 0);
                (void)bk.bind_fragment_sampler(g_sampler, 0);
                (void)bk.draw(wcount, 1, fw_tail, 0);
                fw_tail += wcount;
            }
        }
    }
    // UI overlay (always on top): buttons + help text, alpha-blended.
    // Appended after all scene verts; own batch so atlas UVs stay on the
    // font texture (atlas_tex_w/h are shared batch members).
    if (g_ui_valid && g_ui_batch.count > 0) {
        u32 umax = static_cast<u32>(g_ui_batch.count) * VERTS_PER_QUAD;
        auto *uverts = g_arena.alloc_array<SpriteVertex>(umax);
        if (uverts) {
            u32 ucount = g_ui_batch.generate_vertices(uverts, umax, 0.0f, 0.0f, 1.0f);
            if (ucount > 0) {
                u32 base_bytes = fw_tail * sizeof(SpriteVertex);
                (void)bk.update_buffer(g_vb, uverts, base_bytes, ucount * sizeof(SpriteVertex));
                (void)bk.bind_pipeline(g_ui_pipe);
                BufferHandle ubufs[2] = {g_vb, g_cam_ub};
                (void)bk.bind_vertex_buffers(ubufs, 2, nullptr, nullptr);
                (void)bk.bind_uniform_buffer(g_cam_ub, 0);
                (void)bk.bind_fragment_texture(g_font_tex, 0);
                (void)bk.bind_fragment_sampler(g_sampler, 0);
                (void)bk.draw(ucount, 1, fw_tail, 0);
            }
        }
    }
    (void)bk.end_pass();
}

void game_cleanup(void *) {
    auto &bk = *g_backend;
    if (g_pipe_valid) {
        bk.destroy_pipeline(g_pipeline);
        g_pipe_valid = false;
    }
    if (g_glow_valid) {
        bk.destroy_pipeline(g_glow_pipe);
        g_glow_valid = false;
    }
    if (g_fw_valid) {
        bk.destroy_pipeline(g_fw_pipe);
        g_fw_valid = false;
    }
    if (g_ui_valid) {
        bk.destroy_pipeline(g_ui_pipe);
        g_ui_valid = false;
    }
    bk.destroy_texture(g_font_tex);
    bk.destroy_texture(g_glow_tex);
    for (u16 sh = 0; sh < FW_SHAPES; ++sh) {
        bk.destroy_texture(g_spark_tex[sh]);
    }
    bk.destroy_buffer(g_vb);
    bk.destroy_buffer(g_cam_ub);
    bk.destroy_buffer(g_param_ub);
    bk.destroy_buffer(g_fw_param_ub);
    bk.destroy_texture(g_texture);
    if (g_card_valid) {
        bk.destroy_texture(g_card_tex);
        g_card_valid = false;
    }
    if (g_normal_valid) {
        bk.destroy_texture(g_normal_tex);
        g_normal_valid = false;
    }
    bk.destroy_sampler(g_sampler);
}

} // namespace

AppCallbacks markmos_main(int argc, char **argv) {
    if (argc > 1 && argv && argv[1]) {
        const char *arg = argv[1];
        if (arg[0] == 'd' && arg[1] >= '1' && arg[1] <= '9' && arg[2] == '\0') {
            g_mode = static_cast<LabMode>(arg[1] - '1');
        } else if (arg[0] == 's') {
            g_mode = LabMode::Sprite;
        } else if (arg[0] == 'g' && arg[1] == 'r') {
            g_mode = LabMode::Grayscale;
        } else if (arg[0] == 'o') {
            g_mode = LabMode::Outline;
        } else if (arg[0] == 'c') {
            g_mode = LabMode::ColorGrade;
        } else if (arg[0] == 'l') {
            g_mode = LabMode::LabFile;
        } else if (arg[0] == 'n' && arg[1] == 'd') {
            g_mode = LabMode::NormalDerive;
        } else if (arg[0] == 'n' && arg[1] == 'p') {
            g_mode = LabMode::NormalProc;
        } else if (arg[0] == 'p' && arg[1] == 'n') {
            g_mode = LabMode::NormalPng;
        } else if (arg[0] == 'c' && arg[1] == 'a') {
            g_mode = LabMode::CardNormal;
        } else if (arg[0] == 'c' && arg[1] == 't') {
            g_mode = LabMode::CardCartoon;
        } else if (arg[0] == 'p' && arg[1] == 'l') {
            g_mode = LabMode::CardPlastic;
        } else if (arg[0] == 'g' && arg[1] == 'l') {
            g_mode = LabMode::CardGlow;
        } else if (arg[0] == 'g' && arg[1] == 'o') {
            g_mode = LabMode::CardGold;
        } else if (arg[0] == 's' && arg[1] == 't') {
            g_mode = LabMode::CardStay;
        } else if (arg[0] == 's' && arg[1] == 'u') {
            g_mode = LabMode::CardSun;
        } else if (arg[0] == 'a' && arg[1] == 'u') {
            g_mode = LabMode::CardAura;
        } else if (arg[0] == 'f') {
            g_mode = LabMode::Firework;
        }
    }
    return {.user_data = nullptr, .init = game_init, .frame = game_frame, .resize = game_resize, .cleanup = game_cleanup};
}

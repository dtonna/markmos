// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../input/mm_input_state.hpp"
#include "../math/mm_color.h"
#include "../math/mm_rect.h"
#include "mm_palette.hpp"
#include "../render/mm_renderer.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui {

// Text draws record with SortKey{text_layer, 0, 0, 1.0f} (see draw_text), and
// text_layer is a RUNTIME field: 0 by default, raised around overlay blocks
// (freecell does this). A scissor or a popup background that guards text must
// carry that SAME key — submit() stable-sorts by key alone, so a mismatch does
// not error, it silently reorders:
//   - key too high  -> the popup background paints OVER its own rows. That is
//     exactly how the combobox rows went missing in mm_07, whose host never
//     raises text_layer: its text sat in key 0 under a key-1 background.
//   - key too low   -> the scissor lands before the text and never clips it.
// Derive it from the renderer instead of hardcoding layer 1.
inline SortKey text_key(const Renderer &r) noexcept { return SortKey{r.text_layer, 0, 0, 1.0f}; }

// ─── Widget types ────────────────────────────────────────────────
enum class widget_type : u8 {
    PANEL,
    LABEL,
    BUTTON,
    TOGGLE,
    SLIDER,
    CHECKBOX,
    TEXT_FIELD,
    IMAGE,
    LISTBOX,  // appended last: existing type numbers stay stable
    COMBOBOX, // appended last: existing type numbers stay stable
    RADIOBOX, // appended last: existing type numbers stay stable
    PROGRESSBAR, // appended last: existing type numbers stay stable
    SEPARATOR,   // appended last: existing type numbers stay stable
    TABBAR,      // appended last: existing type numbers stay stable
    SCROLLVIEW,  // appended last: existing type numbers stay stable
};

enum widget_flag : u8 {
    WF_VISIBLE   = 1 << 0,
    WF_ENABLED   = 1 << 1,
    WF_CLIP      = 1 << 2,
    WF_FOCUSABLE = 1 << 3,
    WF_AUTO_W    = 1 << 4,
    WF_AUTO_H    = 1 << 5,
};

enum class btn_state : u8 {
    NORMAL,
    HOVER,
    PRESSED,
};

// Horizontal alignment of a widget's text inside its frame.
// Scopes to LABEL (buttons centre their own label by design, a TextField's
// caret assumes left) and to the ListBox/COMBOBOX default row text.
enum class text_align : u8 {
    LEFT   = 0,
    CENTER = 1,
    RIGHT  = 2,
};

// Pure geometry: where a line of `text_w` starts inside a `box_w`-wide box.
// Free function on purpose so it is testable without a Renderer, exactly
// like content_box()/content_insets().
inline f32 align_text_x(text_align a, f32 box_x, f32 box_w, f32 text_w) noexcept {
    switch (a) {
        case text_align::CENTER: return box_x + (box_w - text_w) * 0.5f;
        case text_align::RIGHT:  return box_x + box_w - text_w;
        case text_align::LEFT:
        default:                 return box_x;
    }
}

// The scale a widget's text is BOTH measured at and drawn at lives next to
// measure() below - it needs Manager, which is not defined yet at this point.

// ─── Layout alignment per axis: 0 = Start (left/top, legacy default) ──
enum class layout_align : u8 {
    START  = 0, // left / top (legacy: children pile from padding)
    CENTER = 1, // centered group within container inner size
    END    = 2, // right / bottom
};

// ─── Shape type for WidgetStyle ──────────────────────────────────
enum class shape_type : u8 {
    DEFAULT = 0, // use style's shape setting
    RECT,
    ROUNDED_RECT,
    CIRCLE,  // ring border only (see ring_*); radius forced 0.5
    ELLIPSE, // ring border only; radius forced 0.5 (aspect from quad)
    CUSTOM,  // skip Pass 1, on_draw handles everything
};

// ComboBox popup open/close animation. Per widget, so one dropdown can scale
// in while the next rolls out like a shutter.
enum class popup_anim_mode : u8 {
    SCALE_FADE = 0, // uniform scale about the field-side edge + alpha (default)
    GARAGE,         // roller door: height grows from the field edge, rows revealed
    SLIDE,          // translates a few px out of the field + alpha
    FADE,           // alpha only, geometry untouched
};

// Side order for per-side borders: Left, Top, Right, Bottom.
enum : u8 { BORDER_L = 0, BORDER_T = 1, BORDER_R = 2, BORDER_B = 3 };

// ListBox row renderer: the app draws a row's CONTENTS itself. Scrolling,
// selection highlight, hit-testing and clipping stay in the engine - they are
// the parts that must not be re-implemented per app (freecell's solved-games
// browser hand-rolled 242 lines, most of it exactly this).
// (row_x, row_y, row_w, row_h) is the row's rect inside the CONTENT area;
// `item` is the index into the listbox items. **row_w does NOT inset for the
// scrollbar** - right-aligned row content slides under it, so reserve it
// yourself (the default one-string row never noticed: its text is
// left-aligned). row_h is all you need to do that:
// `ListboxMetrics::K_BAR_RATIO * row_h` is exactly scrollbar_w.
using RowCallback = void (*)(u16 id, Renderer &r, SpriteBatch &b, f32 row_x, f32 row_y, f32 row_w, f32 row_h, int item,
                            void *user);

// ─── WidgetStyle: shared visual definition ───────────────────────
struct WidgetStyle {
    TextureHandle bg_tex;       // invalid = fallback bg_color
    u32      border_color; // legacy uniform fallback (0 = no border)
    f32         border_width; // legacy uniform fallback, normalized [0-1]
    f32         corner_r;     // 0=sharp, >0=rounded rect, 0.5=circle
    shape_type     shape;        // Rect / RoundedRect / Circle / Ellipse
    u8       _pad[3];
    u32      border_c[4]; // per-side color L,T,R,B (0 = use border_color)
    f32         border_w[4]; // per-side width L,T,R,B (all 0 = use legacy uniform)
    u32      ring_color;  // Circle/Ellipse ring (0 = use border_color)
    f32         ring_width;  // Circle/Ellipse ring width (0 = use border_width);
    // Hard drop shadow (alpha lives in shadow_color's top byte). One extra
    // quad in the SAME rounded batch - no new pipeline, no extra draw call.
    // shadow_grow expands the silhouette, which is what makes a hard shadow
    // read as depth instead of a second border. 0 color = no shadow.
    // Not drawn for textured fills: the quad would sample the fill's texture.
    u32      shadow_color;
    f32         shadow_dx;
    f32         shadow_dy;
    f32         shadow_grow;
    // Thumb texture for TOGGLE / SLIDER (invalid = theme colour). The thumb
    // is one primitive per widget, so a textured thumb costs its own small
    // flush instead of breaking the whole Pass-2 batch. The line progressbar
    // has no thumb by design - only fill + completion burst.
    TextureHandle thumb_tex;
    // Texture for the progressbar's VALUE FILL (and the slider's fill, which
    // shares the field name). The ramp spans the FILL, not the track: a
    // partially filled bar shows the whole ramp inside its own width, which
    // reads as a bright leading edge. (A track-wide ramp would need a UV
    // sub-rect, i.e. the texture's pixel size at draw time - not worth a
    // field for a look that is a matter of taste.)
    // A textured fill costs its OWN flush: a texture bind is per-flush, and
    // Pass 2 otherwise packs every track / fill / thumb into one batch.
    TextureHandle fill_tex;
    // Striped value fill (line variant): bands of `stripe_color`, `pitch` px
    // apart, running to the RIGHT (CSS repeating-linear-gradient). The phase
    // walks so the pattern drifts - that animation lives in update() and
    // render() only reads it, so render stays pure draw (Plan B).
    // stripe_pitch == 0 is the "off" switch, which is what keeps every
    // existing style byte-identical (the same rule as the per-side borders).
    // The bands are anchored at the fill's LEFT edge at a fixed pitch, so
    // they do NOT stretch as the value grows - a striped bar must not have its
    // stripes widen with the fill.
    f32         stripe_pitch; // px; 0 = no stripes
    u32      stripe_color; // band colour (0 + pitch>0 = translucent black)
};
static_assert(sizeof(WidgetStyle) == 104, "WidgetStyle size");

// Style whose FILL is a texture - which is how a gradient reaches the UI
// (WidgetStyle::bg_tex, see Renderer::make_gradient_texture). The rounded SDF
// path samples it as the fill, so the corner radius, the per-side borders and
// the edge AA all still apply.
MM_FORCE_INLINE static WidgetStyle gradient_style(TextureHandle grad, f32 corner_r, u32 border_color = 0,
                                                 f32 border_width = 0.0f) noexcept {
    WidgetStyle s{};
    s.bg_tex       = grad;
    s.corner_r     = corner_r;
    s.shape        = shape_type::ROUNDED_RECT;
    s.border_color = border_color;
    s.border_width = border_width;
    return s;
}


// The corner radius a widget's PASS-2 primitives use (slider track, progress
// bar track, toggle thumb...). Pass 1 already rounds the frame with the same
// number, so the parts inside it must use it too - a rounded frame around a
// square track is exactly the "border radius does not round" look. CIRCLE /
// ELLIPSE shapes force 0.5 (their frame is a circle, so the parts must be too).
MM_FORCE_INLINE static f32 style_corner_radius(const WidgetStyle &s, u8 widget_shape) noexcept {
    if (widget_shape == (u8)shape_type::CIRCLE || widget_shape == (u8)shape_type::ELLIPSE) {
        return 0.5f;
    }
    return s.corner_r;
}

// Resolve effective per-side (width, color) from a style.
// All-zero border_w[] → legacy uniform (border_width/border_color) on every
// side, so styles that never set per-side fields render exactly as before.
// Otherwise each side uses its own width, falling back per-side to
// border_color when its color entry is 0.
MM_FORCE_INLINE static void resolve_border_sides(const WidgetStyle &s, f32 out_w[4], u32 out_c[4]) noexcept {
    if (s.border_w[0] == 0.0f && s.border_w[1] == 0.0f && s.border_w[2] == 0.0f && s.border_w[3] == 0.0f) {
        for (int k = 0; k < 4; ++k) {
            out_w[k] = s.border_width;
            out_c[k] = s.border_color;
        }
        return;
    }
    for (int k = 0; k < 4; ++k) {
        out_w[k] = s.border_w[k];
        out_c[k] = s.border_c[k] ? s.border_c[k] : s.border_color;
    }
}

// Content box inside the style border (px, same space as ax/frame.w).
// Fill/thumb/drag must stay inside it: Pass 1 draws bg+border first, and
// later content would otherwise paint over the left/right caps (the border
// then reads as "missing"). Same rule toggle thumbs already follow.
MM_FORCE_INLINE static void content_box(const WidgetStyle &s, f32 ax, f32 w, f32 &x0, f32 &x1) noexcept {
    f32    sw[4];
    u32 sc[4];
    resolve_border_sides(s, sw, sc);
    x0 = ax + sw[BORDER_L] * w;
    x1 = ax + w - sw[BORDER_R] * w;
}

// Border insets in px (the inner band the SDF border occupies: bw in local
// units = bw*frame-size px per axis). Content (rows, text, children clip)
// must clear these on all four sides. Borderless styles inset 0.
MM_FORCE_INLINE static void content_insets(const WidgetStyle &s, f32 w, f32 h, f32 &il, f32 &it, f32 &ir, f32 &ib) noexcept {
    f32    sw[4];
    u32 sc[4];
    resolve_border_sides(s, sw, sc);
    il = sw[BORDER_L] * w;
    it = sw[BORDER_T] * h;
    ir = sw[BORDER_R] * w;
    ib = sw[BORDER_B] * h;
}

// Content rect inside the style border. Degenerate styles (insets eating
// the frame) yield x1<=x0 / y1<=y0 — callers fall back to frame geometry
// (today's behavior) or skip, never wrap unsigned scissor sizes.
MM_FORCE_INLINE static void content_rect(const WidgetStyle &s, f32 ax, f32 ay, f32 w, f32 h, f32 &x0, f32 &y0, f32 &x1,
                                         f32 &y1) noexcept {
    f32 il, it, ir, ib;
    content_insets(s, w, h, il, it, ir, ib);
    x0 = ax + il;
    y0 = ay + it;
    x1 = ax + w - ir;
    y1 = ay + h - ib;
}

// Resolve the Circle/Ellipse ring (width, color). Zero ring fields fall
// back to the legacy uniform border (NOT to per-side values — a circle has
// no sides, so per-side widths never apply to it).
MM_FORCE_INLINE static void resolve_ring(const WidgetStyle &s, f32 &out_w, u32 &out_c) noexcept {
    out_w = (s.ring_width != 0.0f) ? s.ring_width : s.border_width;
    out_c = s.ring_color ? s.ring_color : s.border_color;
}

// ─── UI events ───────────────────────────────────────────────────
enum class ui_event_type : u8 {
    CLICK,        // tap / keyboard confirm activated the widget
    CHANGE,       // slider / toggle / radio / list / combo value changed
    FOCUS_GAINED, // focus_id arrived here
    FOCUS_LOST,   // focus_id left here
    HOVER_ENTER,  // hot (hover) arrived here
    HOVER_EXIT,   // hot (hover) left here
};

struct UiEvent {
    u16      id;
    ui_event_type type;
    f32         value; // meaning per type — see emit_event doc
};

// ─── Widget node ─────────────────────────────────────────────────
using DrawCallback   = void (*)(u16 id, Renderer &r, SpriteBatch &batch, f32 abs_x, f32 abs_y, f32 dt, void *user);
// F7: click/draw callbacks receive an opaque owner pointer (no globals).
// ChangeCallback (sliders) intentionally keeps its old shape — no slider
// user needs context today; extend it the same way if that changes.
using ClickCallback  = void (*)(u16 id, void *user);
using ChangeCallback = void (*)(u16 id, f32 value);

struct Widget {
    mm_math::rect frame; // parent-relative pos+size (root = screen points); use .x/.y/.w/.h
    f32         scale;
    u32      bg_color;           // 0xAARRGGBB, 0 = transparent
    u32      text_color;         // 0xAARRGGBB
    f32         press_scale;        // current animated press scale (1.0 = normal)
    f32         press_scale_target; // target scale (1.0 normal, <1 pressed)
    f32         anim_t;             // animation timer
    f32         thumb_pos;          // toggle thumb position 0=off 1=on (smooth slide)
    f32         hover_factor;       // hover glow transition 0=idle 1=full glow
    ClickCallback          on_click;
    DrawCallback           on_draw;
    char                   text[48];
    u16               parent;    // UINT16_MAX = root
    u8                type;      // widget_type
    u8                flags;     // widget_flag
    u8                state;     // btn_state or toggle bool
    u8                style_id;  // index into Manager::styles[] (0 = classic)
    u8                shape;     // shape_type override (0 = use style's shape)
    i8                 pad[4];    // per-widget padding [top, right, bottom, left] (was parallel array; fits tail padding)
    mm_math::position_mode pos_mode;  // how frame.x/y resolve (default RELATIVE = legacy offset behavior)
    mm_math::anchor        anchor_pt; // ANCHORED only: which parent point to pin (frame.x/y = px offset)
};

static_assert(sizeof(Widget) == 128, "Widget size"); // rect is 16B/align-16 (was 120 with 2x vec2)

// ─── Theme — centralized color palette ───────────────────────────
struct Theme {
    // ── Colors ──────────────────────────────────────────────────
    u32     panel_bg;
    u32     button_bg;
    u32     button_text;
    u32     toggle_track_on;
    u32     toggle_track_off;
    u32     toggle_thumb;
    u32     toggle_thumb_hot;
    u32     toggle_text;
    u32     slider_bg;
    u32     slider_track;
    u32     slider_fill;
    u32     progressbar_bg;
    u32     progressbar_track;
    u32     progressbar_fill;
    u32     slider_thumb;
    u32     slider_thumb_hot;
    u32     slider_text;
    u32     checkbox_on;
    u32     checkbox_off;
    u32     checkbox_border;
    u32     checkbox_check;
    u32     checkbox_text;
    u32     listbox_bg;
    u32     listbox_sel;
    u32     listbox_text;
    u32     textfield_bg;
    u32     textfield_text;
    u32     cursor;
    u32     focus_color;
    u32     modal_backdrop; // Modal dim (alpha in the top byte; 0 = no dim)
    u32     text_primary;
    u32     separator_color;      // horizontal/vertical rule
    f32        separator_thickness;  // px
    u32     tab_bg;               // tab strip background
    u32     tab_active_bg;        // the active cell's fill
    u32     tab_active_fg;        // active cell label
    u32     tab_fg;               // inactive cell label
    u32     text_secondary;

    // ── Layout: default padding [top, right, bottom, left] ──────
    i8       panel_pad[4];
    i8       label_pad[4];
    text_align   label_align = text_align::LEFT; // default horizontal align for label()
    i8       button_pad[4];
    i8       toggle_pad[4];
    i8       slider_pad[4];
    i8       checkbox_pad[4];
    i8       textfield_pad[4];
    i8       listbox_pad[4];

    // ── Layout: spacing defaults ────────────────────────────────
    u8      layout_padding;
    u8      layout_spacing;

    // ── Font ────────────────────────────────────────────────────
    char         font_path[128]; // "" = use embedded default
    f32        font_scale;     // global scale multiplier

    static Theme dark() noexcept;
    static Theme load(const char *path) noexcept;
};

// ─── ListBox metrics (the "list header"): cached per-listbox record of
// derived item sizes, all in UI points (content_scale is applied globally
// by the renderer, like every other widget). Derivation chain (no
// per-object magic numbers — the K_* below are the only design tokens):
//   window layout → listbox size → row_h → text size:
//   text_scale = theme.font_scale × K_TEXT_RATIO
//   row_h = font line_height × text_scale + 2 × row_pad
//   visible / scrollbar_w / indent derive from row_h + listbox size.
// Refreshed every frame in render() (has the font); handle() reads the
// cache (guarded: row_h <= 0 means "not measured yet").
// Tab strip geometry: N equal cells across the frame. Deliberately NOT a
// WidgetStyle-driven widget (unlike every other family): a tab strip's parts
// are its own colours, so they come from the Theme and one plain flush draws
// bar + active cell. A style would buy rounded corners nobody asked for.
struct TabbarMetrics {
    f32 cell_w;
    f32 text_scale; // derived label scale, fits the cell height
    int   count;
};

struct ListboxMetrics {
    // Public because a RowCallback gets row_h but not scrollbar_w: right-aligned
    // row content has to inset by scrollbar_w itself, and that is row_h × this.
    static constexpr f32 K_BAR_RATIO = 0.36f; // scrollbar width / row_h

    f32 row_h;       // derived item height, px
    f32 text_scale;  // derived row text scale
    f32 text_h;      // line-box height at text_scale, px
    f32 scrollbar_w; // px
    f32 indent;      // row text left indent, px
    int   visible;     // visible rows
};

// ─── TreeView ────────────────────────────────────────────────────
//
// A TREE IS A LISTBOX plus a node model. That is the whole design: a ListBox
// already has the scroll, the selection, the keyboard navigation, the row
// callback and the metrics, so a separate widget would duplicate all five and
// add a fourth scrolling implementation. The only genuinely new thing is the
// FLATTEN - deciding which nodes are visible given which ancestors are open -
// and it is pure, so it is unit-testable without a Renderer or a Manager.
//
// The model is caller-owned and flat: `children` are CONTIGUOUS (first_child +
// child_count), which makes the walk a loop instead of a search.
//
// **Row order is ARRAY order, not a depth-first walk** — and that is a
// consequence of the contiguity, not a choice: a node's children are a range of
// indices, so a child cannot be interleaved between its parent and a sibling.
// An app that wants a particular look lays its array out in the order it wants
// the rows to appear; nothing has to be sorted at draw time.
struct TreeNode {
    i32  parent      = -1;   // -1 for a root
    u32 first_child = 0;    // index of the first child, if any
    u32 child_count = 0;
    u8  flags       = 0;    // TREE_HAS_CHILDREN
    u8  _pad[3]     = {0, 0, 0};
};
static constexpr u8 TREE_HAS_CHILDREN = 1 << 0;

// The expanded set is a bitset (Manager::tree_expanded), so these take it as
// 64-bit words rather than a byte per node: no expansion buffer, and the pure
// helpers stay usable from a test with a stack array.
static bool tree_bit(const u64 *bits, u32 node) noexcept {
    return (bits[node >> 6] & (1ull << (node & 63))) != 0;
}

// True when every ancestor of `node` is expanded. A root is always visible; a
// node under a collapsed parent is not. Walks up with a guard so a cyclic parent
// chain (app bug) cannot hang the frame.
static bool tree_node_visible(const TreeNode *nodes, u32 count, const u64 *bits, u32 node) noexcept {
    if (nodes == nullptr || bits == nullptr || node >= count) {
        return false;
    }
    u32 hops = 0;
    while (nodes[node].parent >= 0) {
        const u32 p = static_cast<u32>(nodes[node].parent);
        if (p >= count || !tree_bit(bits, p)) {
            return false;
        }
        node = p;
        if (++hops > count) {
            return false; // cycle
        }
    }
    return true;
}

// How many ROWS a listbox of this tree shows: the visible nodes, in document
// order. Pure, and the number the flatten below is checked against.
static u32 tree_visible_count(const TreeNode *nodes, u32 count, const u64 *bits) noexcept {
    if (nodes == nullptr || bits == nullptr) {
        return 0;
    }
    u32 n = 0;
    for (u32 i = 0; i < count; ++i) {
        if (tree_node_visible(nodes, count, bits, i)) {
            ++n;
        }
    }
    return n;
}

// Visible row -> node index. `out` is filled in document order; returns how many
// rows there are. A ROW indexes the flattened list, not the node array - that
// distinction is the whole point, and conflating the two is the bug a row
// callback can otherwise have.
static u32 tree_flatten(const TreeNode *nodes, u32 count, const u64 *bits, u32 *out, u32 out_cap) noexcept {
    if (nodes == nullptr || bits == nullptr || out == nullptr) {
        return 0;
    }
    u32 n = 0;
    for (u32 i = 0; i < count && n < out_cap; ++i) {
        if (tree_node_visible(nodes, count, bits, i)) {
            out[n++] = i;
        }
    }
    return n;
}

// Defined in mm_ui_wlist.hpp, which is included at the TAIL of this header (the
// family modules need Manager). Declared here so Manager's own body can call
// them - tree_sync() clamps the listbox's scroll after a collapse.
namespace listbox {
f32 maxscroll(u8 item_count, const ListboxMetrics& m) noexcept;
void clamp_scroll(f32& scroll, u8 item_count, const ListboxMetrics& m) noexcept;
} // namespace listbox

// ─── DataGrid ────────────────────────────────────────────────────
//
// Cells are `const char *` and a column may not be wider than the grid.
static constexpr u32 MAX_GRID_COLS = 8;

// A sortable table. Again: a LISTBOX plus a column model, for the same reason a
// tree is one - scroll, selection, keyboard and the row callback are the
// listbox's. What is new is the HEADER (a row of column titles that takes the
// tap, so the listbox's rows can stay data) and the SORT ORDER, which the grid
// maintains rather than asking the app to re-sort its own array on every tap.
//
// The column model is caller-owned and flat, like TreeNode's.
struct GridColumn {
    const char *title;
    f32       width;    // px, or <= 0 to share the remainder
    text_align  align;    // per-column alignment for the cells
    bool        sortable;
};

struct GridRow {
    const char *cells[8]; // up to 8 columns, nullptr/"" ends the row
};

// Sort: a column index plus a direction. Applied by INDEX, so the grid never
// touches the app's array - the model is read-only, and `grid_row_source` tells
// the app which model row to draw for a visible row.
enum class grid_sort : u8 { NONE = 0, ASC = 1, DESC = 2 };

// Pure comparison of one column of two rows. `a`/`b` are the cell POINTERS, so
// the grid compares text without owning it. Numeric columns compare as numbers
// when both parse, which is what makes "12" sort before "9" - a string compare
// puts "12" after "9" and a price column looks broken.
inline int grid_cell_cmp(const char *a, const char *b) noexcept {
    if (a == nullptr) {
        a = "";
    }
    if (b == nullptr) {
        b = "";
    }
    // Numeric when BOTH parse as a number, so an int column is not sorted as text.
    char       *ea = nullptr;
    char       *eb = nullptr;
    const f64 da = std::strtod(a, &ea);
    const f64 db = std::strtod(b, &eb);
    const bool   na = (ea != a) && (*ea == '\0');
    const bool   nb = (eb != b) && (*eb == '\0');
    if (na && nb) {
        if (da < db) {
            return -1;
        }
        return da > db ? 1 : 0;
    }
    return std::strcmp(a, b) < 0 ? -1 : (std::strcmp(a, b) > 0 ? 1 : 0);
}

// Where a column's header cell sits, given the widths already assigned. Pure,
// so a test can check the geometry without a Renderer. Widths are resolved
// first: every column with width <= 0 shares what is left.
struct GridMetrics {
    f32    x[MAX_GRID_COLS]; // left edge of each column
    f32    w[MAX_GRID_COLS]; // resolved width
    f32    total_w;
    u32 columns;
};

inline GridMetrics compute_grid_metrics(const GridColumn *cols, u32 count, f32 total_w) noexcept {
    GridMetrics m{};
    m.columns = count < MAX_GRID_COLS ? count : MAX_GRID_COLS;
    if (cols == nullptr || m.columns == 0) {
        m.total_w = total_w;
        return m;
    }
    f32 used = 0.0f;
    u32 flex = 0;
    for (u32 i = 0; i < m.columns; ++i) {
        if (cols[i].width > 0.0f) {
            used += cols[i].width;
        } else {
            ++flex;
        }
    }
    const f32 rest  = total_w - used;
    const f32 share = flex > 0 ? (rest > 0.0f ? rest / static_cast<f32>(flex) : 0.0f) : 0.0f;
    f32       x     = 0.0f;
    for (u32 i = 0; i < m.columns; ++i) {
        m.x[i] = x;
        m.w[i] = cols[i].width > 0.0f ? cols[i].width : share;
        x += m.w[i];
    }
    m.total_w = x;
    return m;
}

// Sort tap cycles ASC <-> DESC after the first tap arms the column: a third
// state ("unsorted") would mean a tap that sorts by NOTHING, which is never what
// the user meant.
inline grid_sort grid_tap_cycle(grid_sort cur) noexcept {
    return cur == grid_sort::ASC ? grid_sort::DESC : grid_sort::ASC;
}

// Builds the model-row order for a sort, writing INDICES (the app's array is
// never touched) into `out`. Stable insertion sort: equal cells keep the model's
// order, because a table that reshuffles equal rows on every tap is unreadable.
// Returns how many indices were written.
inline u32 grid_sort_order(const GridRow *rows, u32 count, u8 col, grid_sort dir, u8 *out, u32 cap) noexcept {
    if (rows == nullptr || out == nullptr) {
        return 0;
    }
    u32 n = count < cap ? count : cap;
    if (n > 255) {
        n = 255; // the order array is uint8, like every row index here
    }
    for (u32 i = 0; i < n; ++i) {
        out[i] = static_cast<u8>(i);
    }
    if (n < 2 || dir == grid_sort::NONE || col >= MAX_GRID_COLS) {
        return n;
    }
    const u8 lim = col < MAX_GRID_COLS ? col : 0;
    const bool    desc = dir == grid_sort::DESC;
    for (u32 i = 1; i < n; ++i) {
        const u8 v = out[i];
        u32      j = i;
        while (j > 0) {
            int c = grid_cell_cmp(rows[v].cells[lim], rows[out[j - 1]].cells[lim]);
            if (desc) {
                c = -c;
            }
            if (c >= 0) {
                break;
            }
            out[j] = out[j - 1];
            --j;
        }
        out[j] = v;
    }
    return n;
}

// ─── Accordion ───────────────────────────────────────────────────
//
// A HEADER BUTTON plus a content region that animates open/closed. Composed from
// existing widgets (panel + button + content) rather than being a container of
// its own: an accordion with its own child list would be a second layout system
// for a widget that is, structurally, a panel whose height is animated.
//
// What the engine owns is the only part that is genuinely fiddly - the height
// animation and the clipping that goes with it:
//   drawn_h  = what is on screen this frame (eased toward target_h)
//   target_h = open ? content_h : 0
// Content is clipped to the drawn height, so a collapsing section reveals less
// of itself every frame instead of vanishing at the end.

struct AccordionStyle {
    static constexpr f32 ANIM_SPEED = 12.0f; // matches the widget lerp rate
    static constexpr f32 MIN_H       = 1.0f; // a 0-height clip is backend-dependent
};

// Eased height for a frame. Pure, so the collapse can be tested without a
// Renderer: `dt == 0` must be a no-op (the update(dt) contract), and it snaps on
// arrival so a settled accordion stops touching its frame every frame.
inline f32 accordion_step(f32 drawn_h, f32 target_h, f32 dt) noexcept {
    if (!(dt > 0.0f)) {
        return drawn_h;
    }
    const f32 t = 1.0f - std::exp(-AccordionStyle::ANIM_SPEED * dt);
    f32       h = drawn_h + (target_h - drawn_h) * t;
    if (__builtin_fabsf(target_h - h) < 0.5f) {
        h = target_h;
    }
    return h < 0.0f ? 0.0f : h;
}

// ─── UI Manager ──────────────────────────────────────────────────
struct Manager {
    static constexpr u16 MAX = 128;
    Widget                    pool[MAX];
    u16                  count;

    u16                  freelist[MAX];
    u16                  freelist_count;

    u16                  hot;      // widget under pointer
    u16                  active;   // widget being pressed (tracked across frames)
    u16                  clicked;  // widget clicked this frame
    u16                  focus_id; // keyboard/gamepad focus
    // Where focus WAS, when it had to be dropped because that widget turned out
    // to be disabled or hidden. Tab then resumes from here instead of jumping to
    // the first focusable widget on the page, which is what a player expects
    // after a control greys out. Cleared as soon as it is consumed.
    u16                  focus_anchor;

    // Slider storage (parallel arrays)
    f32                     slider_value[MAX];
    ChangeCallback            on_change[MAX];

    // Progressbar storage (parallel arrays, no Widget growth).
    // value = displayed (smoothed) 0..1, target = set_progress() goal.
    // update(dt) lerps value toward target; render draws value only.
    // burst = completion-sparkle timer (-1 = inactive, else seconds since
    // the displayed value arrived at full); celebrated latches per fill so
    // each arrival pops exactly one sparkle (cleared when target drops).
    f32                     progressbar_value[MAX];
    f32                     progressbar_stripe_t[MAX]; // stripe phase, px
    f32                     progressbar_target[MAX];
    f32                     progressbar_burst[MAX];
    bool                      progressbar_celebrated[MAX];

    // F7: per-widget callback owners (parallel arrays, no Widget growth).
    // Factories init both to nullptr; hosts that need context assign them
    // next to on_click/on_draw (freecell sets its Game* everywhere).
    void                     *click_user[MAX];
    void                     *draw_user[MAX];

    // Toggle / Checkbox / Radiobox on/off colors (parallel arrays)
    u32                  on_color[MAX];
    u32                  off_color[MAX];

    // Radiobox group id (parallel array; one selected widget per group —
    // exclusivity enforced in radiobox_select, state holds the selection).
    u8                   radio_group[MAX];

    // Checkbox / Radiobox state cross-fade: 0 = fully off, 1 = fully on.
    // Lerped toward `state` in update() with the same t_lerp as the toggle
    // thumb, so tapping a radio fades its dot in/out instead of popping it.
    // A parallel array (Widget stays 128B) for the same reason as the rest.
    f32                     state_fade[MAX];

    //     // Per-widget material override (invalid = use default)
    Material                  widget_material[MAX];

    // Layout data (parallel arrays)
    u8                   layout_type[MAX];    // 0=None, 1=HBox, 2=VBox
    u8                   layout_pad[MAX];     // uniform padding
    u8                   layout_spacing[MAX]; // gap between children
    // Extended layout (all default 0 = legacy behavior: absolute pos/size)
    u8                   layout_align[MAX]; // packed: bits[1:0]=main-axis, bits[3:2]=cross-axis (layout_align)
    u8                   size_pct_w[MAX];   // 0 = absolute w, else % of container inner width
    u8                   size_pct_h[MAX];   // 0 = absolute h, else % of container inner height
    i8                    margin[MAX][4];    // outer margin [top, right, bottom, left]

    // TextField data (parallel arrays)
    u8                   cursor_pos[MAX];

    // ListBox data (parallel arrays; the caller owns the item strings)
    const char *const        *listbox_items[MAX];
    u8                   listbox_count[MAX];
    i8                    listbox_selected[MAX]; // -1 = none
    f32                     listbox_scroll[MAX];   // rows scrolled (fractional ok)
    f32                     listbox_anchor[MAX];   // body-drag anchor scroll
    ListboxMetrics            listbox_metrics[MAX];  // cached derivation (the list header)

    // ComboBox data (parallel arrays; the caller owns the item strings).
    // Closed-field label/filter lives in Widget.text[48] (selected item
    // label in select mode, editable filter buffer in editable mode).
    // Popup row geometry reuses compute_listbox_metrics() (same chain as
    // Listbox); popup height = min(visible_items, K_MAX_POPUP_ROWS)*row_h.
    static constexpr u8  K_MAX_POPUP_ROWS = 6;
    const char *const        *combobox_items[MAX];
    u8                   combobox_count[MAX];
    i8                    combobox_selected[MAX];  // -1 = none (item index, not filtered row)
    f32                     combobox_scroll[MAX];

    // Combobox scroll + popup animation state.
    // combobox_scroll is the DRAWN value (render reads it); scroll_target is what the
    // finger/wheel/keys asked for and update() eases the drawn value toward it
    // - a drag that tracks the finger 1:1 reads as stutter, and a 1-frame jump
    // when the popup opens reads as a glitch. Same shape as progressbar
    // value/target, same t_lerp.
    f32                     combobox_scroll_target[MAX];
    f32                     combobox_scroll_vel[MAX];  // rows/s, for flick inertia
    f32                     combobox_anim[MAX];        // 0..1 popup open/close
    popup_anim_mode           combobox_anim_mode[MAX];
    bool                      combobox_closing[MAX];     // close requested, still drawing
    f32                     combobox_anchor[MAX];    // popup body-drag anchor scroll
    bool                      combobox_open[MAX];      // popup visible
    bool                      combobox_editable[MAX];  // true = type-to-filter, false = select-only
    bool                      combobox_filtering[MAX]; // true = text[] is a live filter (not the label)
    int                       combobox_hl[MAX];        // highlighted visible row (-1 = none; arrows move this, Enter commits)
    ListboxMetrics            combobox_metrics[MAX];   // cached row derivation (the list header)

    // Per-widget padding lives in Widget::pad (every widget has it by
    // construction; factories copy the theme default, image zeroes).

    // Cached content size from measure pass (text w/h at scale)
    u16                  content_w[MAX];
    u16                  content_h[MAX];
    u16                  content_ascent[MAX]; // max ascender (baseline up) from glyph metrics

    // Absolute position cache (rebuilt each frame)
    f32                     _abs_x[MAX];
    f32                     _abs_y[MAX];

    // Last rendered view size, UI points (set in render(), read in
    // handle() for popup flip/clamp; 0 = unknown → no flip).
    f32                     view_w;
    f32                     view_h;

    // TextField edit state
    u16                  editing_id;
    // True once a press inside a ScrollView has travelled far enough to be a
    // drag. One gesture at a time (like `active`/`hot`), and it is what stops
    // the release from also firing a tap on whatever child was under the finger:
    // ScrollView is the first container whose CHILDREN are pickable, so "scroll
    // this" and "tap that" are the same gesture until something arbitrates.
    bool                      scroll_dragging = false;
    f32                     cursor_timer;

    // Event queue (bounded ring; hosts drain via poll_event)
    // Wheel distance per notch, shared by every scrolling surface (the ListBox
    // and ComboBox paths carry their own 3.0f literals from before this existed).
    static constexpr f32    K_SCROLL_WHEEL_STEP = 60.0f;
    // Vertical finger travel before a press inside a ScrollView becomes a drag
    // instead of a tap on whatever was under it. ScrollViews are the first
    // container whose CHILDREN are pickable, so "tap the child" and "scroll the
    // view" arrive as the same gesture and something has to arbitrate.
    static constexpr f32    K_SCROLL_DRAG_SLOP  = 6.0f;
    static constexpr u8  K_MAX_EVENTS = 64;
    UiEvent                   events[K_MAX_EVENTS];
    u8                   event_head; // next write slot
    u8                   event_tail; // next read slot (oldest)
    u8                   event_count;
    u16                  prev_focus; // focus edge detection (init UINT16_MAX)
    u16                  prev_hot;   // hover edge detection (init UINT16_MAX)

    // Measure dirty flag — skips measure() when nothing changed
    bool                      measure_dirty;
    bool                      abs_cache_dirty;

    // Theme
    Theme                     theme;

    // Style pool (shared visual definitions)
    static constexpr u16 MAX_STYLES = 64;
    WidgetStyle               styles[MAX_STYLES];
    u16                  style_count;

    u8                   register_style(const WidgetStyle &s) noexcept {
        assert(style_count < MAX_STYLES);
        u8 id = static_cast<u8>(style_count++);
        styles[id] = s;
        return id;
    }

    void     init() noexcept;
    void     clear() noexcept;
    void     backfill_callback_owners(void *owner) noexcept;

    u16 panel(f32 x, f32 y, f32 w, f32 h, u32 color, u16 parent = UINT16_MAX, u8 style_id = 0) noexcept;
    u16 label(f32 x, f32 y, const char *text, u32 color, f32 scale, u16 parent = UINT16_MAX, u8 style_id = 0) noexcept;
    u16 button(f32 x, f32 y, f32 w, f32 h, const char *text, u32 bg, u32 fg, ClickCallback cb, u16 parent = UINT16_MAX,
                    f32 scale = 1.2f, u8 style_id = 0, u8 shape = 0, DrawCallback on_draw = nullptr, void *user = nullptr) noexcept;
    // toggle / checkbox / radiobox take an optional LABEL scale last, defaulting
    // to 1.0 so every existing call site is untouched. They had no way to
    // author one at all (the label is drawn at Widget::scale, which the factory
    // pinned to 1.0), and a 1.0 label next to a fixed 28pt box is oversized -
    // which is how the ScrollView page's clip canaries ended up with 28pt-tall
    // text spilling across the viewport.
    u16 toggle(f32 x, f32 y, f32 w, f32 h, const char *text, u32 bg_on, u32 bg_off, u32 fg, bool initial, ClickCallback cb,
                    u16 parent = UINT16_MAX, u8 style_id = 0, f32 scale = 1.0f) noexcept;
    u16 slider(f32 x, f32 y, f32 w, f32 h, f32 initial, ChangeCallback cb, u16 parent = UINT16_MAX, u8 style_id = 0) noexcept;
    u16 progressbar(f32 x, f32 y, f32 w, f32 h, f32 initial, u16 parent = UINT16_MAX, f32 scale = 1.0f,
                         u8 style_id = 0, u8 shape = 0) noexcept;
    // Optional LABEL scale last, defaulting to 1.0 (see toggle/checkbox/radiobox):
    // a fixed-height field with a 1.0 value is a text box with no padding left,
    // and there was no way to author it any other way.
    u16 textfield(f32 x, f32 y, f32 w, f32 h, const char *initial_text, u32 bg, u32 fg, u16 parent = UINT16_MAX,
                       u8 style_id = 0, f32 scale = 1.0f) noexcept;
    u16 checkbox(f32 x, f32 y, const char *text, u32 on_c, u32 off_c, u32 fg, bool initial, ClickCallback cb,
                      u16 parent = UINT16_MAX, u8 style_id = 0, f32 scale = 1.0f) noexcept;
    u16 radiobox(f32 x, f32 y, const char *text, u32 on_c, u32 off_c, u32 fg, u8 group, bool initial, ClickCallback cb,
                      u16 parent = UINT16_MAX, u8 style_id = 0, f32 scale = 1.0f) noexcept;
    u16 image(f32 x, f32 y, f32 w, f32 h, u16 parent = UINT16_MAX, u8 style_id = 0) noexcept;
    u16 listbox(f32 x, f32 y, f32 w, f32 h, const char *const *items, u8 item_count, ClickCallback cb, u16 parent = UINT16_MAX,
                     u8 style_id = 0) noexcept;
    u16 combobox(f32 x, f32 y, f32 w, f32 h, const char *const *items, u8 item_count, bool editable, ClickCallback cb,
                      u16 parent = UINT16_MAX, u8 style_id = 0) noexcept;
    void     remove(u16 id) noexcept;
    void     set_material(u16 id, const Material &mat) noexcept;
    // Swap a widget's style at runtime. Needed for texture-backed looks (a
    // gradient fill, a pressed skin) that a per-widget colour multiply cannot
    // express. measure_dirty because measure() derives content insets from
    // the style, so the next frame's geometry follows. An out-of-range widget
    // or style is refused, never clamped into a valid slot.
    void set_style(u16 id, u8 style_id) noexcept {
        if (id < MAX && style_id < MAX_STYLES) {
            pool[id].style_id = style_id;
            measure_dirty     = true;
        }
    }

    void     set_layout(u16 id, u8 type, u8 padding = 0, u8 spacing = 0) noexcept;
    void     set_layout_align(u16 id, u8 main_align, u8 cross_align) noexcept;
    void     set_size_pct(u16 id, u8 w_pct, u8 h_pct) noexcept;
    void     set_margin(u16 id, i8 top, i8 right, i8 bottom, i8 left) noexcept;
    void     measure(Renderer &r) noexcept;
    void     layout(Renderer &r) noexcept;

    bool     was_clicked(u16 id) const noexcept { return id == clicked; }
    // ── Event queue (bounded ring, drained by the host) ──
    // Change.value carries: slider 0..1, toggle/checkbox state 0/1,
    // radio select 1, listbox/combobox selected item index. Click /
    // Focus / Hover events carry value 0. Full when event_count ==
    // K_MAX_EVENTS: push overwrites the oldest (newest state wins).
    void     emit_event(ui_event_type type, u16 id, f32 value = 0.0f) noexcept {
        if (id >= MAX) {
            return;
        }
        events[event_head] = UiEvent{id, type, value};
        event_head         = static_cast<u8>((event_head + 1) % K_MAX_EVENTS);
        if (event_count < K_MAX_EVENTS) {
            ++event_count;
        } else {
            event_tail = static_cast<u8>((event_tail + 1) % K_MAX_EVENTS);
        }
    }
    bool poll_event(UiEvent &out) noexcept { // FIFO; false when empty
        if (event_count == 0) {
            return false;
        }
        out        = events[event_tail];
        event_tail = static_cast<u8>((event_tail + 1) % K_MAX_EVENTS);
        --event_count;
        return true;
    }
    u8 event_pending() const noexcept { return event_count; }
    void    clear_events() noexcept { event_head = event_tail = event_count = 0; }
    bool    is_toggled(u16 id) const noexcept { return id < MAX ? pool[id].state != 0 : false; }
    f32   get_slider_value(u16 id) const noexcept { return id < MAX ? slider_value[id] : 0.0f; }
    f32   get_progress(u16 id) const noexcept { return id < MAX ? progressbar_value[id] : 0.0f; }
    void    set_progress(u16 id, f32 v) noexcept;
    int     get_listbox_selected(u16 id) const noexcept { return id < MAX ? listbox_selected[id] : -1; }
    int     get_combobox_selected(u16 id) const noexcept { return id < MAX ? combobox_selected[id] : -1; }
    bool    is_combobox_open(u16 id) const noexcept { return id < MAX ? combobox_open[id] : false; }
    bool    is_radiobox_selected(u16 id) const noexcept { return id < MAX && pool[id].type == (u8)widget_type::RADIOBOX && pool[id].state != 0; }
    int     get_radio_group_selected(u8 group) const noexcept {
        for (u16 i = 0; i < count; ++i) {
            if ((pool[i].flags & WF_VISIBLE) && pool[i].type == (u8)widget_type::RADIOBOX && radio_group[i] == group && pool[i].state) {
                return i;
            }
        }
        return -1;
    }
    const ListboxMetrics &get_listbox_metrics(u16 id) const noexcept {
        static const ListboxMetrics kZero{};
        return id < MAX ? listbox_metrics[id] : kZero;
    }
    const ListboxMetrics &get_combobox_metrics(u16 id) const noexcept {
        static const ListboxMetrics kZero{};
        return id < MAX ? combobox_metrics[id] : kZero;
    }
    // Pure derivation: unit-testable, no backend needed.
    // ui_scale is theme.font_scale (global UI size; the window's
    // content_scale is applied by the renderer, not here).
    static ListboxMetrics compute_listbox_metrics(f32 list_h, f32 line_height, f32 ui_scale) noexcept {
        constexpr f32 K_TEXT_RATIO   = 0.45f; // row text scale / ui_scale (gallery ratio)
        constexpr f32 K_ROW_PAD      = 4.0f;  // vertical pad per side, × ui_scale
        constexpr f32 K_MIN_ROW      = 16.0f; // min row height, × ui_scale
        constexpr f32 K_INDENT_RATIO = 0.28f; // text indent / row_h
        ListboxMetrics  m{};
        if (ui_scale <= 0.0f || line_height <= 0.0f || list_h <= 0.0f) {
            return m; // invalid → row_h 0, callers must guard
        }
        m.text_scale  = ui_scale * K_TEXT_RATIO;
        m.text_h      = line_height * m.text_scale;
        m.row_h       = m.text_h + 2.0f * K_ROW_PAD * ui_scale;
        f32 min_row = K_MIN_ROW * ui_scale;
        if (m.row_h < min_row) {
            m.row_h = min_row;
        }
        m.visible = static_cast<int>(list_h / m.row_h);
        if (m.visible < 1) {
            m.visible = 1;
        }
        m.scrollbar_w = m.row_h * ListboxMetrics::K_BAR_RATIO;
        m.indent      = m.row_h * K_INDENT_RATIO;
        return m;
    }
    bool is_editing() const noexcept { return editing_id != UINT16_MAX; }
    // True while keystrokes belong to a widget, not to app hotkeys: a
    // TextField is being edited, or an editable ComboBox is open (its
    // filter eats typing) or focused-closed (typing would auto-open it).
    // Hosts (labs, menus) must skip destructive/page hotkeys then —
    // otherwise typing 'm' in a filter jumps pages and wipes the text.
    bool is_text_capture() const noexcept {
        if (editing_id != UINT16_MAX) {
            return true;
        }
        for (u16 i = 0; i < count; ++i) {
            if (pool[i].type != (u8)widget_type::COMBOBOX) {
                continue;
            }
            if (!combobox_editable[i]) {
                continue;
            }
            // EFFECTIVE visible/enabled, like every other "can the user reach
            // this" question. The own flag answered "yes" for a combo inside a
            // hidden panel, so the host was told the user was mid-edit and
            // skipped every hotkey - silently, with nothing on screen to explain
            // it. This predicate is what a host consults BEFORE its own keys, so
            // a wrong answer here eats input rather than drawing anything.
            if (!is_visible(i) || !is_enabled(i)) {
                continue;
            }
            if (combobox_open[i]) {
                return true;
            }
            if (focus_id == i) {
                return true;
            }
        }
        return false;
    }

    // ── ComboBox helpers (pure w.r.t. strings; read cached metrics) ──
    // Case-insensitive substring: empty filter matches everything.
    static bool combobox_match(const char *item, const char *filter) noexcept {
        if (!filter || filter[0] == '\0') {
            return true;
        }
        if (!item) {
            return false;
        }
        for (const char *s = item; *s; ++s) {
            const char *a = s;
            const char *b = filter;
            for (;;) {
                char ca = *a, cb = *b;
                if (cb == '\0') {
                    return true;
                }
                if (ca >= 'A' && ca <= 'Z') {
                    ca = static_cast<char>(ca + 32);
                }
                if (cb >= 'A' && cb <= 'Z') {
                    cb = static_cast<char>(cb + 32);
                }
                if (ca != cb) {
                    break;
                }
                ++a;
                ++b;
            }
        }
        return false;
    }
    // Visible (filtered) item count. Select-only mode, or editable with
    // no live filter (label shown): all items.
    u8 combobox_visible_count(u16 id) const noexcept {
        if (id >= MAX) {
            return 0;
        }
        if (!combobox_editable[id] || !combobox_filtering[id]) {
            return combobox_count[id];
        }
        u8 n = 0;
        for (u8 i = 0; i < combobox_count[id]; ++i) {
            if (combobox_match(combobox_items[id][i], pool[id].text)) {
                ++n;
            }
        }
        return n;
    }
    // Map a visible popup row → item index (-1 = none).
    int combobox_visible_to_item(u16 id, int vis_row) const noexcept {
        if (id >= MAX || vis_row < 0) {
            return -1;
        }
        if (!combobox_editable[id] || !combobox_filtering[id]) {
            return vis_row < combobox_count[id] ? vis_row : -1;
        }
        int seen = 0;
        for (u8 i = 0; i < combobox_count[id]; ++i) {
            if (combobox_match(combobox_items[id][i], pool[id].text)) {
                if (seen == vis_row) {
                    return i;
                }
                ++seen;
            }
        }
        return -1;
    }
    // Map an item index → visible popup row (-1 when filtered out).
    int combobox_item_to_visible(u16 id, int item) const noexcept {
        if (id >= MAX || item < 0 || item >= combobox_count[id]) {
            return -1;
        }
        if (!combobox_editable[id] || !combobox_filtering[id]) {
            return item;
        }
        int seen = 0;
        for (u8 i = 0; i < combobox_count[id]; ++i) {
            if (combobox_match(combobox_items[id][i], pool[id].text)) {
                if (static_cast<int>(i) == item) {
                    return seen;
                }
                ++seen;
            }
        }
        return -1;
    }
    // Popup rect in UI points (overlay: below field, flipped above when it
    // would overflow view_h, clamped to view_w). view 0 = unknown → below.
    // Returns false when metrics not ready (row_h <= 0).
    bool combobox_popup_rect(u16 id, f32 &px, f32 &py, f32 &pw, f32 &ph) const noexcept {
        if (id >= MAX) {
            return false;
        }
        const ListboxMetrics &lm = combobox_metrics[id];
        if (lm.row_h <= 0.0f) {
            return false;
        }
        u8 vis  = combobox_visible_count(id);
        u8 rows = vis > K_MAX_POPUP_ROWS ? K_MAX_POPUP_ROWS : vis;
        if (rows == 0) {
            rows = 1; // empty filter still shows a 1-row "no match" box
        }
        const auto &w = pool[id];
        px            = _abs_x[id];
        pw            = w.frame.w;
        ph            = static_cast<f32>(rows) * lm.row_h;
        py            = _abs_y[id] + w.frame.h;
        if (view_h > 0.0f && py + ph > view_h && _abs_y[id] - ph >= 0.0f) {
            py = _abs_y[id] - ph; // flip above
        }
        if (view_w > 0.0f && px + pw > view_w) {
            px = view_w - pw;
            if (px < 0.0f) {
                px = 0.0f;
            }
        }
        return true;
    }
    static f32 combobox_maxscroll(u8 vis_count) noexcept {
        f32 x = static_cast<f32>(vis_count > K_MAX_POPUP_ROWS ? vis_count - K_MAX_POPUP_ROWS : 0);
        return x;
    }
    // Restore the closed label from the selection ("" when none).
    void combobox_restore_label(u16 id) noexcept {
        if (id >= MAX) {
            return;
        }
        int         sel = combobox_selected[id];
        const char *s   = (sel >= 0 && sel < combobox_count[id]) ? combobox_items[id][sel] : nullptr;
        if (s) {
            std::strncpy(pool[id].text, s, sizeof(pool[id].text) - 1);
            pool[id].text[sizeof(pool[id].text) - 1] = '\0';
        } else {
            pool[id].text[0] = '\0';
        }
        combobox_filtering[id] = false;
    }
    // Close without committing (outside tap, field toggle, Esc): drop any
    // live filter and restore the label.
    void combobox_close_cancel(u16 id) noexcept {
        if (id >= MAX) {
            return;
        }
        combobox_restore_label(id);
        pool[id].state = 0;
        // Shut with the close animation, but only if the popup actually
        // finished opening: a tap during the 130ms open would otherwise make
        // it shrink, which reads as "it bounced". Mid-open = snap shut, which
        // is both faster and what the tap asked for. update() flips `open`
        // once the animation lands.
        if (combobox_anim[id] < 0.999f) {
            combobox_shut_now(id);
        } else {
            combobox_closing[id] = true;
        }
        combobox_scroll_vel[id] = 0.0f;
    }

    // Complete a close immediately (no animation).
    void combobox_shut_now(u16 id) noexcept {
        if (id >= MAX) {
            return;
        }
        combobox_open[id]      = false;
        combobox_closing[id]   = false;
        combobox_anim[id]      = 0.0f;
        combobox_scroll_vel[id] = 0.0f;
        combobox_scroll[id]    = 0.0f;
        combobox_scroll_target[id] = 0.0f;
    }
    // Open (shows the full list: any live filter is dropped) and highlight
    // the current selection; scroll it into view. Closes other popups.
    void combobox_open_now(u16 id) noexcept {
        if (id >= MAX) {
            return;
        }
        for (u16 j = 0; j < MAX; ++j) {
            if (j != id && pool[j].type == (u8)widget_type::COMBOBOX && combobox_open[j]) {
                combobox_close_cancel(j);
            }
        }
        combobox_filtering[id] = false;
        combobox_open[id]      = true;
        combobox_closing[id]   = false;
        combobox_anim[id]      = 0.0f; // animate in from the field edge
        combobox_scroll_vel[id] = 0.0f;
        int vis                = combobox_item_to_visible(id, combobox_selected[id]);
        combobox_hl[id]        = vis;
        f32 &sc              = combobox_scroll[id];
        int    maxr            = static_cast<int>(K_MAX_POPUP_ROWS);
        if (vis >= 0) {
            if (vis < static_cast<int>(sc)) {
                sc = static_cast<f32>(vis);
            } else if (vis >= static_cast<int>(sc) + maxr) {
                sc = static_cast<f32>(vis - maxr + 1);
            }
        } else {
            sc = 0.0f;
        }
        f32 maxsc = combobox_maxscroll(combobox_visible_count(id));
        if (sc > maxsc) {
            sc = maxsc;
        }
        if (sc < 0.0f) {
            sc = 0.0f;
        }
        // Drawn value and target must agree, or the popup opens scrolled
        // somewhere and then visibly glides there.
        combobox_scroll_target[id] = sc;
    }
    // Select a radio: clear the rest of its group, set this one.
    // Caller fires on_click, and only on actual change (re-tapping the
    // selected radio is a silent no-op). Defined in mm_ui_wtoggle.hpp.
    void radiobox_select(u16 id) noexcept;
    // Commit an item (tap row / Enter): selection + label, popup shut.
    // Caller sets clicked + fires on_click.
    void combobox_commit(u16 id, int item) noexcept {
        if (id >= MAX) {
            return;
        }
        combobox_selected[id] = static_cast<i8>(item);
        combobox_restore_label(id); // copies label, clears filtering
        combobox_hl[id]        = combobox_item_to_visible(id, item);
        pool[id].state         = 0;
        combobox_scroll_vel[id] = 0.0f;
        if (combobox_anim[id] < 0.999f) {
            combobox_shut_now(id);
        } else {
            combobox_closing[id] = true; // animate out (already picked a row)
        }
    }

    // Tooltip: `host` is the widget that reveals it, `text` a caller-owned
    // string ('\\n' makes extra lines). delay < 0 hides it again.
    // The default is a NAMED constant because alloc() has to write the same
    // value into a recycled slot: a leftover delay from the previous host is
    // invisible until the number happens to be an ugly one (a 9-second tooltip).
    static constexpr f32 K_TOOLTIP_DEFAULT_DELAY = 0.5f;
    // Keyboard step for a focused SLIDER, as a fraction of its 0..1 range. Named
    // because it is a tuning decision, not an implementation detail: it is the
    // only number standing between "a key press moves the value a useful amount"
    // and "a key press is invisible". 1% reaches any value by repeated presses and
    // crosses the whole track in 100.
    static constexpr f32 K_SLIDER_KEY_STEP = 0.01f;
    void  tooltip(u16 host, const char *text, f32 delay = K_TOOLTIP_DEFAULT_DELAY) noexcept;
    void  tooltip_hide(u16 host) noexcept;

    void  handle(const InputState &input) noexcept;
    // Plan B: time-driven widget state (press/thumb/hover/panel/cursor)
    // advances here. render() is pure draw (no dt, no state mutation) —
    // call update(dt) once per frame next to handle().
    void  update(f32 dt) noexcept;
    void  render(Renderer &r, SpriteBatch &batch) noexcept;
    f32 abs_x(u16 id) const noexcept; // screen x (resolves pos_mode chain)
    f32 abs_y(u16 id) const noexcept; // screen y (resolves pos_mode chain)
    void  set_position_mode(u16 id, mm_math::position_mode m) noexcept;
    void  set_anchor(u16 id, mm_math::anchor a) noexcept;

    // ── Container helpers ────────────────────────────────────────
    // There is deliberately no container BASE CLASS: the pool is a flat id
    // array (Widget stays 128B), so a base class would not remove a single
    // `switch (w.type)` and would add virtual dispatch + allocation rules we
    // do not want. What was actually missing is asking questions about the
    // tree in one place - these are that.
    // O(1): a PANEL is the container widget of this API (children are created
    // with `parent=`), plus anything that lays out children or clips them.
    bool is_container(u16 id) const noexcept {
        return id < MAX && (pool[id].type == (u8)widget_type::PANEL || layout_type[id] != 0 || (pool[id].flags & WF_CLIP));
    }

    // Direct children, submission order. Template (not std::function) to keep
    // the no-heap/no-std-function rule the family modules already follow.
    template <typename Fn> void for_each_child(u16 parent, Fn &&fn) const {
        for (u16 i = 0; i < count; ++i) {
            if (pool[i].parent == parent) {
                fn(i);
            }
        }
    }

    // Whole subtree, depth first. Depth-capped: a corrupted parent chain must
    // not hang the frame (same guard as resolve_abs).
    template <typename Fn> void for_each_descendant(u16 parent, Fn &&fn, u8 depth = 0) const {
        if (depth > 64) {
            return;
        }
        for (u16 i = 0; i < count; ++i) {
            if (pool[i].parent == parent) {
                fn(i);
                for_each_descendant(i, fn, static_cast<u8>(depth + 1));
            }
        }
    }

    u16 child_count(u16 parent) const noexcept {
        u16 n = 0;
        for (u16 i = 0; i < count; ++i) {
            if (pool[i].parent == parent) {
                ++n;
            }
        }
        return n;
    }

    // Effective state = own flag AND every ancestor's. DERIVED, not stored:
    // propagating flags downward cannot represent "re-enable one child of a
    // disabled panel", and the whole tree is a handful of levels deep.
    bool is_enabled(u16 id) const noexcept {
        for (u8 d = 0; id < MAX && d < 65; ++d) {
            if (!(pool[id].flags & WF_ENABLED)) {
                return false;
            }
            id = pool[id].parent;
        }
        return true;
    }
    bool is_visible(u16 id) const noexcept {
        for (u8 d = 0; id < MAX && d < 65; ++d) {
            if (!(pool[id].flags & WF_VISIBLE)) {
                return false;
            }
            id = pool[id].parent;
        }
        return true;
    }

    // Setters write THIS widget's flag only; use is_enabled()/is_visible() to
    // read the effective state. They exist so hosts stop poking pool[].flags.
    void set_enabled(u16 id, bool on) noexcept {
        if (id >= MAX) {
            return;
        }
        if (on) {
            pool[id].flags |= WF_ENABLED;
        } else {
            pool[id].flags &= static_cast<u8>(~WF_ENABLED);
        }
    }
    // ── Modal scope ──────────────────────────────────────────────
    // One active modal at a time (a nested dialog is a second Manager, not a
    // second scope). While set: the dim backdrop is drawn, ALL input is routed
    // to the subtree, Tab cycles inside it, and Back/Escape or a tap outside
    // fires `modal_dismiss` and ends the scope. Apps no longer hand-roll a
    // backdrop, an `if (overlay_open) return;` input guard and a dismiss path.
    u16    modal_id     = UINT16_MAX;
    ClickCallback modal_dismiss = nullptr;
    void       *modal_user   = nullptr;

    // True when `id` is the modal root or a descendant of it.
    bool in_modal(u16 id) const noexcept {
        for (u8 d = 0; id < MAX && d < 65; ++d) {
            if (id == modal_id) {
                return true;
            }
            id = pool[id].parent;
        }
        return modal_id == UINT16_MAX; // no modal: everything is "inside"
    }

    // Build the modal LAST: the dim backdrop is emitted where the subtree
    // starts in Pass 1, so anything built before it sits under the dim.
    void begin_modal(u16 root, ClickCallback on_dismiss = nullptr, void *user = nullptr) noexcept {
        if (root >= MAX) {
            return;
        }
        modal_id       = root;
        modal_dismiss  = on_dismiss;
        modal_user     = user;
        // Focus starts inside, so Tab has somewhere to go.
        if (focus_id == UINT16_MAX || !in_modal(focus_id)) {
            focus_id = find_next_focus(UINT16_MAX - 1);
            if (focus_id == UINT16_MAX) {
                focus_id = root;
            }
        }
    }
    void end_modal() noexcept {
        modal_id      = UINT16_MAX;
        modal_dismiss = nullptr;
        modal_user    = nullptr;
    }
    bool is_modal() const noexcept { return modal_id != UINT16_MAX; }

    // Topmost hit widget at a point. Public: apps legitimately want it (menus
    // that close on outside taps), and the tests need it to prove the modal
    // scope. Respects the modal subtree + effective state.
    //
    // Rebuilds the abs cache first (a no-op when clean): pick() reads cached
    // screen positions, so a hit test before the first handle() of the frame
    // would otherwise test stale rects - a footgun for a public query.
    // Effective clip rect for a widget: the intersection of EVERY clipping
    // ancestor's content rect. Public because it is a question a host legitimately
    // asks (its own overlay pass needs the same answer the engine's passes use),
    // and because "what will actually be painted" is not otherwise observable.
    bool     get_clip(u16 id, i16 &cx, i16 &cy, u16 &cw, u16 &ch) const noexcept;
    bool     nav_consumed = false; // set by handle(); read via nav_consumed_this_frame()

    u16 hit_test(f32 px, f32 py) noexcept {
        rebuild_abs_cache();
        return pick(px, py);
    }

    // ── Tooltip ──────────────────────────────────────────────────
    // Hover a widget for a moment and a panel with its text appears above it.
    // Owns nothing: the strings are caller-owned (same contract as
    // listbox_items), so a tooltip never copies or frees.
    u16               tooltip_for = UINT16_MAX; // widget currently hovered
    f32                  tooltip_t   = 0.0f;        // hover seconds accumulated
    const char            *tooltip_str[MAX];
    f32                  tooltip_delay[MAX];

    // ── Long text override ───────────────────────────────────────
    // Widget::text is a 48-byte inline buffer (Widget is locked at 128B), so a
    // long string used to be silently truncated mid-word by the factories'
    // strncpy. When text_ext[id] is set it IS the display string and the inline
    // buffer is ignored - same "caller owns the strings" contract as
    // listbox_items/combobox_items. Widget stays 128B and nothing is copied.
    const char           *text_ext[MAX];
    const char           *text_of(u16 id) const noexcept {
        const char *e = id < MAX ? text_ext[id] : nullptr;
        return e ? e : pool[id].text;
    }
    // Returns false (and changes nothing) when the string does not fit the
    // inline buffer: callers can then either shorten it or set_text_ext().
    bool set_text(u16 id, const char *s) noexcept;
    void set_text_ext(u16 id, const char *s) noexcept {
        if (id < MAX) {
            text_ext[id] = s;
            measure_dirty = true;
        }
    }

    // ── Separator + TabBar ──────────────────────────────────────
    // A themed rule. Neither factory takes a WidgetStyle: a separator is a
    // solid bar and a tab strip draws its own cells (see TabbarMetrics).
    u16 separator(f32 x, f32 y, f32 w, u16 parent, u32 color = 0, f32 thickness = 0.0f) noexcept {
        const u16 id = alloc();
        if (id == UINT16_MAX) {
            return id;
        }
        auto &wg       = pool[id];
        wg.frame.x     = x;
        wg.frame.y     = y;
        wg.frame.w     = w;
        wg.frame.h     = thickness > 0.0f ? thickness : theme.separator_thickness;
        wg.scale       = 1.0f;
        wg.bg_color    = color != 0 ? color : theme.separator_color;
        wg.text_color  = 0;
        wg.press_scale = 1.0f;
        wg.press_scale_target = 1.0f;
        wg.anim_t      = 1.0f; // never fades in: a rule that fades is a bug
        wg.thumb_pos   = 0.0f;
        wg.hover_factor = 0.0f;
        wg.text[0]     = '\0';
        wg.parent      = parent;
        wg.type        = (u8)widget_type::SEPARATOR;
        wg.flags       = WF_VISIBLE | WF_ENABLED; // NO auto size: the frame IS the rule
        wg.state       = 0;
        wg.style_id    = 0;
        wg.shape       = 0;
        wg.pos_mode    = mm_math::position_mode::RELATIVE;
        wg.anchor_pt   = mm_math::anchor::TOP_LEFT;
        wg.on_click    = nullptr;
        wg.on_draw     = nullptr;
        click_user[id] = nullptr;
        draw_user[id]  = nullptr;
        std::memset(wg.pad, 0, sizeof(wg.pad));
        align[id]      = theme.label_align;
        text_box[id]   = 0;
        return id;
    }
    // Vertical rule: `h` is the length, the width is the thickness.
    u16 separator_v(f32 x, f32 y, f32 h, u16 parent, u32 color = 0, f32 thickness = 0.0f) noexcept {
        const f32 th = thickness > 0.0f ? thickness : theme.separator_thickness;
        return separator(x, y, th, parent, color, h);
    }

    // Tab strip. The app owns the label array (same contract as
    // listbox_items) and swaps its own content when on_change fires.
    const char *const *tabbar_items[MAX];
    u8              tabbar_count_[MAX];
    i8               tabbar_active[MAX];
    TabbarMetrics        tabbar_metrics[MAX];
    u16 tabbar(f32 x, f32 y, f32 w, f32 h, const char *const *items, u8 count, u16 parent, int active = 0, ChangeCallback cb = nullptr) noexcept {
        const u16 id = alloc();
        if (id == UINT16_MAX) {
            return id;
        }
        auto &wg       = pool[id];
        wg.frame.x     = x;
        wg.frame.y     = y;
        wg.frame.w     = w;
        wg.frame.h     = h;
        wg.scale       = 1.0f; // UNUSED: the label scale is derived (TabbarMetrics)
        wg.bg_color    = theme.tab_bg;
        wg.text_color  = theme.tab_fg;
        wg.press_scale = 1.0f;
        wg.press_scale_target = 1.0f;
        wg.anim_t      = 1.0f;
        wg.thumb_pos   = 0.0f;
        wg.hover_factor = 0.0f;
        wg.text[0]     = '\0';
        wg.parent      = parent;
        wg.type        = (u8)widget_type::TABBAR;
        wg.flags       = WF_VISIBLE | WF_ENABLED | WF_FOCUSABLE;
        wg.state       = 0;
        wg.style_id    = 0;
        wg.shape       = 0;
        wg.pos_mode    = mm_math::position_mode::RELATIVE;
        wg.anchor_pt   = mm_math::anchor::TOP_LEFT;
        wg.on_click    = nullptr;
        wg.on_draw     = nullptr;
        click_user[id] = nullptr;
        draw_user[id]  = nullptr;
        std::memset(wg.pad, 0, sizeof(wg.pad));
        align[id]        = theme.label_align;
        text_box[id]     = 0;
        on_change[id]     = cb;
        tabbar_items[id]  = items;
        tabbar_count_[id] = count;
        tabbar_active[id] = static_cast<i8>(active < 0 ? 0 : (active >= static_cast<int>(count) ? static_cast<int>(count) - 1 : active));
        return id;
    }
    // ─── ScrollView ─────────────────────────────────────────────
    // A viewport for free-positioned children: their coordinates are the
    // CONTENT's, and the offset moves the whole subtree. Until this existed,
    // scrolling lived only inside LISTBOX and the COMBOBOX popup, and every
    // other scrolling surface was hand-rolled (freecell's solved-games browser
    // is 242 lines of exactly this).
    u16 scrollview(f32 x, f32 y, f32 w, f32 h, u16 parent, u8 style_id = 0) noexcept {
        const u16 id = alloc();
        if (id == UINT16_MAX) {
            return id;
        }
        auto &wg               = pool[id];
        wg.frame.x             = x;
        wg.frame.y             = y;
        wg.frame.w             = w;
        wg.frame.h             = h;
        wg.scale               = 1.0f;
        wg.bg_color            = theme.listbox_bg;
        wg.text_color          = 0;
        wg.press_scale         = 1.0f;
        wg.press_scale_target  = 1.0f;
        wg.anim_t              = 0.0f;
        wg.thumb_pos           = 0.0f;
        wg.hover_factor        = 0.0f;
        wg.text[0]             = '\0';
        wg.parent              = parent;
        wg.type                = (u8)widget_type::SCROLLVIEW;
        // WF_CLIP is what clips the children to the window, and it is also what
        // makes this a container. Not focusable: it has no selection of its own,
        // so there is nothing for Confirm to act on.
        wg.flags               = WF_VISIBLE | WF_ENABLED | WF_CLIP;
        wg.state               = 0;
        wg.style_id            = style_id;
        wg.shape               = 0;
        wg.pos_mode            = mm_math::position_mode::RELATIVE;
        wg.anchor_pt           = mm_math::anchor::TOP_LEFT;
        wg.on_click            = nullptr;
        wg.on_draw             = nullptr;
        click_user[id]         = nullptr;
        draw_user[id]          = nullptr;
        std::memset(wg.pad, 0, sizeof(wg.pad));
        align[id]              = theme.label_align;
        text_box[id]           = 0;
        scroll_scroll[id]      = 0.0f;
        scroll_content_h[id]   = 0.0f;
        scroll_anchor[id]      = 0.0f;
        measure_dirty          = true;
        abs_cache_dirty        = true;
        return id;
    }

    // Total scrollable height. Pure scroll_max() below is testable without a
    // Manager, which is why it is a static and not folded into set_scroll.
    void set_scroll_content_h(u16 id, f32 content_h) noexcept {
        if (id >= MAX) {
            return;
        }
        scroll_content_h[id] = content_h > 0.0f ? content_h : 0.0f;
        scroll_scroll[id]    = scroll_clamp(scroll_scroll[id], id);
        abs_cache_dirty      = true;
    }

    // Scrollbar geometry, pure so the tests can check it without a Manager.
    //
    // The width is a CONSTANT, deliberately not proportional to anything. The
    // ListBox derives its bar from ROW HEIGHT (row_h * 0.36), which is right for
    // it because row height tracks the font scale - and it produced a 94pt-wide
    // bar here, since the input was the 260pt VIEW height instead.
    static constexpr f32    K_SCROLL_BAR_W = 10.0f;

    // Thumb length, proportional to how much is visible - the standard "the
    // thumb is the content" read. Never shorter than a grabbable stub.
    static f32 scroll_thumb_h(f32 view_h, f32 max_scroll) noexcept {
        if (max_scroll <= 0.0f || view_h <= 0.0f) {
            return view_h;
        }
        const f32 total = view_h + max_scroll;
        f32       th    = view_h * (view_h / total);
        const f32 floor_h = 24.0f;
        return th < floor_h ? (floor_h < view_h ? floor_h : view_h) : th;
    }

    // Largest legal offset: the classic "no scrollbar when it all fits".
    static f32 scroll_max(f32 content_h, f32 view_h) noexcept {
        const f32 over = content_h - view_h;
        return over > 0.0f ? over : 0.0f;
    }

    f32 scroll_max(u16 id) const noexcept {
        return id < MAX ? scroll_max(scroll_content_h[id], pool[id].frame.h) : 0.0f;
    }

    f32 scroll_clamp(f32 v, u16 id) const noexcept {
        const f32 mx = scroll_max(id);
        if (v < 0.0f) {
            return 0.0f;
        }
        return v > mx ? mx : v;
    }

    f32 get_scroll(u16 id) const noexcept {
        return id < MAX ? scroll_scroll[id] : 0.0f;
    }

    // Any scroll change re-runs the abs cache: that is what makes the subtree
    // move, so skipping it would look like the setter did nothing.
    void set_scroll(u16 id, f32 v) noexcept {
        if (id >= MAX) {
            return;
        }
        const f32 c = scroll_clamp(v, id);
        if (c != scroll_scroll[id]) {
            scroll_scroll[id] = c;
            abs_cache_dirty   = true;
        }
    }

    // Scroll a focused descendant into view - the same job the ListBox does for
    // its own keyboard navigation, generalised to any widget inside any viewport.
    void scroll_reveal(u16 id, f32 top, f32 bottom) noexcept {
        if (id >= MAX || bottom <= top) {
            return;
        }
        const u16 sv = scroll_ancestor_of(id);
        if (sv == UINT16_MAX) {
            return;
        }
        const f32 view_top = abs_y(sv);
        const f32 view_h   = pool[sv].frame.h;
        const f32 cur      = scroll_scroll[sv];
        // `top`/`bottom` are absolute y (the caller already added the offset).
        if (top < view_top) {
            set_scroll(sv, cur - (view_top - top));
        } else if (bottom > view_top + view_h) {
            set_scroll(sv, cur + (bottom - (view_top + view_h)));
        }
    }

    // Nearest SCROLLVIEW ancestor, or UINT16_MAX.
    u16 scroll_ancestor_of(u16 id) const noexcept {
        u16 p = id < MAX ? pool[id].parent : UINT16_MAX;
        u8  d = 0;
        while (p != UINT16_MAX && p < MAX && d < 65) {
            if (pool[p].type == (u8)widget_type::SCROLLVIEW) {
                return p;
            }
            p = pool[p].parent;
            ++d;
        }
        return UINT16_MAX;
    }

    void set_tab(u16 id, int index) noexcept {
        if (id < MAX && index >= 0 && index < static_cast<int>(tabbar_count_[id])) {
            tabbar_active[id] = static_cast<i8>(index);
        }
    }
    int get_tab(u16 id) const noexcept {
        return id < MAX ? tabbar_active[id] : -1;
    }
    // Pure geometry: which cell a local x falls in (-1 = outside). Shared by
    // the tap handler and the tests; the render path never inverts it.
    static int tabbar_cell_at(f32 frame_w, u8 count, f32 local_x) noexcept {
        if (count == 0 || frame_w <= 0.0f || local_x < 0.0f || local_x > frame_w) {
            return -1;
        }
        const f32 cw = frame_w / static_cast<f32>(count);
        int         i  = static_cast<int>(local_x / cw);
        return i >= static_cast<int>(count) ? static_cast<int>(count) - 1 : i;
    }

    // Horizontal text alignment. The member is `align` (not `text_align`) so
    // the type stays usable unqualified inside Manager, same trick as
    // layout_align's member.
    text_align                 align[MAX];
    // The box width the app pinned with set_text_align(), or 0 when the frame
    // is the widget's own. This is what tells a PINNED box from an AUTO_W one:
    // `align` alone cannot, because LEFT is both "the app chose left" and "the
    // app never asked". Without it a left-aligned label could never be
    // ellipsised - a pinned box is the text's limit whatever the alignment.
    u16                   text_box[MAX];
    // SCROLLVIEW: the scroll offset is applied in rebuild_abs_cache(), not in
    // the draw pass, because draw AND hit-testing both read the abs cache -
    // offsetting only the draw would leave taps landing where the widget is not.
    // Direct (no target/velocity): matches ListBox. Smoothing is the ComboBox's
    // pattern if it is ever wanted here.
    f32                      scroll_scroll[MAX];   // drawn offset, px
    f32                      scroll_content_h[MAX]; // total scrollable height
    f32                      scroll_anchor[MAX];   // offset at press, for a drag
    // TreeView state, all on a LISTBOX (see the TreeNode docs above). The
    // expanded set is a bitset, not a byte per node: 4KB for 256 nodes per
    // widget instead of 32KB. Nodes are caller-owned and only referenced.
    // Accordion: drawn height (animated) and target height per section. The
    // header is `id` itself - an accordion IS a button whose content is whatever
    // the app registered with accordion_attach().
    f32                      acc_drawn_h[MAX];
    f32                      acc_target_h[MAX];
    f32                      acc_content_h[MAX];
    u16                   acc_content[MAX]; // the registered content widget
    const TreeNode            *tree_nodes[MAX];
    u32                   tree_count[MAX];
    u64                   tree_expanded[MAX][4]; // 256 nodes per widget
    // DataGrid state, also on a LISTBOX. The column model and the row ORDER are
    // caller-owned POINTERS, not copies: the grid owns the header, the column
    // geometry and the sort STATE, but WHICH model row is visible at position k
    // is the app's to decide - a date column, a numeric key or a custom
    // tie-break is not something the engine can know from a `const char *`, and
    // owning the permutation anyway would cost 255 bytes per widget.
    const GridColumn          *grid_cols[MAX];
    const u8            *grid_order[MAX]; // model row per visible row, or nullptr
    u8                   grid_col_count[MAX];
    f32                     grid_header_h[MAX]; // one row tall, derived in the metrics pass
    u8                   grid_sort_col[MAX];
    grid_sort                 grid_sort_dir[MAX];
    GridMetrics               grid_metrics[MAX];
    // The text a TEXT_FIELD held when editing began, so Escape can revert.
    // Snapshotted at both entry points (tap and Confirm). A parallel array like
    // every other piece of per-widget state here - no heap.
    char                      edit_snapshot[MAX][48];
    // Pin the widget's width to `box_w` and align the text inside it. A label
    // is AUTO_W/AUTO_H by default, so an unpinned box hugs the text and every
    // alignment collapses to LEFT - a centred heading NEEDS a pinned width.
    // box_w = 0 unpins, restoring the frame as the box.
    void set_text_align(u16 id, text_align a, f32 box_w = 0.0f) noexcept {
        if (id < MAX) {
            align[id] = a;
            if (box_w > 0.0f) {
                pool[id].frame.w = box_w;
                pool[id].flags   = static_cast<u8>(pool[id].flags & ~WF_AUTO_W);
                text_box[id]     = static_cast<u16>(box_w);
                measure_dirty     = true;
            } else {
                text_box[id] = 0;
            }
        }
    }
    // True when the last handle() consumed an Up/Down nav action because a
    // FOCUSED widget owns it (ListBox/ComboBox/TabBar selection, or a text
    // field's caret). The engine knows it ate the key but had no way to SAY so,
    // so a host binding Up/Down to something else - a live tuner, a zoom - also
    // fired on every press. mm_07 hit exactly that: "Up/Dn moves selection" on
    // the listbox page was also shrinking the global font 0.1 per press, and
    // the shrunken text survived every page switch, which read as "the listbox
    // lost its text". Check it AFTER handle() (the flag is per-frame).
    bool nav_consumed_this_frame() const noexcept {
        return nav_consumed;
    }
    text_align get_text_align(u16 id) const noexcept {
        return id < MAX ? align[id] : text_align::LEFT;
    }

    // ListBox custom row contents (nullptr = the default one-string row).
    RowCallback              listbox_row_cb[MAX];
    void                    *listbox_row_user[MAX];
    // Value-label placement + scale, shared by SLIDER and PROGRESSBAR so the
    // rule cannot drift apart. Definitions follow the class.
    f32 value_label_x(u16 id, f32 ax, f32 frame_w, f32 text_w, f32 outside_gap = 12.0f,
                        f32 inside_inset = 6.0f) const noexcept;
    f32 value_label_scale(const Renderer &r, f32 frame_h, f32 pad_t, f32 pad_b) const noexcept;

    // Re-derive the listbox's item count from the model and keep its selection
    // and scroll legal. Called on attach and on every expand/collapse: a closed
    // node can REMOVE the selected row, and a selection index past the end is how
    // a listbox ends up rendering a highlight for a row that is not there.
    void tree_sync(u16 id) noexcept {
        if (id >= MAX || tree_nodes[id] == nullptr) {
            return;
        }
        const u32 vis = tree_visible_count(tree_nodes[id], tree_count[id], tree_expanded[id]);
        listbox_count[id] = static_cast<u8>(vis > 255 ? 255 : vis);
        if (listbox_selected[id] >= static_cast<i8>(listbox_count[id])) {
            listbox_selected[id] = static_cast<i8>(listbox_count[id] > 0 ? listbox_count[id] - 1 : -1);
        }
        ui::listbox::clamp_scroll(listbox_scroll[id], listbox_count[id], listbox_metrics[id]);
    }

    // ─── Accordion ────────────────────────────────────────────────
    //
    // `id` is the HEADER BUTTON; the content is a separate widget the app
    // registers. Keeping the content a normal widget (rather than a child list
    // the engine owns) means its own children, scroll views and row renderers
    // work inside a section with no special cases.
    bool is_accordion(u16 id) const noexcept {
        return id < MAX && acc_content[id] != UINT16_MAX;
    }

    // The content's frame is RELATIVE TO THE HEADER - author it as
    // (x, header_h). attach() reparents it, and a frame authored against the
    // content's original parent would silently shift by the difference.
    void accordion_attach(u16 id, u16 content_id, f32 content_h, bool open = false) noexcept {
        if (id >= MAX) {
            return;
        }
        acc_content[id]    = content_id;
        acc_content_h[id]  = content_h > 0.0f ? content_h : 0.0f;
        acc_target_h[id]   = open ? acc_content_h[id] : 0.0f;
        // Attach lands SETTLED, not at 0: a section that fades in from nothing on
        // the frame it is built reads as a glitch, not an animation.
        acc_drawn_h[id] = acc_target_h[id];
        if (content_id != UINT16_MAX) {
            // The content becomes a CHILD of the header and a clipping container.
            // That is what makes the collapse work everywhere at once: every
            // render pass resolves its clip through get_clip(), so clamping the
            // content's rect THERE clips the content, its children, its scroll
            // view and its rows - with no per-pass code at all. It also means the
            // engine never has to mutate the app's own frame height.
            pool[content_id].parent = id;
            // BOTH the header and the content clip, because get_clip() returns
            // only the FIRST clipping ancestor:
            //   header  -> clips the CONTENT'S OWN background to the drawn band
            //   content -> clips the content's children (and its scroll view)
            // With only the content clipping, the panel's own fill drew at full
            // height on a "closed" section. With only the header clipping, a
            // ScrollView inside would report its full viewport as visible.
            pool[id].flags = static_cast<u8>(pool[id].flags | WF_CLIP);
            pool[content_id].flags            = static_cast<u8>(pool[content_id].flags | WF_CLIP);
            set_visible(content_id, true);
            abs_cache_dirty                   = true;
        }
    }

    // Which accordion section owns this widget, or UINT16_MAX. The header is the
    // parent, so a grandchild needs one step up.
    u16 accordion_parent_of(u16 id) const noexcept {
        if (id >= MAX) {
            return UINT16_MAX;
        }
        const u16 p = pool[id].parent;
        return (p < MAX && acc_content[p] != UINT16_MAX) ? p : UINT16_MAX;
    }

    bool accordion_is_open(u16 id) const noexcept {
        return id < MAX && acc_target_h[id] > 0.0f;
    }

    // True once the animation has landed, which is when a caller may stop
    // thinking about the section. Distinct from "is open": a closing section is
    // no longer open but is not settled either.
    bool accordion_is_settled(u16 id) const noexcept {
        return id < MAX && acc_drawn_h[id] == acc_target_h[id];
    }

    void accordion_toggle(u16 id) noexcept {
        if (id >= MAX || acc_content[id] == UINT16_MAX) {
            return;
        }
        accordion_set_open(id, !accordion_is_open(id));
    }

    void accordion_set_open(u16 id, bool open) noexcept {
        if (id >= MAX || acc_content[id] == UINT16_MAX) {
            return;
        }
        const f32 target = open ? acc_content_h[id] : 0.0f;
        if (target != acc_target_h[id]) {
            acc_target_h[id] = target;
            // Hiding the content the instant it starts closing would make the
            // animation pointless - it is VISIBLE=false only once there is
            // nothing left of it to see.
            if (!open) {
                set_visible(id, true); // the header stays
            } else {
                // ...and REOPENING has to bring it back. update() clears the flag
                // when a section settles closed and nothing else ever set it, so
                // a reopened section animated its height with nothing inside it:
                // "closes fine, will not open again".
                set_visible(acc_content[id], true);
            }
            abs_cache_dirty = true;
        }
    }

    f32 accordion_drawn_h(u16 id) const noexcept {
        return id < MAX ? acc_drawn_h[id] : 0.0f;
    }

    f32 accordion_content_h(u16 id) const noexcept {
        return id < MAX ? acc_content_h[id] : 0.0f;
    }

    // ─── TreeView (a LISTBOX with a node model) ───────────────────
    //
    // Attaching a model REPLACES the listbox's item count with the number of
    // VISIBLE rows, and keeps it in step as nodes open and close. Roots are open
    // by default; everything else starts closed, which is the convention users
    // expect and means the first frame is not a wall of 200 rows.
    void set_tree(u16 id, const TreeNode *nodes, u32 count) noexcept {
        if (id >= MAX) {
            return;
        }
        tree_nodes[id] = nodes;
        tree_count[id] = (nodes != nullptr && count > 0) ? count : 0;
        tree_sync(id);
    }

    bool is_tree(u16 id) const noexcept {
        return id < MAX && tree_nodes[id] != nullptr;
    }

    bool tree_has_children(u16 id, u32 node) const noexcept {
        return id < MAX && node < tree_count[id] && (tree_nodes[id][node].flags & TREE_HAS_CHILDREN) != 0;
    }

    bool tree_is_expanded(u16 id, u32 node) const noexcept {
        return id < MAX && node < tree_count[id] && tree_bit(tree_expanded[id], node);
    }

    void tree_set_expanded(u16 id, u32 node, bool on) noexcept {
        if (id >= MAX || node >= tree_count[id]) {
            return;
        }
        const u64 bit = 1ull << (node & 63);
        if (on) {
            tree_expanded[id][node >> 6] |= bit;
        } else {
            tree_expanded[id][node >> 6] &= ~bit;
        }
        tree_sync(id);
    }

    // Scratch size for the flatten: a visible listbox cannot be longer than this
    // (a listbox's item count is a u8), so 64 is generous.
    static constexpr u32 K_TREE_ROW_SCRATCH = 256; // matches the uint8 listbox count, not 64

    // The expander zone: the K_TREE_EXPANDER_W strip at the indent column of a
    // row. Pure geometry on a row + local x, so it is testable without input.
    static constexpr f32    K_TREE_EXPANDER_W = 16.0f;
    static constexpr f32    K_TREE_INDENT_W    = 18.0f;

    static f32 tree_expander_x(u32 depth) noexcept {
        return static_cast<f32>(depth) * K_TREE_INDENT_W + 4.0f;
    }

    // Is (local_x, local_y) inside a row's expander strip? local coordinates are
    // relative to the LISTBOX's content box.
    static bool tree_hit_expander(u32 depth, bool has_children, f32 local_x, f32 row_h) noexcept {
        if (!has_children || row_h <= 0.0f) {
            return false;
        }
        const f32 x0 = tree_expander_x(depth);
        return local_x >= x0 && local_x <= x0 + K_TREE_EXPANDER_W;
    }

    // Same test in absolute coordinates, against a row's own y span.
    bool tree_tap_is_expander(u16 id, f32 px, f32 py) const noexcept {
        if (id >= MAX || tree_nodes[id] == nullptr || listbox_metrics[id].row_h <= 0.0f) {
            return false;
        }
        f32 il = 0.0f, it = 0.0f, ir = 0.0f, ib = 0.0f;
        content_insets(styles[pool[id].style_id], pool[id].frame.w, pool[id].frame.h, il, it, ir, ib);
        const f32      top = abs_y(id) + it;
        const f32      row_h = listbox_metrics[id].row_h;
        const f32      rel_y = py - top + listbox_scroll[id];
        const u32   row   = rel_y > 0.0f ? static_cast<u32>(rel_y / row_h) : 0u;
        if (row >= listbox_count[id]) {
            return false;
        }
        u32 nodes[K_TREE_ROW_SCRATCH];
        const u32 node = tree_row_node(id, row, nodes);
        if (node == UINT32_MAX) {
            return false;
        }
        return tree_hit_expander(tree_row_depth(id, row, nodes), tree_has_children(id, node), px - (abs_x(id) + il), row_h);
    }

    // Toggling a LEAF is a silent no-op, like a radiobox re-tap: the affordance
    // is not there, so the click should not appear to do nothing visibly.
    void tree_toggle(u16 id, u32 node) noexcept {
        if (id >= MAX || node >= tree_count[id] || !tree_has_children(id, node)) {
            return;
        }
        tree_set_expanded(id, node, !tree_is_expanded(id, node));
    }

    u32 tree_row_count(u16 id) const noexcept {
        return id < MAX ? listbox_count[id] : 0;
    }

    // Row index in the listbox -> node index in the model. `rows` is scratch,
    // caller-owned (64 nodes covers a visible listbox); returns UINT32_MAX when
    // the row is out of range.
    u32 tree_row_node(u16 id, u32 row, u32 *rows) const noexcept {
        if (id >= MAX || row >= listbox_count[id]) {
            return UINT32_MAX;
        }
        const u32 n = tree_flatten(tree_nodes[id], tree_count[id], tree_expanded[id], rows, K_TREE_ROW_SCRATCH);
        return row < n ? rows[row] : UINT32_MAX;
    }

    // Node -> row, or UINT32_MAX when the node is hidden (or unknown). Used to
    // scroll a node into view when it is expanded programmatically.
    u32 tree_node_row(u16 id, u32 node, u32 *rows) const noexcept {
        if (id >= MAX || node >= tree_count[id]) {
            return UINT32_MAX;
        }
        const u32 n = tree_flatten(tree_nodes[id], tree_count[id], tree_expanded[id], rows, K_TREE_ROW_SCRATCH);
        for (u32 r = 0; r < n; ++r) {
            if (rows[r] == node) {
                return r;
            }
        }
        return UINT32_MAX;
    }

    u32 tree_row_depth(u16 id, u32 row, u32 *rows) const noexcept {
        u32 node = tree_row_node(id, row, rows);
        if (node == UINT32_MAX) {
            return 0;
        }
        // Depth is derived from the parent chain rather than stored, so an app
        // cannot build a model whose depths disagree with its structure.
        u32 d = 0;
        while (tree_nodes[id][node].parent >= 0 && d < 64) {
            node = static_cast<u32>(tree_nodes[id][node].parent);
            ++d;
        }
        return d;
    }

    // ─── DataGrid ──────────────────────────────────────────────────
    //
    // A grid IS a listbox - scroll, selection, keyboard and the row renderer
    // are the listbox's, for the same reason a tree is one. What it adds is the
    // HEADER and the sort state; everything else stays where it was.
    bool is_grid(u16 id) const noexcept {
        return id < MAX && grid_cols[id] != nullptr && grid_col_count[id] > 0;
    }

    // Column model. The header height is DERIVED (one row) in the metrics pass
    // rather than passed: a table header IS a header row, and the row height is
    // already the metric that decides it. Widths are re-fit on every metrics
    // refresh, so a resized grid re-flows its columns.
    void grid_attach(u16 id, const GridColumn *cols, u8 col_count) noexcept {
        if (id >= MAX) {
            return;
        }
        grid_cols[id]      = cols;
        grid_col_count[id] = col_count < MAX_GRID_COLS ? col_count : MAX_GRID_COLS;
        grid_order[id]     = nullptr;
        grid_sort_col[id]  = UINT8_MAX;
        grid_sort_dir[id]  = grid_sort::NONE;
        // Seed the geometry from the current frame so the header is hittable
        // before the first render; the metrics pass re-fits it on every frame,
        // so this only has to be right once.
        grid_metrics[id]   = compute_grid_metrics(grid_cols[id], grid_col_count[id], pool[id].frame.w);
    }

    // Which MODEL row a visible row shows. Identity with no order array, so a
    // grid that never sorts pays nothing.
    u32 grid_row_source(u16 id, u32 visible) const noexcept {
        if (id >= MAX) {
            return visible;
        }
        const u8 *o = grid_order[id];
        return o != nullptr ? o[visible] : visible;
    }
    void set_grid_order(u16 id, const u8 *order) noexcept {
        if (id < MAX) {
            grid_order[id] = order;
        }
    }

    grid_sort grid_sort_of(u16 id, u8 col) const noexcept {
        return id < MAX && grid_sort_col[id] == col ? grid_sort_dir[id] : grid_sort::NONE;
    }
    // The sort column the header highlights, or UINT8_MAX when unsorted.
    u8 grid_sorted_col(u16 id) const noexcept {
        return id < MAX ? grid_sort_col[id] : UINT8_MAX;
    }

    // A header tap sets the state; the APP re-sorts and calls set_grid_order
    // (grid_sort_order is the one-liner for the common case). Splitting it this
    // way is what lets a grid sort by something a `const char *` cannot express.
    void grid_toggle_sort(u16 id, u8 col) noexcept {
        if (!is_grid(id) || col >= grid_col_count[id] || !grid_cols[id][col].sortable) {
            return;
        }
        const grid_sort next = grid_tap_cycle(grid_sort_of(id, col));
        grid_sort_dir[id]     = next;
        grid_sort_col[id]     = col;
    }

    // Column under a pointer x RELATIVE to the widget's left edge.
    bool grid_header_at(u16 id, f32 local_x, u8 &col) const noexcept {
        if (!is_grid(id)) {
            return false;
        }
        const GridMetrics &gm = grid_metrics[id];
        for (u8 i = 0; i < gm.columns; ++i) {
            if (local_x >= gm.x[i] && local_x < gm.x[i] + gm.w[i]) {
                col = i;
                return true;
            }
        }
        return false;
    }

    // Top of the ROW area, in absolute y. This is the SINGLE definition every
    // row site uses (Pass 1.8 highlight + scrollbar, Pass 3 rows, the tap path),
    // so the header cannot drift out of alignment with the rows - which is what
    // three hand-copied subtractions guarantee eventually. 0 for a plain listbox,
    // so every row site is unchanged when no grid is present.
    f32 row_area_top(u16 id, f32 content_y) const noexcept {
        return content_y + (id < MAX ? grid_header_h[id] : 0.0f);
    }

    // ─── Row renderer ──────────────────────────────────────────────
    void set_row_renderer(u16 id, RowCallback cb, void *user = nullptr) noexcept {
        if (id < MAX) {
            listbox_row_cb[id]   = cb;
            listbox_row_user[id] = user;
        }
    }

    // Which animation a popup uses when it opens/closes.
    void set_popup_anim(u16 id, popup_anim_mode mode) noexcept {
        if (id < MAX) {
            combobox_anim_mode[id] = mode;
        }
    }
    popup_anim_mode get_popup_anim(u16 id) const noexcept {
        return id < MAX ? combobox_anim_mode[id] : popup_anim_mode::SCALE_FADE;
    }

    // WF_CLIP: clip children to this widget's content rect. Setter exists so
    // hosts stop poking pool[].flags.
    void set_clip(u16 id, bool on) noexcept {
        if (id >= MAX) {
            return;
        }
        if (on) {
            pool[id].flags |= WF_CLIP;
        } else {
            pool[id].flags &= static_cast<u8>(~WF_CLIP);
        }
    }

    // WF_FOCUSABLE: whether Tab/Shift-Tab can land on this widget. Every
    // interactive factory sets it (buttons included) so the whole toolkit is
    // keyboard-reachable; clear it for a decorative hit target or an app that
    // drives its own key map and does not want the Tab order to change.
    void set_focusable(u16 id, bool on) noexcept {
        if (id >= MAX) {
            return;
        }
        if (on) {
            pool[id].flags |= WF_FOCUSABLE;
        } else {
            pool[id].flags &= static_cast<u8>(~WF_FOCUSABLE);
            // Never leave focus parked on a widget that just left the ring.
            if (focus_id == id) {
                focus_id = UINT16_MAX;
            }
        }
    }

    bool is_focusable(u16 id) const noexcept {
        return id < MAX && (pool[id].flags & WF_FOCUSABLE) != 0;
    }

    // Which widget the pointer is over this frame (UINT16_MAX for none). Public
    // because "no pointer at all" is a real state - see InputState::has_pointer
    // - and the hover colour, the toggle thumb's hot shade and the tooltip are
    // all driven off this one id, so a test needs to read it directly.
    bool is_hot(u16 id) const noexcept {
        return id < MAX && hot == id;
    }

    void set_visible(u16 id, bool on) noexcept {
        if (id >= MAX) {
            return;
        }
        if (on) {
            if (!(pool[id].flags & WF_VISIBLE)) {
                // Showing something RE-DIRTIES measure(). An AUTO widget's frame
                // is only assigned for a visible widget, and the metrics of a
                // hidden one are measured but not sized - so a widget that was
                // hidden while a measure pass ran has a stale frame, and without
                // this the next pass early-returns and the stale size is drawn.
                //
                // The subtree matters, not just this widget: set_visible on a
                // container reveals its descendants too, and a one-shot dirty flag
                // is cheaper than tracking which of them were skipped.
                measure_dirty = true;
            }
            pool[id].flags |= WF_VISIBLE;
        } else {
            pool[id].flags &= static_cast<u8>(~WF_VISIBLE);
        }
    }
  private:
    u16      alloc() noexcept;
    void          rebuild_abs_cache() noexcept;
    mm_math::rect resolve_abs(u16 id, u8 depth) noexcept;
    f32         scroll_offset_for(u16 id, u8 depth) const noexcept;
    void          layout_children(u16 parent_id) noexcept; // runtime dispatch on HBox/VBox
    template <u8 Axis>                                     // 0 = horizontal flow, 1 = vertical flow
    void     layout_children_axis(u16 parent_id) noexcept;
    void     layout_subtree(u16 id) noexcept; // layout container + nested containers below it
    u16 pick(f32 px, f32 py) const noexcept;
    void     reveal_focused() noexcept;
    bool     is_finger_down(const InputState &input) const noexcept;


    u16 find_next_focus(u16 from) const noexcept;
    u16 find_prev_focus(u16 from) const noexcept;
};

// ─── Color helpers (engine format: 0xAARRGGBB) ──────────────────
MM_FORCE_INLINE static u32 ui_lighten(u32 color, u8 amount) noexcept {
    u32 a = color & 0xFF000000;
    u32 b = (u32)((color & 0xFF) + amount);
    if (b > 255) {
        b = 255;
    }
    u32 g = (u32)(((color >> 8) & 0xFF) + amount);
    if (g > 255) {
        g = 255;
    }
    u32 r = (u32)(((color >> 16) & 0xFF) + amount);
    if (r > 255) {
        r = 255;
    }
    return a | (r << 16) | (g << 8) | b;
}

MM_FORCE_INLINE static u32 ui_lerp_color(u32 a, u32 b, f32 t) noexcept {
    // Packed engine colors (0xAARRGGBB) — same per-channel math as before,
    // now explicit about the packing.
    return mm_math::color::from_u32_argb(a).lerp(mm_math::color::from_u32_argb(b), t).to_u32_argb();
}

// Multiply a packed color's alpha. The sprite pipeline already honours the
// alpha byte (the panel fade relies on it), so a fade costs nothing extra.
MM_FORCE_INLINE static u32 ui_alpha(u32 color, f32 mul) noexcept {
    if (mul >= 1.0f) {
        return color;
    }
    if (mul <= 0.0f) {
        return color & 0x00FFFFFF;
    }
    mm_math::color c = mm_math::color::from_u32_argb(color);
    return c.with_alpha(c.a * mul).to_u32_argb();
}

MM_FORCE_INLINE static u32 ui_darken(u32 color, u8 amount) noexcept {
    u32 a = color & 0xFF000000;
    u32 b = (u32)((int)(color & 0xFF) - (int)amount);
    if (b > 255) {
        b = 0;
    }
    u32 g = (u32)((int)(((color >> 8) & 0xFF)) - (int)amount);
    if (g > 255) {
        g = 0;
    }
    u32 r = (u32)((int)(((color >> 16) & 0xFF)) - (int)amount);
    if (r > 255) {
        r = 0;
    }
    return a | (r << 16) | (g << 8) | b;
}

inline Theme Theme::dark() noexcept {
    Theme t{};
    // format: 0xAARRGGBB
    t.panel_bg         = palette::PANEL;
    t.button_bg        = palette::BUTTON_BG;
    t.button_text      = palette::WHITE;
    t.toggle_track_on  = palette::TOGGLE_GREEN;
    t.toggle_track_off = palette::GRAY_DARK;
    t.toggle_thumb     = 0xFFF0F0FF;
    t.toggle_thumb_hot = 0xFFEECCFF;
    t.toggle_text      = palette::WHITE;
    t.slider_bg        = palette::GRAY_DARK;
    t.slider_track     = 0xFF333333;
    t.slider_fill      = 0xFF88FF44;
    t.slider_thumb     = 0xFFAAFF66;
    t.slider_thumb_hot = 0xFFAAFF88;
    t.slider_text      = palette::GREEN_SOFT;
    t.progressbar_bg    = palette::GRAY_DARK;
    t.progressbar_track = 0xFF333333;
    t.progressbar_fill  = 0xFF88FF44;
    t.checkbox_on      = palette::TOGGLE_GREEN;
    t.checkbox_off     = palette::GRAY_DARK;
    t.checkbox_border  = palette::GRAY_MID;
    t.checkbox_check   = palette::WHITE;
    t.checkbox_text    = palette::WHITE;
    t.listbox_bg       = 0xFF242424;
    t.listbox_sel      = 0xFF3A6EA5;
    t.listbox_text     = palette::WHITE;
    t.textfield_bg     = palette::PANEL;
    t.textfield_text   = palette::WHITE;
    t.cursor           = palette::WHITE;
    t.focus_color      = 0xFF88FF44;
    t.modal_backdrop  = 0x99000000;
    t.text_primary     = palette::WHITE;
    t.text_secondary   = palette::GRAY_LIGHT;

    t.panel_pad[0] = t.panel_pad[1] = t.panel_pad[2] = t.panel_pad[3] = 0;
    t.label_pad[0] = t.label_pad[1] = t.label_pad[2] = t.label_pad[3] = 0;
    t.label_align = text_align::LEFT;
    t.separator_color     = 0x40FFFFFF;
    t.separator_thickness = 2.0f;
    t.tab_bg              = 0xFF1E1E1E;
    t.tab_active_bg       = 0xFF4A4A4A;
    t.tab_active_fg       = palette::WHITE;
    t.tab_fg              = palette::GRAY_LIGHT;
    // A label is a horizontal strip, so the vertical gap is what reads as
    // "cramped" - and the row height is the axis apps squeeze hardest (freecell
    // packs 8-9 menu rows into half a window). Give top/bottom more than the
    // sides; the label is centred on its ink box, so this is pure breathing room.
    t.button_pad[0] = 8; // top
    t.button_pad[1] = 6; // right
    t.button_pad[2] = 8; // bottom
    t.button_pad[3] = 6; // left
    t.toggle_pad[0] = t.toggle_pad[1] = t.toggle_pad[2] = t.toggle_pad[3] = 4;
    t.slider_pad[0] = t.slider_pad[1] = t.slider_pad[2] = t.slider_pad[3] = 4;
    t.checkbox_pad[0] = t.checkbox_pad[1] = t.checkbox_pad[2] = t.checkbox_pad[3] = 4;
    t.textfield_pad[0] = t.textfield_pad[1] = t.textfield_pad[2] = t.textfield_pad[3] = 6;
    t.listbox_pad[0] = t.listbox_pad[1] = t.listbox_pad[2] = t.listbox_pad[3] = 0;

    t.layout_padding                                                          = 8;
    t.layout_spacing                                                          = 4;

    t.font_path[0]                                                            = '\0';
    t.font_scale                                                              = 1.0f;
    return t;
}

// ─── Inline implementations ──────────────────────────────────────
inline void Manager::init() noexcept {
    count          = 0;
    freelist_count = 0;
    hot            = UINT16_MAX;
    active         = UINT16_MAX;
    clicked        = UINT16_MAX;
    focus_id       = UINT16_MAX;
    focus_anchor   = UINT16_MAX;
    editing_id     = UINT16_MAX;
    scroll_dragging = false;
    cursor_timer   = 0.0f;
    event_head = event_tail = event_count = 0;
    prev_focus                            = UINT16_MAX;
    prev_hot                              = UINT16_MAX;
    theme                                 = Theme::dark();
    style_count                           = 0;
    register_style({TextureHandle::invalid(), 0, 0.0f, 0.0f, shape_type::ROUNDED_RECT, {}, {}, {}, 0, 0.0f});
    measure_dirty   = true;
    abs_cache_dirty = true;
    for (u16 i = 0; i < MAX; ++i) {
        widget_material[i].pipeline = PipelineHandle::invalid();
        layout_type[i]              = 0;
        layout_pad[i]               = 0;
        layout_spacing[i]           = 0;
        layout_align[i]             = 0;
        size_pct_w[i]               = 0;
        size_pct_h[i]               = 0;
        margin[i][0] = margin[i][1] = margin[i][2] = margin[i][3] = 0;
        cursor_pos[i]                                             = 0;
        listbox_items[i]                                          = nullptr;
        listbox_count[i]                                          = 0;
        listbox_selected[i]                                       = -1;
        listbox_scroll[i]                                         = 0.0f;
        listbox_anchor[i]                                         = 0.0f;
        listbox_metrics[i]                                        = ListboxMetrics{};
        combobox_items[i]                                         = nullptr;
        combobox_count[i]                                         = 0;
        combobox_selected[i]                                      = -1;
        combobox_scroll[i]                                        = 0.0f;
        combobox_scroll_target[i]                                = 0.0f;
        combobox_scroll_vel[i]                                   = 0.0f;
        combobox_anim[i]                                         = 0.0f;
        combobox_anim_mode[i]                                     = popup_anim_mode::SCALE_FADE;
        combobox_closing[i]                                      = false;
        combobox_anchor[i]                                        = 0.0f;
        combobox_open[i]                                          = false;
        combobox_editable[i]                                      = false;
        combobox_filtering[i]                                     = false;
        combobox_hl[i]                                            = -1;
        combobox_metrics[i]                                        = ListboxMetrics{};
        progressbar_value[i]                                       = 0.0f;
        progressbar_target[i]                                      = 0.0f;
        progressbar_burst[i]                                       = -1.0f;
        progressbar_stripe_t[i]                                     = 0.0f;
        progressbar_celebrated[i]                                  = false;
        on_color[i]                                               = 0;
        text_ext[i]                                             = nullptr;
        listbox_row_cb[i]                                        = nullptr;
        align[i]                                                 = text_align::LEFT;
        text_box[i]                                              = 0;
        scroll_scroll[i]                                         = 0.0f;
        scroll_content_h[i]                                      = 0.0f;
        scroll_anchor[i]                                         = 0.0f;
        acc_drawn_h[i]                                           = 0.0f;
        acc_target_h[i]                                          = 0.0f;
        acc_content_h[i]                                         = 0.0f;
        acc_content[i]                                           = UINT16_MAX;
        tree_nodes[i]                                            = nullptr;
        tree_count[i]                                            = 0;
        for (u32 b = 0; b < 4; ++b) {
            tree_expanded[i][b] = 0;
        }
        tabbar_items[i]                                          = nullptr;
        tabbar_count_[i]                                         = 0;
        tabbar_active[i]                                         = 0;
        listbox_row_user[i]                                     = nullptr;
        tooltip_str[i]                                         = nullptr;
        tooltip_delay[i]                                       = K_TOOLTIP_DEFAULT_DELAY;
        state_fade[i]                                            = 0.0f;
        off_color[i]                                              = 0;
        click_user[i]                                             = nullptr;
        draw_user[i]                                              = nullptr;        radio_group[i]                                            = 0;
        content_w[i]                                              = 0;
        content_h[i]                                              = 0;
        content_ascent[i]                                         = 0;
        _abs_x[i]                                                 = 0;
        _abs_y[i]                                                 = 0;
    }
    view_w = 0.0f;
    view_h = 0.0f;
}

inline void Manager::clear() noexcept {
    count          = 0;
    freelist_count = 0;
    hot            = UINT16_MAX;
    active         = UINT16_MAX;
    clicked        = UINT16_MAX;
    focus_id       = UINT16_MAX;
    focus_anchor   = UINT16_MAX;
    editing_id     = UINT16_MAX;
    event_head = event_tail = event_count = 0; // stale ids die with the pool
    prev_focus                            = UINT16_MAX;
    prev_hot                              = UINT16_MAX;
    measure_dirty                         = true;
    abs_cache_dirty                       = true;
    // Style pool resets with the widget pool (no widgets remain, so no
    // style_id can dangle). Without this every rebuild leaks slots until
    // register_style() hits MAX_STYLES and aborts.
    style_count                           = 0;
    register_style({TextureHandle::invalid(), 0, 0.0f, 0.0f, shape_type::ROUNDED_RECT, {}, {}, {}, 0, 0.0f});
}

// Safety net for callback owners: any widget carrying a callback but no
// owner gets `owner`. Call after (re)building a screen, before measure().
// Builders normally pass owners explicitly via button(); this covers
// stragglers and future buttons so a null owner can never crash a tap.
inline void Manager::backfill_callback_owners(void *owner) noexcept {
    for (u16 i = 0; i < count; ++i) {
        if (pool[i].on_click != nullptr && click_user[i] == nullptr) {
            click_user[i] = owner;
        }
        if (pool[i].on_draw != nullptr && draw_user[i] == nullptr) {
            draw_user[i] = owner;
        }
    }
}

inline u16 Manager::alloc() noexcept {
    abs_cache_dirty = true;
    u16 id;
    if (freelist_count > 0) {
        id = freelist[--freelist_count];
    } else if (count >= MAX) {
        return UINT16_MAX;
    } else {
        id = count++;
    }
    // Reset the PER-WIDGET state of the slot. clear() deliberately touches no
    // per-widget array (it only moves `count`), so a recycled slot arrives
    // holding whatever the PREVIOUS widget stored - and mm_07/freecell rebuild
    // their UI every frame, so this is the common case, not an edge one.
    //
    // WHY THIS BLOCK EXISTS, and why it grew the way it did: the first three
    // batches (tree / accordion / grid / row-renderer) were added one bug at a
    // time, each found by a screenshot. All four were the same defect -
    // "is there a model attached?", which a stale value answers YES to.
    //
    // That framing was too narrow, because it only covers values something ASKS
    // about. Everything below is in the same class by the same premise: the
    // invariant that makes these resets necessary - "clear() moves count and
    // touches nothing" - applies identically to all ~70 per-widget arrays, and
    // a factory cannot be relied on to cover it either (the listbox/combobox
    // pair is three of the four widget families that share these slots).
    //
    // So the rule is the blunt one: ANY per-widget array whose writer is a
    // SETTER (not a factory) gets reset here. A factory always runs after
    // alloc() and overwrites what it owns; a setter does not. If you add a
    // `set_foo(id, ...)`, add `foo[id]` to this list in the same commit - the
    // arrays that reached here are exactly the ones somebody forgot twice.
    //
    // The first group below is animation/derived state rather than
    // attachment, and is here for the same reason: progressbar's stripe phase
    // is animation CONTINUITY, so a recycled slot must not resume a phase the
    // previous widget owned (the stripe visibly jumps on its first frame), and
    // the popup animation mode is a real per-widget choice, which is why it has
    // a setter and not a style.
    widget_material[id].pipeline = PipelineHandle::invalid();
    layout_type[id]              = 0;
    layout_pad[id]               = 0;
    layout_spacing[id]           = 0;
    layout_align[id]             = 0;
    size_pct_w[id]               = 0;
    size_pct_h[id]               = 0;
    margin[id][0]                = 0;
    margin[id][1]                = 0;
    margin[id][2]                = 0;
    margin[id][3]                = 0;
    text_ext[id]                 = nullptr;
    tooltip_str[id]              = nullptr;
    tooltip_delay[id]            = K_TOOLTIP_DEFAULT_DELAY;
    combobox_anim_mode[id]       = popup_anim_mode::SCALE_FADE;
    progressbar_stripe_t[id]     = 0.0f;

    // Model state (TreeView / Accordion / DataGrid / ListBox row renderer).
    tree_nodes[id]           = nullptr;
    tree_count[id]           = 0;
    for (u32 b = 0; b < 4; ++b) {
        tree_expanded[id][b] = 0;
    }
    acc_content[id]          = UINT16_MAX;
    acc_drawn_h[id]          = 0.0f;
    acc_target_h[id]         = 0.0f;
    acc_content_h[id]        = 0.0f;
    grid_cols[id]            = nullptr;
    grid_order[id]           = nullptr;
    grid_col_count[id]       = 0;
    grid_header_h[id]        = 0.0f;
    grid_sort_col[id]        = UINT8_MAX;
    grid_sort_dir[id]        = grid_sort::NONE;
    grid_metrics[id]         = GridMetrics{};
    listbox_row_cb[id]       = nullptr;
    listbox_row_user[id]     = nullptr;
    return id;
}

inline void Manager::remove(u16 id) noexcept {
    if (id >= MAX) {
        return;
    }
    if (!(pool[id].flags & WF_VISIBLE)) {
        return;
    }
    pool[id].flags &= ~WF_VISIBLE;
    if (freelist_count < MAX) {
        freelist[freelist_count++] = id;
    }
    if (focus_id == id) {
        focus_id = UINT16_MAX;
    }
    if (editing_id == id) {
        editing_id = UINT16_MAX;
    }
    if (active == id) {
        active = UINT16_MAX;
    }
    if (hot == id) {
        hot = UINT16_MAX;
    }
    if (clicked == id) {
        clicked = UINT16_MAX;
    }
    measure_dirty   = true;
    abs_cache_dirty = true;
}

// Resolve one widget to screen space, folding the parent chain root-down
// through each widget's pos_mode (rect::apply_positioning). Root widgets
// are ABSOLUTE; a root ANCHORED widget pins to the view rect (falls back
// to ABSOLUTE while the view size is still unknown on frame 1). Depth is
// capped (cycle/corruption guard) — past it the frame is taken as-is.
inline mm_math::rect Manager::resolve_abs(u16 id, u8 depth) noexcept {
    const Widget &w = pool[id];
    if (w.parent >= MAX || depth > 64) {
        if (w.pos_mode == mm_math::position_mode::ANCHORED && view_w > 0.0f && view_h > 0.0f) {
            mm_math::rect screen(0.0f, 0.0f, view_w, view_h);
            return mm_math::rect::apply_positioning(w.frame, screen, w.pos_mode, w.anchor_pt, mm_math::vec2(w.frame.x, w.frame.y));
        }
        return w.frame;
    }
    if (w.pos_mode == mm_math::position_mode::ABSOLUTE) {
        return w.frame;
    }
    mm_math::rect prel = resolve_abs(w.parent, static_cast<u8>(depth + 1));
    // Coupling note: RELATIVE reads child.x/y and ignores offset;
    // ANCHORED reads offset (+half-size centering) and ignores child.x/y —
    // so the Widget passes its frame.x/y as the ANCHORED offset.
    mm_math::vec2 off  = (w.pos_mode == mm_math::position_mode::ANCHORED) ? mm_math::vec2(w.frame.x, w.frame.y) : mm_math::vec2(0.0f, 0.0f);
    return mm_math::rect::apply_positioning(w.frame, prel, w.pos_mode, w.anchor_pt, off);
}

// The scroll offset that applies to a widget: the sum of every SCROLLVIEW
// ancestor's current offset, so nested scroll views compose. Ancestors only -
// a scroll view is not offset by its own scroll, that is the window it shows.
//
// This is deliberately a SEPARATE walk from resolve_abs(). resolve_abs folds
// geometry down the parent chain; a scroll offset is not part of a widget's
// position, it is a viewport transform, and folding it in there would make
// every rect that a widget computes for itself (the scrollbar, the clip rect,
// hit-testing) silently disagree with the one it draws at.
inline f32 Manager::scroll_offset_for(u16 id, u8 depth) const noexcept {
    if (depth > 64) {
        return 0.0f; // cycle guard, same as resolve_abs / for_each_descendant
    }
    u16 p = pool[id].parent;
    while (p != UINT16_MAX && p < MAX) {
        if (pool[p].type == (u8)widget_type::SCROLLVIEW) {
            return scroll_scroll[p] + scroll_offset_for(p, static_cast<u8>(depth + 1));
        }
        p = pool[p].parent;
    }
    return 0.0f;
}

inline void Manager::rebuild_abs_cache() noexcept {
    if (!abs_cache_dirty) {
        return;
    }
    abs_cache_dirty = false;
    for (u16 i = 0; i < count; ++i) {
        mm_math::rect r   = resolve_abs(i, 0);
        // The viewport transform, applied once, here. Everything downstream -
        // draw, hit-testing, clip rects, the scrollbar - reads these two floats,
        // so there is exactly one place that knows about scrolling.
        const f32    off = scroll_offset_for(i, 0);
        _abs_x[i]           = r.x;
        _abs_y[i]           = r.y - off;
    }
}

inline f32    Manager::abs_x(u16 id) const noexcept { return _abs_x[id]; }

inline f32    Manager::abs_y(u16 id) const noexcept { return _abs_y[id]; }

// Scroll every ScrollView ancestor just far enough to show `focus_id`.
//
// Why this exists at all: a ScrollView is a viewport, not a selection control, so
// every focusable child is in the tab order - which means Tab walks into content
// that is currently scrolled out of view. The focus ring was then drawn outside
// the viewport and clipped away by the very container it belongs to, so the ring
// vanished with no visible cause and focus looked stuck. Nothing reconciled the
// offset; get_clip is the wrong tool for the fix because it returns the
// INTERSECTION of every clipping ancestor, and a nested pair needs each viewport
// adjusted separately - so this walks the parent chain.
//
// Minimal movement, and the two directions are exclusive (a widget cannot be both
// above and below the fold):
//   1. no change when the widget is already fully inside, so the first Tab onto an
//      already-visible control does not jitter;
//   2. align its TOP to the viewport top when it is above the fold;
//   3. align its BOTTOM to the viewport bottom when it is below.
// scroll_clamp then stops the last row from leaving a gap at the end of content.
//
// The abs cache is rebuilt after each adjustment, not once at the end. That is
// what makes this INSTANT rather than a multi-frame creep: adjusting a ScrollView
// moves its descendants but not itself, so walking the parent chain innermost-
// outermost and refreshing between steps is enough - every offset used to compute
// the next one is already correct. Without the refresh the ring would be drawn on
// the pre-scroll position for a frame, which is exactly the artefact being fixed.
inline void Manager::reveal_focused() noexcept {
    if (focus_id >= MAX) {
        return;
    }
    u16 p      = pool[focus_id].parent;
    u8  depth  = 0;
    bool     changed = false;
    while (p != UINT16_MAX && p < MAX && depth < 64) {
        if (pool[p].type == (u8)widget_type::SCROLLVIEW) {
            if (changed) {
                rebuild_abs_cache(); // an inner adjustment moved the focused widget
            }
            // The CONTENT rect, not the frame - the same geometry get_clip() clips
            // to. A ScrollView's children are cut to its border band, so measuring
            // against the frame says "visible" for a widget whose last few pixels
            // are the ones being cut off. content_rect() insets 0 for a borderless
            // style, which makes this identical to the frame there.
            f32 cx0 = 0.0f, cy0 = 0.0f, cx1 = 0.0f, cy1 = 0.0f;
            content_rect(styles[pool[p].style_id], _abs_x[p], _abs_y[p], pool[p].frame.w, pool[p].frame.h, cx0, cy0, cx1, cy1);
            if (cy1 <= cy0) { // degenerate style: fall back to frame behaviour
                cy0 = _abs_y[p];
                cy1 = cy0 + pool[p].frame.h;
            }
            const f32 vt  = cy0;
            const f32 vh  = cy1 - cy0;
            const f32 top = _abs_y[focus_id];
            const f32 bot = top + pool[focus_id].frame.h;
            // A DELTA, not an absolute offset. _abs_y already has the current
            // offset folded in, so adding the overshoot is the only form that is
            // right: an absolute value derived from the drawn rect lands short by
            // exactly the current offset, and the reveal then needs a second press
            // to finish. (It did, for two iterations, before this said "delta".)
            f32 delta = 0.0f;
            if (top < vt) {
                delta = top - vt;             // above the fold: scroll up by the gap
            } else if (bot > vt + vh) {
                delta = bot - (vt + vh);      // below the fold: scroll down by the gap
            }
            const f32 c = scroll_clamp(scroll_scroll[p] + delta, p);
            if (c != scroll_scroll[p]) {
                scroll_scroll[p] = c;
                // NOT set_scroll(), so the cache invalidation that setter does has
                // to be done here. rebuild_abs_cache() early-returns on a clean
                // flag, so without this the refresh below is a silent no-op and the
                // ring is drawn at the pre-scroll position - the exact artefact this
                // function exists to remove.
                abs_cache_dirty = true;
                changed         = true;
            }
        }
        p = pool[p].parent;
        ++depth;
    }
    if (changed) {
        rebuild_abs_cache(); // the subtree has to move, or set_scroll looks like a no-op
    }
}

inline u16 Manager::pick(f32 px, f32 py) const noexcept {
    for (u16 i = count; i > 0; --i) {
        u16 id = i - 1;
        auto    &w  = pool[id];
        if (!in_modal(id) || !is_visible(id) || !is_enabled(id)) {
            continue;
        }
        if (w.type == (u8)widget_type::LABEL || w.type == (u8)widget_type::SEPARATOR) {
            continue;
        }
        f32 ax = abs_x(id);
        f32 ay = abs_y(id);
        if (px >= ax && px <= ax + w.frame.w && py >= ay && py <= ay + w.frame.h) {
            return id;
        }
    }
    return UINT16_MAX;
}

inline bool Manager::is_finger_down(const InputState &input) const noexcept {
    for (u8 i = 0; i < input.touch.active_count; ++i) {
        auto &f = input.touch.fingers[i];
        if (f.phase == TouchPhase::Pressing || f.phase == TouchPhase::Moved) {
            return true;
        }
    }
    return false;
}

// Does a widget with this clip get drawn at all?
//
// A clip can be EMPTY - a collapsed Accordion intersects its content down to
// zero height, and get_clip() reports that as "clipped, w/h may be 0" rather
// than as "no clip", because "no clip" means draw EVERYTHING. Two ways to get
// that wrong, both of which have happened here:
//
//   - treating an empty clip as no clip paints the hidden subtree in full;
//   - handing the zero rect to the backend is INVALID: MTLScissorRect must have
//     non-zero width and height, and both backends pass the values straight
//     through unchecked, so the outcome is undefined. Pass 1.75's box vanished
//     under it while Pass 2's checkmark stayed, from the same widget and the
//     same frame.
//
// So every pass that scissors per widget asks this first and SKIPS on false.
// The batch bookkeeping stays valid because a skipped widget draws nothing: the
// previous widget's scissor correctly remains in force.
static constexpr bool clip_draws(bool clipped, u32 w, u32 h) noexcept { return !clipped || (w > 0 && h > 0); }

inline bool Manager::get_clip(u16 id, i16 &cx, i16 &cy, u16 &cw, u16 &ch) const noexcept {
    // EVERY clipping ancestor, intersected - not the first one. Returning the
    // first is the obvious shortcut and it is wrong the moment two containers
    // clip: a ScrollView inside an Accordion section is itself a clipping
    // container, so a row inside it resolved the ScrollView's full viewport and
    // never saw the section's band - collapsing the section left the rows
    // standing. (Same class: a child of a clipped panel nested in another
    // clipped panel could paint over the OUTER panel's border band.)
    bool   any = false;
    f32  ix0 = 0.0f, iy0 = 0.0f, ix1 = 0.0f, iy1 = 0.0f;
    u16 p = pool[id].parent;
    while (p != UINT16_MAX) {
        if (pool[p].flags & WF_CLIP) {
            // Clip to the CONTENT rect (inside the border bands), so clipped
            // children can never paint over their container's border.
            // Borderless styles inset 0 → identical to the frame rect.
            f32 x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
            content_rect(styles[pool[p].style_id], abs_x(p), abs_y(p), pool[p].frame.w, pool[p].frame.h, x0, y0, x1, y1);
            if (x1 <= x0 || y1 <= y0) { // degenerate style: frame behavior
                x0 = abs_x(p);
                y0 = abs_y(p);
                x1 = x0 + pool[p].frame.w;
                y1 = y0 + pool[p].frame.h;
            }
            // An accordion's content is clipped to its section's DRAWN height,
            // so a collapsing section reveals less of itself every frame instead
            // of vanishing at the end. Clamping HERE is what makes one change
            // cover every render pass - they all resolve their clip through
            // get_clip(), so the content, its children, a scroll view inside it
            // and its rows are all clipped without a line of per-pass code.
            const u16 acc = accordion_parent_of(p);
            if (acc != UINT16_MAX) {
                // p is the CONTENT: its rect is the section's visible band, so a
                // collapsing section shrinks it.
                const f32 drawn = acc_drawn_h[acc];
                if (drawn < (y1 - y0)) {
                    y1 = y0 + drawn;
                }
            } else if (p < MAX && acc_content[p] != UINT16_MAX) {
                // p is the HEADER: its rect is the header itself, so the band
                // hangs BELOW it by the drawn height. This is what clips the
                // content panel's own background.
                //
                // The FRAME rect, deliberately overriding the content rect
                // resolved above: the header's border band is meant to keep
                // children from painting over the header's OWN border, and the
                // section body is not such a child - it sits under the header,
                // where those side borders are nowhere near it. Inheriting them
                // inset the whole body by border_width * header_width (13.6pt on
                // a 340pt header at 0.04) and by the header's top inset, so the
                // panel was narrower than the header and the first glyphs of any
                // child near the left edge were cut - which is exactly what a
                // clipped section looks like.
                x0 = abs_x(p);
                y0 = abs_y(p);
                x1 = x0 + pool[p].frame.w;
                y1 = y0 + pool[p].frame.h + acc_drawn_h[p];
            }
            if (!any) {
                ix0 = x0;
                iy0 = y0;
                ix1 = x1;
                iy1 = y1;
                any = true;
            } else {
                // Intersect. A collapsed section can legitimately intersect down
                // to nothing; that is an EMPTY clip (draws nothing), which is a
                // different answer from "no clip" (draws everything).
                if (x0 > ix0) {
                    ix0 = x0;
                }
                if (y0 > iy0) {
                    iy0 = y0;
                }
                if (x1 < ix1) {
                    ix1 = x1;
                }
                if (y1 < iy1) {
                    iy1 = y1;
                }
            }
        }
        p = pool[p].parent;
    }
    if (!any) {
        return false;
    }
    cx = static_cast<i16>(ix0);
    cy = static_cast<i16>(iy0);
    cw = static_cast<u16>((ix1 > ix0) ? (ix1 - ix0) : 0.0f);
    ch = static_cast<u16>((iy1 > iy0) ? (iy1 - iy0) : 0.0f);
    return true;
}












inline u16 Manager::find_next_focus(u16 from) const noexcept {
    for (u16 i = from + 1; i < count; ++i) {
        // Modal scope + EFFECTIVE state: Tab must not escape the dialog, and
        // must not land on a widget whose panel was disabled (own-flag only
        // would let it through).
        if (!in_modal(i) || !is_visible(i) || !is_enabled(i)) {
            continue;
        }
        if (!(pool[i].flags & WF_FOCUSABLE)) {
            continue;
        }
        return i;
    }
    // Wrap-around: SAME checks as the loop above. Leaving it on own-flags only
    // is how focus escaped the modal - it cycled forward inside the dialog,
    // wrapped, and landed on the scene.
    for (u16 i = 0; i < count && i < from; ++i) {
        if (!in_modal(i) || !is_visible(i) || !is_enabled(i)) {
            continue;
        }
        if (!(pool[i].flags & WF_FOCUSABLE)) {
            continue;
        }
        return i;
    }
    return UINT16_MAX;
}

inline u16 Manager::find_prev_focus(u16 from) const noexcept {
    // Clamp the START, not just each lookup. `from` is UINT16_MAX when nothing
    // holds focus, and the first loop used to walk down from it - so it read
    // pool[65534], 8.4MB past a 128-entry array, and SIGBUS'd. The three
    // predicates on the line below all guard on `id < MAX`, which made the loop
    // look safe; the `pool[id].flags` read AFTER them was the one that was not,
    // so the guards were no protection at all. Shift-Tab from an unfocused page
    // is the whole trigger - you just have to be on a page with focusable
    // widgets to reach it, which is every page but this one's first frame.
    //
    // With nothing focused the walk starts at the end of the pool, so Shift-Tab
    // lands on the LAST focusable widget (the mirror of Tab landing on the
    // first) instead of doing nothing.
    const u16 top = (from >= count) ? count : from;
    for (u16 i = top; i > 0; --i) {
        u16 id = i - 1;
        if (!in_modal(id) || !is_visible(id) || !is_enabled(id)) {
            continue;
        }
        if (!(pool[id].flags & WF_FOCUSABLE)) {
            continue;
        }
        return id;
    }
    // Wrap-around: the SAME checks as the loop above, including in_modal() and
    // the EFFECTIVE state. This loop was left on own-flags only, which is the
    // exact bug the forward version documents: Shift-Tab inside a modal cycled
    // back within the dialog, wrapped, and landed on the scene.
    for (u16 i = count; i > top; --i) {
        u16 id = i - 1;
        if (!in_modal(id) || !is_visible(id) || !is_enabled(id)) {
            continue;
        }
        if (!(pool[id].flags & WF_FOCUSABLE)) {
            continue;
        }
        return id;
    }
    return UINT16_MAX;
}

inline void Manager::set_material(u16 id, const Material &mat) noexcept {
    if (id >= MAX) {
        return;
    }
    widget_material[id] = mat;
}

inline void Manager::set_layout(u16 id, u8 type, u8 padding, u8 spacing) noexcept {
    if (id >= MAX) {
        return;
    }
    layout_type[id]    = type;
    layout_pad[id]     = padding;
    layout_spacing[id] = spacing;
}

// Main/cross alignment for layout containers (0=Start, 1=Center, 2=End).
// Default 0/0 reproduces the legacy pile-from-padding behavior.
inline void Manager::set_layout_align(u16 id, u8 main_align, u8 cross_align) noexcept {
    if (id >= MAX) {
        return;
    }
    if (main_align > 2) {
        main_align = 0;
    }
    if (cross_align > 2) {
        cross_align = 0;
    }
    layout_align[id] = static_cast<u8>((cross_align << 2) | main_align);
}

// Percent size: 0 = absolute w/h (legacy), else % of container inner size.
// Percent of a root widget (no container) is treated as absolute.
inline void Manager::set_size_pct(u16 id, u8 w_pct, u8 h_pct) noexcept {
    if (id >= MAX) {
        return;
    }
    size_pct_w[id] = (w_pct > 100) ? 100 : w_pct;
    size_pct_h[id] = (h_pct > 100) ? 100 : h_pct;
}

// Positioning mode: ABSOLUTE = frame is screen points (parent ignored),
// RELATIVE = frame is a parent offset (legacy default), ANCHORED = pin the
// frame center to a parent anchor point + frame.x/y pixel offset.
inline void Manager::set_position_mode(u16 id, mm_math::position_mode m) noexcept {
    if (id >= MAX) {
        return;
    }
    pool[id].pos_mode = m;
    abs_cache_dirty   = true;
}

// ANCHORED pin point (meaningless unless pos_mode == ANCHORED).
inline void Manager::set_anchor(u16 id, mm_math::anchor a) noexcept {
    if (id >= MAX) {
        return;
    }
    pool[id].anchor_pt = a;
    abs_cache_dirty    = true;
}

// Outer margin [top, right, bottom, left] applied in layout flow.
inline void Manager::set_margin(u16 id, i8 top, i8 right, i8 bottom, i8 left) noexcept {
    if (id >= MAX) {
        return;
    }
    margin[id][0] = top;
    margin[id][1] = right;
    margin[id][2] = bottom;
    margin[id][3] = left;
}

// Inline-buffer setter. Explicit about the limit instead of silently
// truncating mid-word (the trap that bit three mm_07 labels this session).
inline bool Manager::set_text(u16 id, const char *t) noexcept {
    if (id >= MAX || t == nullptr) {
        return false;
    }
    const size_t n = std::strlen(t);
    if (n >= sizeof(pool[id].text)) {
        return false;
    }
    std::memcpy(pool[id].text, t, n + 1);
    text_ext[id] = nullptr;
    measure_dirty = true;
    return true;
}

// The scale a widget's text is BOTH measured at and drawn at. Labels only:
// Sarabun sets Thai smaller than Karla at the same bake size, so a Thai label
// draws 1.2x.
//
// One function because the two passes used to derive it separately and the draw
// pass was the only one that knew: measure() never applied the bump, so every
// Thai label's AUTO_W frame was 20% narrower than the text drawn in it. The old
// detector also ran on raw bytes ("any byte >= 0xE0" - a UTF-8 lead byte, not a
// codepoint) and so scaled U+2019 / U+201C / U+20AC as well.
inline f32 widget_text_scale(const Manager &m, u16 id) noexcept {
    const auto &w = m.pool[id];
    if (w.type != (u8)widget_type::LABEL) {
        return w.scale;
    }
    return text_scale_for(m.text_of(id), w.scale);
}

inline void Manager::measure(Renderer &r) noexcept {
    if (!measure_dirty) {
        return;
    }
    measure_dirty = false;
    for (u16 i = 0; i < count; ++i) {
        auto &w = pool[i];
        // The TEXT METRICS are computed for every widget, hidden or not.
        //
        // This used to `continue` on the own WF_VISIBLE flag, which made
        // content_w/h/ascent stale for a hidden subtree - and something READS
        // them: button::render_text uses content_w for the fit-to-box clamp. The
        // sequence is hide -> set_text (which sets measure_dirty) -> the measure
        // pass runs and skips this widget -> show it -> measure_dirty is now
        // false, so the stale width is never recomputed and a longer label is
        // not shrunk, i.e. it spills out of the button. That is the bug 3d2830d
        // fixed, reachable again through the visibility path.
        //
        // The FRAME is a different question: an AUTO widget's size feeds layout,
        // and a hidden widget should not reserve space in a hidden parent. So the
        // frame follows visibility and the metrics do not.
        //
        // One definition of the numbers, shared with the draw pass: Thai bump,
        // '?' fallback and the combining-mark rule all live in measure_string.
        const f32     sc   = widget_text_scale(*this, i);
        const TextMetrics tm  = measure_string(r.default_font, text_of(i), sc);
        content_w[i]          = static_cast<u16>(tm.widest);
        // The empty-string case still needs a usable box height, or a cleared
        // label collapses its own frame.
        const f32 asc = (tm.ascent > 0.001f) ? tm.ascent : 26.0f * sc;
        content_ascent[i] = static_cast<u16>(asc);
        // Ink box: the first line's ascent/descent, plus one line_height per
        // extra line. Uniform line height keeps it a single number (content_h is
        // a uint16 and every consumer already treats it as one box).
        const f32 lh    = r.default_font.line_height * sc;
        const f32 ink_h = (tm.lines > 1) ? ((static_cast<f32>(tm.lines - 1) * lh) + tm.ascent + tm.descent) : (tm.ascent + tm.descent);
        content_h[i]      = static_cast<u16>(ink_h);
        if (!is_visible(i)) {
            continue;
        }
        // AUTO_W sizes to the WIDEST LINE, not to the text after the last '\n'.
        // It used to use that running line total, so a two-line label whose
        // first line was longer got a frame narrower than the text in it.
        if (w.flags & WF_AUTO_W) {
            w.frame.w = tm.widest + static_cast<f32>(w.pad[3] + w.pad[1]);
        }
        if (w.flags & WF_AUTO_H) {
            w.frame.h = static_cast<f32>(content_h[i]) + static_cast<f32>(w.pad[0] + w.pad[2]);
        }
    }
}

// Where a determinate value label ("42%") goes.
//
// It used to be `ax + frame.w + pad_r + 12` - unconditionally OUTSIDE the
// widget, with nothing checking that the container had room, so a bar near a
// panel's right edge pushed its own value out of the panel and off the window.
// Now: outside when there is room, otherwise INSIDE, right-aligned in the
// content box.
inline f32 Manager::value_label_x(u16 id, f32 ax, f32 frame_w, f32 text_w, f32 outside_gap, f32 inside_inset) const noexcept {
    const f32 want = ax + frame_w + outside_gap;
    // Container right edge: the clip rect when there is one, else the view.
    f32 limit = static_cast<f32>(view_w) - 4.0f;
    i16  cx, cy;
    u16 cw, ch;
    if (get_clip(id, cx, cy, cw, ch)) {
        limit = static_cast<f32>(cx) + static_cast<f32>(cw) - 4.0f;
    }
    if (want + text_w <= limit) {
        return want;
    }
    f32 bx0 = 0.0f, bx1 = 0.0f;
    content_box(styles[pool[id].style_id], ax, frame_w, bx0, bx1);
    const f32 inside = bx1 - text_w - inside_inset;
    return inside > bx0 ? inside : bx0;
}

// Label scale for a value label: the theme's font scale, clamped so the line
// box fits the widget's inner height (the hardcoded 1.0 overflowed a 28pt bar
// and looked far bigger than the labels around it).
inline f32 Manager::value_label_scale(const Renderer &r, f32 frame_h, f32 pad_t, f32 pad_b) const noexcept {
    const f32 line_h = static_cast<f32>(r.default_font.line_height);
    f32       sc     = theme.font_scale;
    const f32 avail  = frame_h - pad_t - pad_b - 4.0f; // 4pt of breathing room
    if (line_h > 0.0f && avail > 0.0f && sc * line_h > avail) {
        sc = avail / line_h;
    }
    return sc > 0.0f ? sc : theme.font_scale;
}

} // namespace ui

// ─── Per-family inline modules (header-only; Manager complete above) ───
#include "mm_ui_wtext.hpp"
#include "mm_ui_wtoggle.hpp"
#include "mm_ui_wslider.hpp"
#include "mm_ui_wprogress.hpp"
#include "mm_ui_wlist.hpp"
#include "mm_ui_wbutton.hpp"
#include "mm_ui_wtab.hpp"

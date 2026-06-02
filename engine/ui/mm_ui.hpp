// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../render/mm_renderer.hpp"
#include "../input/mm_input_state.hpp"
#include <cstring>

namespace ui {

// ─── Widget types ────────────────────────────────────────────────
enum class WidgetType : uint8_t {
    Panel,
    Label,
    Button,
    Toggle,
    Slider,
    Checkbox,
    TextField,
    Image,
};

enum WidgetFlag : uint8_t {
    WF_Visible   = 1 << 0,
    WF_Enabled   = 1 << 1,
    WF_Clip      = 1 << 2,
    WF_Focusable = 1 << 3,
    WF_AutoW     = 1 << 4,
    WF_AutoH     = 1 << 5,
};

enum class BtnState : uint8_t {
    Normal,
    Hover,
    Pressed,
};

// ─── Widget node ─────────────────────────────────────────────────
using DrawCallback = void (*)(uint16_t id, Renderer& r, SpriteBatch& batch,
                              float abs_x, float abs_y, float dt);
using ChangeCallback = void (*)(uint16_t id, float value);

struct Widget {
    float x, y, w, h;
    float scale;
    uint32_t bg_color;      // 0xAABBGGRR, 0 = transparent
    uint32_t text_color;    // 0xAABBGGRR
    void (*on_click)(uint16_t id);
    DrawCallback on_draw;
    char text[48];
    uint16_t parent;        // UINT16_MAX = root
    uint8_t type;           // WidgetType
    uint8_t flags;          // WidgetFlag
    uint8_t state;          // BtnState or toggle bool
    uint8_t _pad[3];
};

static_assert(sizeof(Widget) == 104, "Widget size");

// ─── Theme — centralized color palette ───────────────────────────
struct Theme {
    // ── Colors ──────────────────────────────────────────────────
    uint32_t panel_bg;
    uint32_t button_bg;
    uint32_t button_text;
    uint32_t toggle_track_on;
    uint32_t toggle_track_off;
    uint32_t toggle_thumb;
    uint32_t toggle_thumb_hot;
    uint32_t toggle_text;
    uint32_t slider_bg;
    uint32_t slider_track;
    uint32_t slider_fill;
    uint32_t slider_thumb;
    uint32_t slider_thumb_hot;
    uint32_t slider_text;
    uint32_t checkbox_on;
    uint32_t checkbox_off;
    uint32_t checkbox_border;
    uint32_t checkbox_check;
    uint32_t checkbox_text;
    uint32_t textfield_bg;
    uint32_t textfield_text;
    uint32_t cursor;
    uint32_t focus_color;
    uint32_t text_primary;
    uint32_t text_secondary;

    // ── Layout: default padding [top, right, bottom, left] ──────
    int8_t panel_pad[4];
    int8_t label_pad[4];
    int8_t button_pad[4];
    int8_t toggle_pad[4];
    int8_t slider_pad[4];
    int8_t checkbox_pad[4];
    int8_t textfield_pad[4];

    // ── Layout: spacing defaults ────────────────────────────────
    uint8_t layout_padding;
    uint8_t layout_spacing;

    // ── Font ────────────────────────────────────────────────────
    char    font_path[128];      // "" = use embedded default
    float   font_scale;          // global scale multiplier

    static Theme dark() noexcept;
    static Theme load(const char* path) noexcept;
};

// ─── UI Manager ──────────────────────────────────────────────────
struct Manager {
    static constexpr uint16_t MAX = 128;
    Widget pool[MAX];
    uint16_t count;

    uint16_t freelist[MAX];
    uint16_t freelist_count;

    uint16_t hot;           // widget under pointer
    uint16_t active;        // widget being pressed (tracked across frames)
    uint16_t clicked;       // widget clicked this frame
    uint16_t focus_id;      // keyboard/gamepad focus

    // Slider storage (parallel arrays)
    float    slider_value[MAX];
    ChangeCallback on_change[MAX];

    // Toggle / Checkbox on/off colors (parallel arrays)
    uint32_t on_color[MAX];
    uint32_t off_color[MAX];

    //     // Per-widget material override (invalid = use default)
    Material widget_material[MAX];

    // Layout data (parallel arrays)
    uint8_t  layout_type[MAX];     // 0=None, 1=HBox, 2=VBox
    uint8_t  layout_pad[MAX];      // uniform padding
    uint8_t  layout_spacing[MAX];  // gap between children

    // TextField data (parallel arrays)
    uint8_t  cursor_pos[MAX];

    // Per-widget padding [top, right, bottom, left] (0 = none)
    int8_t   pad[MAX][4];

    // Cached content size from measure pass (text w/h at scale)
    uint16_t content_w[MAX];
    uint16_t content_h[MAX];
    uint16_t content_ascent[MAX];  // max ascender (baseline up) from glyph metrics

    // TextField edit state
    uint16_t editing_id;
    float    cursor_timer;

    // Theme
    Theme theme;

    void init() noexcept;
    void clear() noexcept;

    uint16_t panel(float x, float y, float w, float h, uint32_t color,
                   uint16_t parent = UINT16_MAX) noexcept;
    uint16_t label(float x, float y, const char* text, uint32_t color, float scale,
                   uint16_t parent = UINT16_MAX) noexcept;
    uint16_t button(float x, float y, float w, float h, const char* text,
                    uint32_t bg, uint32_t fg, void (*cb)(uint16_t),
                    uint16_t parent = UINT16_MAX) noexcept;
    uint16_t toggle(float x, float y, float w, float h, const char* text,
                    uint32_t bg_on, uint32_t bg_off, uint32_t fg,
                    bool initial, void (*cb)(uint16_t),
                    uint16_t parent = UINT16_MAX) noexcept;
    uint16_t slider(float x, float y, float w, float h,
                    float initial, ChangeCallback cb,
                    uint16_t parent = UINT16_MAX) noexcept;
    uint16_t textfield(float x, float y, float w, float h, const char* initial_text,
                       uint32_t bg, uint32_t fg,
                       uint16_t parent = UINT16_MAX) noexcept;
    uint16_t checkbox(float x, float y, const char* text,
                      uint32_t on_c, uint32_t off_c, uint32_t fg,
                      bool initial, void (*cb)(uint16_t),
                      uint16_t parent = UINT16_MAX) noexcept;
    uint16_t image(float x, float y, float w, float h,
                   uint16_t parent = UINT16_MAX) noexcept;
    void remove(uint16_t id) noexcept;
    void set_material(uint16_t id, const Material& mat) noexcept;

    void set_layout(uint16_t id, uint8_t type, uint8_t padding = 0, uint8_t spacing = 0) noexcept;
    void measure(Renderer& r) noexcept;
    void layout(Renderer& r) noexcept;

    bool was_clicked(uint16_t id) const noexcept { return id == clicked; }
    bool is_toggled(uint16_t id) const noexcept { return id < MAX ? pool[id].state != 0 : false; }
    float get_slider_value(uint16_t id) const noexcept { return id < MAX ? slider_value[id] : 0.0f; }
    bool is_editing() const noexcept { return editing_id != UINT16_MAX; }

    void handle(const InputState& input) noexcept;
    void render(Renderer& r, SpriteBatch& batch, float dt) noexcept;

private:
    uint16_t alloc() noexcept;
    float abs_x(uint16_t id) const noexcept;
    float abs_y(uint16_t id) const noexcept;
    uint16_t pick(float px, float py) const noexcept;
    bool is_finger_down(const InputState& input) const noexcept;
    bool get_clip(uint16_t id, int16_t& cx, int16_t& cy,
                  uint16_t& cw, uint16_t& ch) const noexcept;
    uint16_t find_next_focus(uint16_t from) const noexcept;
    uint16_t find_prev_focus(uint16_t from) const noexcept;
    void layout_children(uint16_t parent_id) noexcept;
};

// ─── Color helpers (engine format: 0xAABBGGRR) ──────────────────
MM_FORCE_INLINE static uint32_t ui_lighten(uint32_t color, uint8_t amount) noexcept {
    uint32_t a = color & 0xFF000000;
    uint32_t r = (uint32_t)((color & 0xFF) + amount);
    if (r > 255) r = 255;
    uint32_t g = (uint32_t)(((color >> 8) & 0xFF) + amount);
    if (g > 255) g = 255;
    uint32_t b = (uint32_t)(((color >> 16) & 0xFF) + amount);
    if (b > 255) b = 255;
    return a | (b << 16) | (g << 8) | r;
}

MM_FORCE_INLINE static uint32_t ui_darken(uint32_t color, uint8_t amount) noexcept {
    uint32_t a = color & 0xFF000000;
    uint32_t r = (uint32_t)((int)(color & 0xFF) - (int)amount);
    if (r > 255) r = 0;
    uint32_t g = (uint32_t)((int)(((color >> 8) & 0xFF)) - (int)amount);
    if (g > 255) g = 0;
    uint32_t b = (uint32_t)((int)(((color >> 16) & 0xFF)) - (int)amount);
    if (b > 255) b = 0;
    return a | (b << 16) | (g << 8) | r;
}

inline Theme Theme::dark() noexcept {
    Theme t{};
    t.panel_bg         = 0x2D2D2DFF;
    t.button_bg        = 0x3A3A3AFF;
    t.button_text      = 0xFFFFFFFF;
    t.toggle_track_on  = 0x5CB85CFF;
    t.toggle_track_off = 0x444444FF;
    t.toggle_thumb     = 0xFFF0F0FF;
    t.toggle_thumb_hot = 0xCCEEFFFF;
    t.toggle_text      = 0xFFFFFFFF;
    t.slider_bg        = 0x444444FF;
    t.slider_track     = 0x333333FF;
    t.slider_fill      = 0x44FF88FF;
    t.slider_thumb     = 0x66FFAAFF;
    t.slider_thumb_hot = 0x88FFAAFF;
    t.slider_text      = 0xFF88FF88;
    t.checkbox_on      = 0x5CB85CFF;
    t.checkbox_off     = 0x444444FF;
    t.checkbox_border  = 0xFF888888;
    t.checkbox_check   = 0xFFFFFFFF;
    t.checkbox_text    = 0xFFFFFFFF;
    t.textfield_bg     = 0x2D2D2DFF;
    t.textfield_text   = 0xFFFFFFFF;
    t.cursor           = 0xFFFFFFFF;
    t.focus_color      = 0x44FF88FF;
    t.text_primary     = 0xFFFFFFFF;
    t.text_secondary   = 0xAAAAAAFF;

    t.panel_pad[0] = t.panel_pad[1] = t.panel_pad[2] = t.panel_pad[3] = 0;
    t.label_pad[0] = t.label_pad[1] = t.label_pad[2] = t.label_pad[3] = 0;
    t.button_pad[0] = t.button_pad[1] = t.button_pad[2] = t.button_pad[3] = 6;
    t.toggle_pad[0] = t.toggle_pad[1] = t.toggle_pad[2] = t.toggle_pad[3] = 4;
    t.slider_pad[0] = t.slider_pad[1] = t.slider_pad[2] = t.slider_pad[3] = 4;
    t.checkbox_pad[0] = t.checkbox_pad[1] = t.checkbox_pad[2] = t.checkbox_pad[3] = 4;
    t.textfield_pad[0] = t.textfield_pad[1] = t.textfield_pad[2] = t.textfield_pad[3] = 6;

    t.layout_padding = 8;
    t.layout_spacing = 4;

    t.font_path[0] = '\0';
    t.font_scale   = 1.0f;
    return t;
}

// ─── Inline implementations ──────────────────────────────────────
inline void Manager::init() noexcept {
    count = 0;
    freelist_count = 0;
    hot = UINT16_MAX;
    active = UINT16_MAX;
    clicked = UINT16_MAX;
    focus_id = UINT16_MAX;
    editing_id = UINT16_MAX;
    cursor_timer = 0.0f;
    theme = Theme::dark();
    for (uint16_t i = 0; i < MAX; ++i) {
        widget_material[i].pipeline = PipelineHandle::invalid();
        layout_type[i] = 0;
        layout_pad[i] = 0;
        layout_spacing[i] = 0;
        cursor_pos[i] = 0;
        on_color[i] = 0;
        off_color[i] = 0;
        pad[i][0] = pad[i][1] = pad[i][2] = pad[i][3] = 0;
        content_w[i] = 0;
        content_h[i] = 0;
        content_ascent[i] = 0;
    }
}

inline void Manager::clear() noexcept {
    count = 0;
    freelist_count = 0;
    focus_id = UINT16_MAX;
    editing_id = UINT16_MAX;
}

inline uint16_t Manager::alloc() noexcept {
    if (freelist_count > 0) return freelist[--freelist_count];
    if (count >= MAX) return UINT16_MAX;
    return count++;
}

inline void Manager::remove(uint16_t id) noexcept {
    if (id >= MAX) return;
    if (!(pool[id].flags & WF_Visible)) return;
    pool[id].flags &= ~WF_Visible;
    if (freelist_count < MAX)
        freelist[freelist_count++] = id;
    if (focus_id == id) focus_id = UINT16_MAX;
    if (editing_id == id) editing_id = UINT16_MAX;
    if (active == id) active = UINT16_MAX;
    if (hot == id) hot = UINT16_MAX;
    if (clicked == id) clicked = UINT16_MAX;
}

inline float Manager::abs_x(uint16_t id) const noexcept {
    float ax = pool[id].x;
    if (pool[id].parent != UINT16_MAX) ax += abs_x(pool[id].parent);
    return ax;
}

inline float Manager::abs_y(uint16_t id) const noexcept {
    float ay = pool[id].y;
    if (pool[id].parent != UINT16_MAX) ay += abs_y(pool[id].parent);
    return ay;
}

inline uint16_t Manager::pick(float px, float py) const noexcept {
    for (uint16_t i = count; i > 0; --i) {
        uint16_t id = i - 1;
        auto& w = pool[id];
        if (!(w.flags & WF_Visible)) continue;
        if (!(w.flags & WF_Enabled)) continue;
        if (w.type == (uint8_t)WidgetType::Label) continue;
        float ax = abs_x(id);
        float ay = abs_y(id);
        if (px >= ax && px <= ax + w.w && py >= ay && py <= ay + w.h)
            return id;
    }
    return UINT16_MAX;
}

inline bool Manager::is_finger_down(const InputState& input) const noexcept {
    for (uint8_t i = 0; i < input.touch.active_count; ++i) {
        auto& f = input.touch.fingers[i];
        if (f.phase == TouchPhase::Pressing || f.phase == TouchPhase::Moved)
            return true;
    }
    return false;
}

inline bool Manager::get_clip(uint16_t id, int16_t& cx, int16_t& cy,
                              uint16_t& cw, uint16_t& ch) const noexcept {
    uint16_t p = pool[id].parent;
    while (p != UINT16_MAX) {
        if (pool[p].flags & WF_Clip) {
            cx = static_cast<int16_t>(abs_x(p));
            cy = static_cast<int16_t>(abs_y(p));
            cw = static_cast<uint16_t>(pool[p].w);
            ch = static_cast<uint16_t>(pool[p].h);
            return true;
        }
        p = pool[p].parent;
    }
    return false;
}

inline uint16_t Manager::panel(float x, float y, float w, float h, uint32_t color,
                               uint16_t parent) noexcept {
    uint16_t id = alloc();
    if (id == UINT16_MAX) return id;
    auto& wg = pool[id];
    wg.x = x; wg.y = y; wg.w = w; wg.h = h;
    wg.scale = 1.0f;
    wg.bg_color = color;
    wg.text_color = 0;
    wg.text[0] = '\0';
    wg.parent = parent;
    wg.type = (uint8_t)WidgetType::Panel;
    wg.flags = WF_Visible;
    wg.state = 0;
    wg.on_click = nullptr;
    wg.on_draw = nullptr;
    std::memcpy(pad[id], theme.panel_pad, sizeof(pad[id]));
    return id;
}

inline uint16_t Manager::label(float x, float y, const char* text, uint32_t color,
                               float scale, uint16_t parent) noexcept {
    uint16_t id = alloc();
    if (id == UINT16_MAX) return id;
    auto& wg = pool[id];
    wg.x = x; wg.y = y; wg.w = 0; wg.h = 0;
    wg.scale = scale;
    wg.bg_color = 0;
    wg.text_color = color;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent = parent;
    wg.type = (uint8_t)WidgetType::Label;
    wg.flags = WF_Visible | WF_AutoW | WF_AutoH;
    wg.state = 0;
    wg.on_click = nullptr;
    wg.on_draw = nullptr;
    std::memcpy(pad[id], theme.label_pad, sizeof(pad[id]));
    return id;
}

inline uint16_t Manager::button(float x, float y, float w, float h, const char* text,
                                uint32_t bg, uint32_t fg, void (*cb)(uint16_t),
                                uint16_t parent) noexcept {
    uint16_t id = alloc();
    if (id == UINT16_MAX) return id;
    auto& wg = pool[id];
    wg.x = x; wg.y = y; wg.w = w; wg.h = h;
    wg.scale = 1.2f;
    wg.bg_color = bg;
    wg.text_color = fg;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent = parent;
    wg.type = (uint8_t)WidgetType::Button;
    wg.flags = WF_Visible | WF_Enabled;
    wg.state = 0;
    wg.on_click = cb;
    wg.on_draw = nullptr;
    std::memcpy(pad[id], theme.button_pad, sizeof(pad[id]));
    return id;
}

inline uint16_t Manager::toggle(float x, float y, float w, float h, const char* text,
                                uint32_t bg_on, uint32_t bg_off, uint32_t fg,
                                bool initial, void (*cb)(uint16_t),
                                uint16_t parent) noexcept {
    uint16_t id = alloc();
    if (id == UINT16_MAX) return id;
    auto& wg = pool[id];
    wg.x = x; wg.y = y; wg.w = w; wg.h = h;
    wg.scale = 1.0f;
    wg.bg_color = theme.toggle_track_off;
    wg.text_color = fg;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent = parent;
    wg.type = (uint8_t)WidgetType::Toggle;
    wg.flags = WF_Visible | WF_Enabled | WF_Focusable;
    wg.state = initial ? 1 : 0;
    wg.on_click = cb;
    wg.on_draw = nullptr;
    on_color[id] = bg_on;
    off_color[id] = bg_off;
    std::memcpy(pad[id], theme.toggle_pad, sizeof(pad[id]));
    return id;
}

inline uint16_t Manager::slider(float x, float y, float w, float h,
                                float initial, ChangeCallback cb,
                                uint16_t parent) noexcept {
    uint16_t id = alloc();
    if (id == UINT16_MAX) return id;
    auto& wg = pool[id];
    wg.x = x; wg.y = y; wg.w = w; wg.h = h;
    wg.scale = 1.0f;
    wg.bg_color = theme.slider_bg;
    wg.text_color = 0;
    wg.text[0] = '\0';
    wg.parent = parent;
    wg.type = (uint8_t)WidgetType::Slider;
    wg.flags = WF_Visible | WF_Enabled | WF_Focusable;
    wg.state = 0;
    wg.on_click = nullptr;
    wg.on_draw = nullptr;
    slider_value[id] = initial;
    on_change[id] = cb;
    std::memcpy(pad[id], theme.slider_pad, sizeof(pad[id]));
    return id;
}

inline uint16_t Manager::textfield(float x, float y, float w, float h, const char* initial_text,
                                   uint32_t bg, uint32_t fg,
                                   uint16_t parent) noexcept {
    uint16_t id = alloc();
    if (id == UINT16_MAX) return id;
    auto& wg = pool[id];
    wg.x = x; wg.y = y; wg.w = w; wg.h = h;
    wg.scale = 1.0f;
    wg.bg_color = bg;
    wg.text_color = fg;
    std::strncpy(wg.text, initial_text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent = parent;
    wg.type = (uint8_t)WidgetType::TextField;
    wg.flags = WF_Visible | WF_Enabled | WF_Focusable;
    wg.state = 0;
    wg.on_click = nullptr;
    wg.on_draw = nullptr;
    cursor_pos[id] = static_cast<uint8_t>(std::strlen(wg.text));
    std::memcpy(pad[id], theme.textfield_pad, sizeof(pad[id]));
    return id;
}

inline uint16_t Manager::checkbox(float x, float y, const char* text,
                                  uint32_t on_c, uint32_t off_c, uint32_t fg,
                                  bool initial, void (*cb)(uint16_t),
                                  uint16_t parent) noexcept {
    uint16_t id = alloc();
    if (id == UINT16_MAX) return id;
    auto& wg = pool[id];
    wg.x = x; wg.y = y; wg.w = 28.0f; wg.h = 28.0f;
    wg.scale = 1.0f;
    wg.bg_color = initial ? on_c : off_c;
    wg.text_color = fg;
    std::strncpy(wg.text, text, sizeof(wg.text) - 1);
    wg.text[sizeof(wg.text) - 1] = '\0';
    wg.parent = parent;
    wg.type = (uint8_t)WidgetType::Checkbox;
    wg.flags = WF_Visible | WF_Enabled | WF_Focusable;
    wg.state = initial ? 1 : 0;
    wg.on_click = cb;
    wg.on_draw = nullptr;
    on_color[id] = on_c;
    off_color[id] = off_c;
    std::memcpy(pad[id], theme.checkbox_pad, sizeof(pad[id]));
    return id;
}

inline uint16_t Manager::image(float x, float y, float w, float h,
                                uint16_t parent) noexcept {
    uint16_t id = alloc();
    if (id == UINT16_MAX) return id;
    auto& wg = pool[id];
    wg.x = x; wg.y = y; wg.w = w; wg.h = h;
    wg.scale = 1.0f;
    wg.bg_color = 0xFFFFFFFF;  // white tint = show texture as-is
    wg.text_color = 0;
    wg.text[0] = '\0';
    wg.parent = parent;
    wg.type = (uint8_t)WidgetType::Image;
    wg.flags = WF_Visible | WF_Enabled;
    wg.state = 0;
    wg.on_click = nullptr;
    wg.on_draw = nullptr;
    return id;
}

inline uint16_t Manager::find_next_focus(uint16_t from) const noexcept {
    for (uint16_t i = from + 1; i < count; ++i) {
        if (!(pool[i].flags & WF_Visible)) continue;
        if (!(pool[i].flags & WF_Enabled)) continue;
        if (!(pool[i].flags & WF_Focusable)) continue;
        return i;
    }
    for (uint16_t i = 0; i < count && i < from; ++i) {
        if (!(pool[i].flags & WF_Visible)) continue;
        if (!(pool[i].flags & WF_Enabled)) continue;
        if (!(pool[i].flags & WF_Focusable)) continue;
        return i;
    }
    return UINT16_MAX;
}

inline uint16_t Manager::find_prev_focus(uint16_t from) const noexcept {
    for (uint16_t i = from; i > 0; --i) {
        uint16_t id = i - 1;
        if (!(pool[id].flags & WF_Visible)) continue;
        if (!(pool[id].flags & WF_Enabled)) continue;
        if (!(pool[id].flags & WF_Focusable)) continue;
        return id;
    }
    for (uint16_t i = count; i > from + 1; --i) {
        uint16_t id = i - 1;
        if (!(pool[id].flags & WF_Visible)) continue;
        if (!(pool[id].flags & WF_Enabled)) continue;
        if (!(pool[id].flags & WF_Focusable)) continue;
        return id;
    }
    return UINT16_MAX;
}

inline void Manager::set_material(uint16_t id, const Material& mat) noexcept {
    if (id >= MAX) return;
    widget_material[id] = mat;
}

inline void Manager::set_layout(uint16_t id, uint8_t type, uint8_t padding, uint8_t spacing) noexcept {
    if (id >= MAX) return;
    layout_type[id] = type;
    layout_pad[id] = padding;
    layout_spacing[id] = spacing;
}

inline void Manager::measure(Renderer& r) noexcept {
    for (uint16_t i = 0; i < count; ++i) {
        auto& w = pool[i];
        if (!(w.flags & WF_Visible)) continue;
        float tw = 0;
        float max_a = 0, max_d = 0;
        if (w.text[0] != '\0') {
            uint32_t text_len = static_cast<uint32_t>(std::strlen(w.text));
            Utf8Decoder dec(w.text, text_len);
            for (;;) {
                uint32_t cp = dec.next();
                if (cp == 0) break;
                if (cp == '\n') continue;
                auto* g = r.default_font.get_glyph(cp);
                if (g) {
                    tw += static_cast<float>(g->advance) * w.scale;
                    float asc = static_cast<float>(-g->bearing_y) * w.scale;
                    if (asc > max_a) max_a = asc;
                    int dpx = g->bearing_y + static_cast<int16_t>(g->h);
                    if (dpx > 0) {
                        float desc = static_cast<float>(dpx) * w.scale;
                        if (desc > max_d) max_d = desc;
                    }
                }
            }
        }
        content_w[i] = static_cast<uint16_t>(tw);
        if (max_a < 0.001f) max_a = 26.0f * w.scale; // fallback if no glyphs
        content_ascent[i] = static_cast<uint16_t>(max_a);
        content_h[i] = static_cast<uint16_t>(max_a + max_d);
        if (w.flags & WF_AutoW)
            w.w = tw + static_cast<float>(pad[i][3] + pad[i][1]);
        if (w.flags & WF_AutoH)
            w.h = static_cast<float>(content_h[i]) + static_cast<float>(pad[i][0] + pad[i][2]);
    }
}

} // namespace ui

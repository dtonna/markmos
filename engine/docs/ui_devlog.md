# UI Rendering Bug Fix Log

## Problems Found & Fixes Applied

### 1. Text Buffer Overwrite (mm_renderer.hpp)

**Problem**: `draw_text()` used a single shared `text_vb`/`text_ib` buffer and always wrote at offset 0. Each subsequent `draw_text` call overwrote the previous text's vertex data before the render graph executed, causing only the last text to appear.

**Fix**: Append each text at a running byte offset (`text_vertex_count` / `text_index_count`). Bind at that offset in the submit handler. Reset counters each frame in `flush_text()`.

---

### 2. Sprite Buffer Overwrite (mm_renderer.hpp)

**Problem**: Same pattern as text — `flush_sprites_impl()` always wrote to `sprite_vb`/`sprite_ib` at offset 0. Pass 2 sprites (toggle thumb, slider parts, checkbox details) overwrote pass 1 backgrounds.

**Fix**: Added `sprite_vertex_count` / `sprite_index_count`, append at running offsets, reset in `begin_frame()`.

---

### 3. Metal Backend `draw_indexed` (mm_metal_backend.hpp)

**Problem**: Backend hardcoded `indexBufferOffset=0` and `baseVertex=0`, passing `first_index` as `baseInstance`. All indexed draws referenced the wrong vertices.

**Fix**: Correctly compute `current_ib_offset + first_index * index_size` as the byte offset for index buffer binding, and pass `vertex_offset` as `baseVertex`.

---

### 4. Submit Handler Double-Offset (mm_renderer.hpp)

**Problem**: After fixing the backend, the submit handler in `flush_text()` / `flush_sprites_impl()` used both bind-byte-offset AND `first_index`/`vertex_offset` parameters, causing double-counted offsets.

**Fix**: Changed both text and sprite draw calls to `draw_indexed(key, idx_count, 1, 0, 0)` — the bind offset already positions correctly; `first_index=0` avoids double-counting.

---

### 5. Button Text Width Measurement (mm_ui.cpp)

**Problem**: Button text was horizontally centered using `strlen * 12 * scale` — a rough estimate that didn't account for varying glyph widths (e.g. 'i' vs 'W').

**Fix**: Replaced with actual glyph advance summation using `r.default_font.get_glyph()` per character, then cache width in `content_w[i]` via the `measure()` pass.

---

### 6. Vertical Text Centering (mm_ui.cpp)

**Problem**: The formula `ay + (h - 30*s)/2` placed text too low. The `-30` didn't match the actual font bearing (`bearing_y = -26` at base_size=48).

**Fix**: Changed to `ay + (h + 26*s)/2`, compensating for `bearing_y ≈ -26` (the visual center of uppercase glyphs is 13px above baseline). Later replaced with per-glyph metrics (see #11).

---

### 7. Missing Toggle Text (mm_ui.cpp)

**Problem**: `WidgetType::Toggle` fell through to the Label branch (`txt_y = ay; txt_sc = w.scale`), which rendered text at the widget's absolute Y (baseline = top edge) instead of centered vertically.

**Fix**: Added explicit `WidgetType::Toggle` case with centered text formula.

---

### 8. Label Spacing Too Tight (mm_game_entry.cpp)

**Problem**: Manual Y offsets between labels and neighboring widgets left gaps as small as 0–4px.

| Element | Original | Gap | Fixed | Gap |
|---------|----------|-----|-------|-----|
| Volume label offset | `cy+8` | 4px to checkbox | `cy+14` | 10px |
| Layout label offset | `cy+4` | 4px to buttons | `cy-14` | 4px to panel |
| Player Name label offset | `cy+4` | 0px to slider | `cy+14` | 10px |

---

### 9. Descender ('y' tail) Overflow (mm_game_entry.cpp)

**Problem**: The button default `scale=1.2f` (set in `button()`) made text ~57px tall in a 44px button. The 'y' descender extended ~10px below baseline, overflowing the button bottom.

**Root cause**: Font metrics: `bearing_y = -26` (ascender ≈ 35px), `descender ≈ 10px` at base_size=48. The centering formula didn't allocate room for descenders.

**Fixes**:
- Set `scale=1.0f` on Play Game, Restart, Main Menu buttons
- Increased Play Game button height: 44→50
- Increased gap after Play Game: `gap + 4`
- TextField "Player1": set `scale=0.6f` to fit within h=32

---

### 10. Button Scale Default (mm_ui.hpp)

**Problem**: `button()` defaulted to `wg.scale = 1.2f`, making most buttons too large for their 44px height.

**Fix**: Override scale on per-button basis via `m.pool[id].scale = 1.0f` in caller code.

---

### 11. Font Metrics Discovery

Measured at baked size=48:
| Glyph | bearing_y | h (atlas) | Advance | 
|-------|-----------|-----------|---------|
| 'P'   | -26       | 26        | 22      |
| 'a'   | -21       | 22        | 16      |

- `line_height = 57` (set to `base_size * 1.2f`)
- Ascender ≈ 35px, Descender ≈ 10px (per glyph)
- Visual span of a text line: `txt_y - ascender*s` to `txt_y + descender*s`

---

### 12. Measure Pass & Per-Widget Padding (mm_ui.hpp / mm_ui.cpp)

**Problem**: No systematic way to:
- Size labels automatically to fit their text
- Add padding around text within widgets
- Center text accounting for actual glyph extents (ascender + descender)

**Additions to `Manager`**:
- `int8_t pad[MAX][4]` — per-widget padding [top, right, bottom, left]
- `uint16_t content_w[MAX]` — cached text width (glyph advance sum × scale)
- `uint16_t content_h[MAX]` — cached visual height (ascender + descender) × scale
- `uint16_t content_ascent[MAX]` — cached max ascender × scale

**New flags**: `WF_AutoW` (bit 4), `WF_AutoH` (bit 5) — labels set both by default.

**`measure(Renderer& r)`** — computes per-widget:
- `content_w[i]` = sum of glyph advances × scale
- `content_ascent[i]` = max `-bearing_y × scale` across all glyphs in text
- `content_h[i]` = max_ascender + max_descender (where descender = `bearing_y + h`, clamped to ≥0)
- If `WF_AutoW`: `w = content_w + pad_left + pad_right`
- If `WF_AutoH`: `h = content_h + pad_top + pad_bottom`

**`layout(Renderer& r)`** — now takes `Renderer&`, calls `measure(r)` before positioning children.

**`render()`** — calls `measure(r)` at start. Text centering uses per-glyph metrics:

```cpp
// Before: txt_y = ay + (h + 26*s) / 2          (hardcoded, ignores descenders)
// After:  txt_y = ay + (h + 2*a - hc) / 2      (a = content_ascent, hc = content_h)
```

This gives correct centering for any text:
- "Play Game" (ascender=35s, descender=10s): `(h + 70s - 45s)/2 = (h + 25s)/2`
- "One" (ascender=35s, no descender): `(h + 70s - 35s)/2 = (h + 35s)/2`

---

## Remaining / Future

- **Game click-through**: Clicking blocks in-game doesn't work reliably (known pre-existing issue, deferred)
- **Movable player element**: Game is pure click-to-hit falling blocks. No paddle/player entity exists. `g.scene` entity pool is initialized but unused.
- **Text clipping**: TextField doesn't set `WF_Clip`. Long text with descenders can overflow the background rect visually (though cursor edit position assumes 12px char width).

---

## Summary of Changed Files

| File | Changes |
|------|---------|
| `engine/render/mm_renderer.hpp` | Buffer append, submit handler, draw_indexed params |
| `engine/rhi/mm_metal_backend.hpp` | bind_index_buffer offset, draw_indexed baseVertex |
| `engine/ui/mm_ui.hpp` | pad[4], content_w/h/ascent, WF_AutoW/H, measure(), layout(Renderer&) |
| `engine/ui/mm_ui.cpp` | layout(), render() — measure call, per-glyph centering, padding offset |
| `engine/entry/mm_game_entry.cpp` | Button heights, scales, gap spacing, layout(r) call |

# Markmos Engine

A cross-platform game engine built from scratch — Metal backend for iOS/macOS,
Vulkan backend for Android.

Written in C++23 with no exceptions, no RTTI, and no engine hiding the details from you.

## Screenshots

<div align="center">
  <img src="screenshots/title.png" width="200" style="border-radius: 20px">
  <img src="screenshots/gameplay.png" width="200" style="border-radius: 20px">
  <img src="screenshots/gameover.png" width="200" style="border-radius: 20px">
</div>

## Engine Examples

### Sprite Stress Test (mm_01)

<div align="center">
  <img src="screenshots/examples/mm_01_sprite_10k.png" width="400" style="border-radius: 12px">
</div>

10,000 animated sprites rendered at 60fps using the engine's sprite batch. Tests draw-call sorting, vertex throughput, and fill rate.

### Game Entry (mm_game_entry)

<div align="center">
  <img src="screenshots/examples/mm_game_entry.png" width="400" style="border-radius: 12px">
</div>

Production-style entry point with UI, particle effects, camera shake, tweening, save data, and touch/mouse hit-testing.

---

## Shader Lab (mm_06)

Interactive shader playground with 18+ material modes, hot-reload from `.msl` files, and on-screen navigation.

<div align="center">
  <img src="screenshots/examples/mm_06_sprite.png" width="280" title="Sprite: unlit textured quads">
  <img src="screenshots/examples/mm_06_grayscale.png" width="280" title="Grayscale: luminance desaturate">
  <img src="screenshots/examples/mm_06_dissolve.png" width="280" title="Dissolve: checker dissolve with glowing edges">
</div>

<div align="center">
  <img src="screenshots/examples/mm_06_outline.png" width="280" title="Outline: Sobel edge detect">
  <img src="screenshots/examples/mm_06_color.png" width="280" title="Color: lift/gamma/gain tint">
  <img src="screenshots/examples/mm_06_lab_file.png" width="280" title="Lab File: hot-reload from engine/shaders/lab/lab.frag.msl">
</div>

<div align="center">
  <img src="screenshots/examples/mm_06_normal_derive.png" width="280" title="Normal Derive: in-shader Sobel normal from checker">
  <img src="screenshots/examples/mm_06_normal_proc.png" width="280" title="Normal Proc: sine heightfield CPU-generated">
  <img src="screenshots/examples/mm_06_normal_png.png" width="280" title="Normal PNG: tangent-space RGB normal map">
</div>

<div align="center">
  <img src="screenshots/examples/mm_06_card.png" width="280" title="Card: albedo + normal map lit">
  <img src="screenshots/examples/mm_06_cartoon.png" width="280" title="Cartoon: toon bands + ink outline">
  <img src="screenshots/examples/mm_06_plastic.png" width="280" title="Plastic: clearcoat + fresnel rim + specular">
</div>

<div align="center">
  <img src="screenshots/examples/mm_06_glow.png" width="280" title="Glow: teal ring expand + fade">
  <img src="screenshots/examples/mm_06_gold.png" width="280" title="Gold: breathing gold border">
  <img src="screenshots/examples/mm_06_stay.png" width="280" title="Stay: persistent gold border + gentle pulse">
</div>

<div align="center">
  <img src="screenshots/examples/mm_06_sun.png" width="280" title="Sun: rotating gold rays with intensity">
  <img src="screenshots/examples/mm_06_aura.png" width="280" title="Aura: breathing inner light + warm-white edge frame">
</div>

<div align="center">
  <img src="screenshots/examples/mm_06_firework_01.png" width="280" title="Firework: night-sky page with auto-launch rockets">
  <img src="screenshots/examples/mm_06_firework_02.png" width="280" title="Firework: dual-tone rockets">
  <img src="screenshots/examples/mm_06_firework_03.png" width="280" title="Firework: rainbow burst mode">
</div>

**Controls:** `1-9`, `C`, `P`, `G`, `Y`, `S`, `X`, `A`, `F`, `W` switch modes; `Q`/`E` or on-screen `< PREV` / `NEXT >` cycle pages; `R` reloads the current lab `.msl` file.

---

## UI Lab (mm_07)

Widget showcase with 18 pages, live-tune knobs, modal dialogs, and keyboard shortcuts.

<div align="center">
  <img src="screenshots/examples/mm_07_panels_labels.png" width="280" title="Panels + Labels: nested panels, clipped overflow, multi-line text, tooltips, modal dialog">
  <img src="screenshots/examples/mm_07_buttons.png" width="280" title="Buttons: normal, accent, disabled, custom-drawn pill, live click counters">
  <img src="screenshots/examples/mm_07_toggles_checkboxes.png" width="280" title="Toggles + Checkboxes: on/off states, radio groups, live state readback">
</div>

<div align="center">
  <img src="screenshots/examples/mm_07_sliders.png" width="280" title="Sliders: continuous value change, live readout, min/max/step">
  <img src="screenshots/examples/mm_07_textfields.png" width="280" title="Text Fields: editable text input with cursor, selection, and validation">
  <img src="screenshots/examples/mm_07_styles.png" width="280" title="Styles: theme variants, border radius, font scaling">
</div>

<div align="center">
  <img src="screenshots/examples/mm_07_shapes.png" width="280" title="Shapes: rectangles, circles, polygons with fill/stroke">
  <img src="screenshots/examples/mm_07_align_percent_margin.png" width="280" title="Align/Percent/Margin: layout helpers and spacing">
  <img src="screenshots/examples/mm_07_focus.png" width="280" title="Focus: keyboard navigation, focus rings, tab order">
</div>

<div align="center">
  <img src="screenshots/examples/mm_07_listbox.png" width="280" title="Listbox: scrollable item list with selection">
  <img src="screenshots/examples/mm_07_combobox.png" width="280" title="Combobox: dropdown list with editable input">
  <img src="screenshots/examples/mm_07_radiobox.png" width="280" title="Radiobox: single-choice button group">
</div>

<div align="center">
  <img src="screenshots/examples/mm_07_progressbar.png" width="280" title="Progressbar: determinate and indeterminate modes">
  <img src="screenshots/examples/mm_07_scrollviews.png" width="280" title="Scrollviews: scrollable content area with scrollbars">
  <img src="screenshots/examples/mm_07_treeviews.png" width="280" title="Treeviews: expandable node hierarchy with selection">
</div>

<div align="center">
  <img src="screenshots/examples/mm_07_datagrid.png" width="280" title="Datagrid: sortable/filterable table grid">
  <img src="screenshots/examples/mm_07_accordions.png" width="280" title="Accordions: animated open/close sections">
</div>

**Controls:** `D1`-`D9`, `T`, `A`, `L`, `S`, `G`, `V`, `C`, `O`, `P` switch pages; `[Prev]`/`[Next]` buttons or `Q`/`E` cycle pages; `B` toggles button border; `Space` pauses animations; `R` rebuilds the current page.

---

## Features

- **Renderer abstraction** — Metal (iOS/macOS) and Vulkan (Android) behind a unified RHI
- **Sokol-style architecture** — `markmos_main()` as the single cross-platform entry point
- **Audio** via miniaudio
- **Memory** via rpmalloc
- **Job system** for multithreaded tasks
- **Sprite batching** with draw call sorting
- **C++23** — no exceptions, no RTTI, no unnecessary abstractions

## Build

### iOS (device)

```bash
cd engine
./build-ios.sh
```

### iOS (simulator)

```bash
./build-ios-sim.sh
```

### Android

```bash
cd engine
./build-android.sh                    # debug APK
CONFIG=Release ./build-android.sh     # release APK
```

## Project Structure

```
engine/
├── core/          # Memory, VFS, job system, utilities
├── render/        # Renderer, shaders, sprite batch
├── rhi/           # Metal/Vulkan backends
├── app/           # Platform entry points (iOS, Android, macOS)
├── thirdparty/    # rpmalloc, miniaudio, simdjson, stb, metal-cpp, VMA
├── shaders/       # GLSL sources (Vulkan)
└── entry/         # Game entry point
```

## License

MIT

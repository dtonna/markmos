# Markmos Engine Manual

## สารบัญ

1. [ภาพรวม](#1-ภาพรวม)
2. [เทคนิคหลัก (Core Techniques)](#2-เทคนิคหลัก-core-techniques)
3. [สถาปัตยกรรมระบบ (System Architecture)](#3-สถาปัตยกรรมระบบ-system-architecture)
4. [คู่มือการใช้งาน (Usage Guide)](#4-คู่มือการใช้งาน-usage-guide)
5. [ตัวอย่างโค้ด (Code Examples)](#5-ตัวอย่างโค้ด-code-examples)
6. [API Reference](#6-api-reference)

---

## 1. ภาพรวม

Markmos เป็น 2D game engine เขียนด้วย C++23 ออกแบบมาให้ไม่มี overhead จาก runtime polymorphism (zero virtual dispatch), ไม่ใช้ exceptions, ไม่ใช้ RTTI, และไม่ใช้ STL containers ใน hot path (แต่ยังใช้ smart pointers / containers ใน initialization path ได้)

### Core Philosophies

| หลักการ | คำอธิบาย |
|---------|----------|
| **Zero Virtual Dispatch** | ใช้ C++20 concepts + templates แทน inheritance |
| **SoA Layout** | Structure of Arrays เพื่อ SIMD-friendly cache locality |
| **Flat POD Structs** | struct ธรรมดา ไม่มี vtable pointer — serialize ได้ทันที |
| **Slotmap Handles** | {id, gen} ปลอดภัยกว่า raw pointer ป้องกัน dangling |
| **Bump / Frame Allocators** | alloc เป็น linear ตลอด frame แล้ว reset พร้อมกัน |
| **Compile-time Polymorphism** | ใช้ concepts (RHI_Backend) เลือก backend ตอน compile |
| **No Exceptions / RTTI** | ใช้ Expected<T,E> แทน exception |

---

## 2. เทคนิคหลัก (Core Techniques)

### 2.1 Compile-time Polymorphism via Concepts

```cpp
template<typename B>
concept RHI_Backend = requires(B& b, ...) {
    { b.create_buffer(...) } -> ExpectedLike<BufferHandle>;
    // ...
};
```

ใช้ concept `RHI_Backend` กำหนด interface ให้ backend ทุกตัว (Metal, Vulkan) ต้อง implement ครบ เวลาเรียกใช้งาน template จะถูก monomorphize ที่ compile time — zero overhead, zero vtable

**ไฟล์:** `rhi/mm_rhi_concept.hpp`

### 2.2 SoA (Structure of Arrays)

Particle System มีข้อมูลเป็น SoA แทน AoS:

```
AoS (เสีย):
    struct Particle { float px, py, vx, vy, life; };
    Particle particles[4096];

SoA (ดีกว่า):
    float px[4096], py[4096], vx[4096], vy[4096], life[4096];
```

เวลา update loop จะอ่าน `px[], py[], vx[], vy[], life[]` เรียงต่อกัน — prefetcher ทำงานได้ดี, SIMD-friendly, ไม่มี wasted space จาก padding

**ไฟล์:** `game/mm_particle_pool.hpp`, `game/mm_tween_pool.hpp`, `game/mm_scene.hpp`

### 2.3 Slotmap with Generation Counter

```cpp
struct SlotHandle {
    uint32_t id  : 24;   // index
    uint32_t gen : 8;    // generation counter
};
```

Handle = {index, generation} เมื่อ free slot แล้ว generation++ — handle เก่าใช้ไม่ได้อีก ป้องกัน ABA problem

Slotmap จอง memory เป็น chunks ขนาด 64 slots (1 cache line) — linear iteration, freelist เก็บใน array (ไม่ต้อง pointer chase)

**ไฟล์:** `core/mm_slotmap.hpp`, `core/mm_handle.hpp`

### 2.4 Frame Arena (Bump Allocator)

```cpp
class FrameArena {
    char*  base_;
    size_t offset_;     // pointer bump
public:
    void* alloc(size_t size, size_t align = 16);
    void  reset();       // offset = 0
};
```

alloc แบบ linear — แค่เลื่อน pointer ไม่มี free, ไม่มี fragmentation reset ทุกจบ frame ใช้กับ temporary data ที่อยู่แค่ frame เดียว

มี `DoubleArena` สำหรับ double buffering — arena สลับกันไปมา ป้องกันกรณี GPU ยังใช้ memory อยู่

**ไฟล์:** `core/mm_arena.hpp`

### 2.5 Sort Key (Packed uint64_t)

```cpp
struct SortKey {
    uint64_t key;
    // layer(8) | pipeline(12) | material(16) | depth(28)
};
```

ใช้ bits packing เพื่อให้เรียงลำดับ draw call ด้วย single uint64_t comparison — hardware sort (O(n log n)), ไม่มี branch, ไม่มี pointer chase

**ไฟล์:** `render/mm_sort_key.hpp`

### 2.6 Render Graph (Command Buffer)

ไม่ใช้ virtual function ต่อ draw call — สะสม Command (tagged union) ไว้ใน array ก่อน แล้วค่อย execute ทีหลัง:

```cpp
enum class CmdType : uint8_t {
    Draw, DrawIndexed, BindPipeline, ...
};

struct Command {
    CmdType type;
    SortKey sort_key;
    union { CmdDraw draw; CmdBindPipeline bind; ... };
};
```

ประโยชน์: sort ก่อน execute (ลด state change), flat array = cache friendly, switch-case เดียว (branch predictor ทำงานได้ดี)

**ไฟล์:** `render/mm_render_graph.hpp`

### 2.7 Lock-Free SPSC Ring Buffer

```cpp
template<typename T, size_t Capacity>
    requires (Capacity > 0 && (Capacity & (Capacity - 1)) == 0)
class RingBuffer { ... };
```

ใช้ atomic cursors แยก writer/reader — single producer, single consumer, ไม่มี lock, power-of-2 size สำหรับ modulo ราคาถูกด้วย mask

**ไฟล์:** `core/mm_ring_buffer.hpp`

### 2.8 Job System (Work-Stealing)

Thread pool + lock-free job queue + dependency counter (`JobCounter`) — ใช้ `pthreads` บน POSIX, spinlock บน queue ขนาดเล็ก

**ไฟล์:** `core/mm_job_system.hpp`

### 2.9 Global Pool Allocator

Fixed-size slab allocator แบบ multi-class (32, 48, 96, 256, 4096 bytes) — O(1) alloc/free, freelist เก็บใน slot ว่าง ไม่ต้อง metadata แยก

**ไฟล์:** `core/mm_pool.hpp`

### 2.10 VFS + Async Asset Loading

Virtual File System รองรับ macOS (.app bundle), iOS, Android (AAssetManager) + async loader ผ่าน job system + ring buffer สำหรับส่งผลกลับมา main thread

**ไฟล์:** `core/mm_vfs.hpp`

### 2.11 Camera Trauma System

ใช้ Perlin noise หรือ sine wave ซ้อนกัน 2-3 ความถี่ สร้าง screen shake ที่ดูเป็นธรรมชาติ:

```cpp
struct CameraTrauma {
    float trauma;         // 0.0 - 1.0
    float decay;          // ปกติ 0.85 - 0.95
    void add_trauma(float amount);
    void get_offset(float time, float& x, float& y, float& angle);
};
```

- ตอนเกิดเหตุการณ์ (ระเบิด, กระแทก) → `camera.add_trauma(SHAKE_LARGE)`
- ทุก frame → `camera.get_offset(time, x, y, angle)` → เอา offset ไปคูณกับ orthographic projection

**ไฟล์:** `game/mm_camera_trauma.hpp`

### 2.12 Tween System (SoA)

```cpp
struct TweenPool {
    float    start    [MAX_TWEENS];
    float    end      [MAX_TWEENS];
    float*   target   [MAX_TWEENS];  // pointer to animated value
    uint8_t  ease     [MAX_TWEENS];
    // ...
};
```

รองรับ Ease Functions: Linear, Quad, Cubic, Elastic, Bounce, Sine, Back + chainable (tween ต่อกัน), loop (loop/ping-pong)

**ไฟล์:** `game/mm_tween_pool.hpp`

### 2.13 Swap-With-Last Despawn

เวลา remove entity/particle/tween — แทนที่จะ shift array ทั้งก้อน ใช้ swap กับตัวสุดท้ายแล้วลด count:

```cpp
void deswap(uint16_t idx) {
    if (idx != count - 1) {
        data[idx] = data[count - 1];
    }
    --count;
}
```

O(1) remove, ไม่มี memmove, cache friendly — ใช้ใน ParticlePool, TweenPool, Scene

### 2.14 Expected<T,E> (No Exception Error Handling)

```cpp
Expected<TextureInfo, RHIError> tex = load_texture(...);
if (!tex) {
    log_error(tex.error());  // ไม่มี try-catch, ไม่มี stack unwinding
}
```

Implement เองด้วย discriminated union + placement new — ถ้า compiler มี `<expected>` ก็ใช้ของ standard แทน

**ไฟล์:** `core/mm_expected.hpp`

### 2.15 Material / Technique System

```cpp
struct Material {
    PipelineHandle pipeline;  // shader pipeline
    TextureHandle  texture;
    SamplerHandle  sampler;
};

struct Technique {
    Material passes[4];  // multi-pass: glow, blur, composite
    uint8_t   pass_count;
};
```

- Material = bundle ของ pipeline + texture + sampler
- Technique = multi-pass material (e.g. เอฟเฟกต์เรืองแสง)
- `flush_sprites(batch, material)` ใช้ pipeline, texture, sampler จาก material โดยตรง

**ไฟล์:** `render/mm_material.hpp`

### 2.16 Scene / Entity System (SoA)

```cpp
struct Scene {
    // SoA layout
    EntityHandle entities[MAX_ENTITIES];
    float        px[MAX_ENTITIES], py[MAX_ENTITIES];
    float        w[MAX_ENTITIES], h[MAX_ENTITIES];
    float        rot[MAX_ENTITIES];
    uint32_t     color[MAX_ENTITIES];
    uint8_t      visible[MAX_ENTITIES];
    uint16_t     count;

    EntityHandle spawn(float x, float y, float w, float h, uint32_t color);
    void         despawn(EntityHandle h);  // swap-with-last
    void         each(auto fn);             // linear scan
};
```

Entity เป็นแค่ index + generation (ผ่าน SlotHandle) ไม่มี class inheritance, render ผ่าน callback `each()` ที่วน linear scan

**ไฟล์:** `game/mm_scene.hpp`

### 2.17 Cache Metrics (Instrumentation)

```cpp
struct CacheCounters {
    atomic<uint32_t> allocations_this_frame;
    atomic<uint32_t> slotmap_lookups_in_hot_loop;
    atomic<uint32_t> pool_overflows;
};
```

เปิดตอน debug (`ENGINE_ENABLE_ASSERT=ON`) เพื่อ monitor จำนวน alloc/frame, slotmap lookups, pool overflow — ปิดใน release (zero overhead)

**ไฟล์:** `core/mm_cache_metrics.hpp`

### 2.18 Sprite Atlas System

```cpp
struct SpriteAtlas {
    TextureHandle texture;
    SpriteFrame   frames[256];
    uint16_t      count;
};

// .sprite text format:
// texture: spritesheet.png
// frame: idle 0 0 64 64
// frame: run_1 64 0 64 64
```

ใช้กับ `SpriteBatch::add_frame(atlas, "idle", x, y, w, h, rot, color)` — atlas UV คำนวณอัตโนมัติ

**ไฟล์:** `render/mm_sprite.hpp`, `render/mm_sprite_loader.hpp`

---

## 3. สถาปัตยกรรมระบบ (System Architecture)

### 3.1 Layers

```
┌─────────────────────────────────┐
│          App Layer              │  mm_app_mac.mm / mm_app_android.cpp
│  Window, Input, Lifecycle       │
├─────────────────────────────────┤
│         Game Layer              │  game/
│  Scene, Particle, Tween, Camera │
├─────────────────────────────────┤
│       Render Layer              │  render/
│  Renderer, RenderGraph, Sprite  │
├─────────────────────────────────┤
│    RHI Layer (Concept)          │  rhi/
│  Metal / Vulkan Backend         │
├─────────────────────────────────┤
│         Core Layer              │  core/
│  Arena, Pool, Slotmap, VFS, Job │
└─────────────────────────────────┘
```

### 3.2 Frame Lifecycle

```
เริ่ม frame
  ↓
process_completed()         ← async load results
  ↓
game_update(dt, input)      ← update logic, particles, tweens
  ↓
camera.update(dt)           ← trauma decay
  ↓
renderer.begin_frame()      ← swap double arena, reset command buffer
  ↓
backend.begin_pass(pass)    ← clear / load render target
  ↓
upload_camera()             ← upload projection matrix to GPU
  ↓
batch.add() → flush_sprites ← submit + draw sprites
  ↓
draw_text()                 ← text rendering (font atlas)
  ↓
backend.end_pass()
  ↓
renderer.end_frame()        ← execute render graph commands
```

### 3.3 Memory Flow

```
Frame Start
  ├── FrameArena.reset()
  │     └── alloc temporary data (vertices, commands, etc.)
  ├── DoubleArena.swap()
  │     ├── current = CPU writes
  │     └── previous = GPU reads (still in-flight)
  └── GlobalPool (persistent)
        ├── slab alloc 32/48/96/256/4096 bytes
        └── O(1), no fragmentation
```

---

## 4. คู่มือการใช้งาน (Usage Guide)

### 4.1 Getting Started

```cpp
#include "render/mm_renderer.hpp"
#include "game/mm_game_state.hpp"

static Renderer renderer;
static SpriteBatch batch;

void init() {
    renderer.init(nullptr, 1170.0f, 2532.0f);
    batch.init();
}

void frame(float dt) {
    batch.reset();
    batch.add(100, 100, 64, 64, 0.0f, 0xFFFFFFFF, 0);

    renderer.begin_frame();
    PassDesc pass{};
    pass.clear_color = {0.05f, 0.05f, 0.10f, 1.0f};
    pass.color_load = LoadOp::Clear;
    renderer.backend.begin_pass(pass);
    renderer.ortho(0, 1170, 2532, 0, -1, 1);
    renderer.upload_camera();
    renderer.flush_sprites(batch);
    renderer.backend.end_pass();
    renderer.end_frame();
}
```

### 4.2 การใช้ Sprite Atlas

สร้างไฟล์ `.sprite`:
```
texture: characters.png
frame: idle 0 0 64 64
frame: walk1 64 0 64 64
frame: walk2 128 0 64 64
```

โหลด:
```cpp
auto atlas = load_sprite_atlas("player.sprite", renderer);
// atlas.texture, atlas.frames[], atlas.count
```

ใช้:
```cpp
batch.add_frame(atlas, "idle", x, y, w, h, rot, color);
// หรือใช้ index:
batch.add_frame(atlas, 0, x, y, w, h, rot, color);
```

### 4.3 การใช้ Particle System

```cpp
ParticlePool particles;

// Burst explosion
particles.spawn_burst(x, y, 20, 50, 200, 1.0f, 0xFFFFAA44);

// Cone emission
particles.spawn_cone(x, y, 30, -PI/2, PI/4, 150, 0.8f, 0xFFFF4444);

// Rain
particles.spawn_rain(x, y, 300, 200, 50, 2.0f, 0x888888FF);

// Update
particles.update(dt, gravity_x, gravity_y);

// Render via sprite batch
batch.reset();
for (uint16_t i = 0; i < particles.count; ++i) {
    if (!particles.active[i]) continue;
    float s = particles.scale[i] * 20;
    batch.add(particles.px[i], particles.py[i],
              s, s, particles.rotation[i],
              particles.color[i], particles.atlas_id[i]);
}
renderer.flush_sprites(batch);
```

### 4.4 การใช้ Tween

```cpp
TweenPool tweens;

// Animate float value from 0 to 1 over 0.5s
float my_value = 0;
auto t1 = tweens.spawn(&my_value, 0.0f, 1.0f, 0.5f, EaseType::QuadOut);

// Chain: after t1 finishes, run t2
auto t2 = tweens.spawn(&my_value, 1.0f, 0.0f, 0.3f, EaseType::ElasticOut);
tweens.chain(t1, t2);

// Looping tween (mode 1 = loop, 2 = ping-pong)
auto t3 = tweens.spawn(&my_value, 0.0f, 1.0f, 1.0f, EaseType::SineInOut, 1);

// Update every frame
tweens.update(dt);

// Cancel
tweens.cancel(t1);
```

### 4.5 การใช้ Scene / Entity System

```cpp
Scene scene;

// Spawn entities
auto e1 = scene.spawn(100, 200, 64, 64, 0xFF4444FF);

// Query by type
scene.each([&](EntityHandle h, uint16_t idx) {
    if (scene.type[idx] == EntityType::Enemy) {
        scene.px[idx] += dt * 100;
    }
});

// AABB query
scene.query_aabb(x1, y1, x2, y2, [&](EntityHandle h, uint16_t idx) {
    // entity is inside the rectangle
});

// Find by type
EntityHandle found;
if (scene.find_by_type(EntityType::Player, found)) {
    // found player
}

// Despawn
scene.despawn(e1);
```

### 4.6 การใช้ Camera Trauma

```cpp
CameraTrauma camera;
camera.decay = 0.92f;  // อัตราการหายไปของ shake

// บวก trauma เมื่อเกิดเหตุการณ์
camera.add_trauma(SHAKE_SMALL);  // 0.2
camera.add_trauma(SHAKE_LARGE);  // 0.6

// ทุก frame
camera.update(dt);
float cam_x, cam_y, cam_angle;
camera.get_offset(time, cam_x, cam_y, cam_angle);

// ใช้กับ ortho projection
renderer.ortho(cam_x, screen_w + cam_x, screen_h + cam_y, cam_y, -1, 1);
renderer.upload_camera();
```

### 4.7 การใช้ Material / Technique

```cpp
// ใช้ built-in materials
renderer.flush_sprites(batch, renderer.builtin_materials[(int)MaterialType::Additive]);

// สร้าง Material เอง
Material mat;
mat.pipeline = renderer.sprite_pipeline;
mat.texture  = my_texture;
mat.sampler  = renderer.default_sampler;
renderer.flush_sprites(batch, mat);

// ใช้ Technique (multi-pass)
Technique tech;
tech.passes[0] = base_mat;
tech.passes[1] = glow_mat;
tech.pass_count = 2;
renderer.flush_sprites(batch, tech);  // ใช้ current_pass()
```

### 4.8 Async Asset Loading

```cpp
g_asset_loader.submit({
    .path = "textures/hero.png",
    .callback = [](VfsBlob blob, void* user) {
        // blob.data, blob.size — เรียกบน main thread
        auto tex = load_texture_from_memory(blob, renderer);
        blob.free();
    },
    .user = nullptr,
});

// ทุก frame — เรียกบน main thread เท่านั้น
g_asset_loader.process_completed();
```

### 4.9 VFS (พลัตฟอร์มที่ไม่ใช่ Android)

```cpp
// macOS: read จาก .app bundle
g_vfs.init(bundle_path, doc_path);

// อ่าน resource
auto blob = g_vfs.read_bundle("textures/hero.png");
if (blob.valid()) {
    // use blob
    blob.free();
}

// เขียน document
g_vfs.write_doc("save.dat", data, size);
g_vfs.write_doc_atomic("save.dat", data, size);  // atomic write
```

### 4.10 Blend Modes

```cpp
// Built-in blend pipelines ใน Renderer:
// sprite_pipeline     — Normal alpha blend  (SrcAlpha, OneMinusSrcAlpha)
// add_pipeline        — Additive           (SrcAlpha, One)
// mul_pipeline        — Multiply           (Zero, SrcColor)
// opq_pipeline        — Opaque             (One, Zero)

// BlendFactor ที่รองรับ:
// Zero, One, SrcAlpha, OneMinusSrcAlpha,
// DstAlpha, OneMinusDstAlpha, SrcColor, OneMinusSrcColor
```

---

### 4.11 การใช้ UI Widget System

UI system มี widget 6 ชนิด: `Panel`, `Label`, `Button`, `Toggle`, `Slider`, `TextField` — เก็บใน flat pool ขนาด 128 widgets, เรียง Z-order ตามลำดับการสร้าง (สร้างทีหลัง = อยู่บน)

#### สร้าง Widgets

```cpp
#include "../ui/mm_ui.hpp"

ui::Manager ui;
ui.init();

// Panel — พื้นหลังสี่เหลี่ยม
uint16_t p = ui.panel(100, 200, 300, 400, 0xCC3344FF);

// Label — ข้อความ
uint16_t l = ui.label(120, 220, "Hello World", 0xFFFFFFFF, 1.5f, p);

// Button — ปุ่มกด
uint16_t b = ui.button(150, 300, 200, 60, "PLAY",
                       0x44AA44FF, 0xFFFFFFFF, on_play_click);
```

#### on_click Callback

```cpp
static void on_play_click(uint16_t id) {
    // id = widget handle ที่ถูกคลิก
    game_state = GameState::Playing;
}

// หรือเช็คภายหลังด้วย was_clicked():
if (ui.was_clicked(btn_id)) {
    // do something
}
```

#### on_draw Callback (วาดเพิ่มเอง)

`on_draw` ถูกเรียกใน Pass 2 (หลังจากวาดพื้นหลัง + slider Pass 1, ก่อนวาดข้อความ Pass 3) ใช้สำหรับเพิ่มเอฟเฟกต์, particle, texture background, หรือ custom renderer calls

**`on_draw` ใช้ texture ได้ 2 วิธี:**

**วิธีที่ 1 — ลงทะเบียน texture + ใช้ `tex_id` (แนะนำ):**
ลงทะเบียน texture ที่ `SpriteBatch` แล้วใช้ `tex_id` ใน `batch.add()` — `flush_sprites` จะแยก draw call ให้อัตโนมัติ:

```cpp
// === ใน game_init() ===
// โหลด texture
auto tex_info = texture_load_from_file(r.backend, vfs, "ui/btn_bg.png");
TextureHandle btn_tex = tex_info ? tex_info->handle : TextureHandle{};

// ลงทะเบียน texture ที่ batch (ทำครั้งเดียว)
batch.register_texture(btn_tex);

// === ใน on_draw callback ===
static void textured_bg(uint16_t id, Renderer& r, SpriteBatch& batch,
                        float ax, float ay, float dt) {
    // tex_id = low 16 bits ของ handle.id (batch.register_texture ใช้ค่าตัวเดียว)
    batch.add(ax, ay, 200, 60, 0, 0xFFFFFFFF, 0,
              static_cast<uint16_t>(btn_tex.handle.id));
}
// ไม่ต้อง flush — ui.render() จะ flush อัตโนมัติรวมกับ background/text passes
```

**ข้อดี:** texture หลายแบบปนกันใน batch เดียวได้ — `flush_sprites_impl` จะ group sprite ตาม `tex_id` และออก draw call แยกให้อัตโนมัติ

**วิธีที่ 2 — flush เองใน callback:**
ใช้สำหรับ custom pipeline หรือการปรับ texture ตาม state:

```cpp
static void textured_button(uint16_t id, Renderer& r, SpriteBatch& batch,
                            float ax, float ay, float dt) {
    auto& ui_pool = /* reference to ui.pool */;
    TextureHandle tex = (ui_pool[id].state == (uint8_t)BtnState::Pressed)
                        ? btn_pressed : btn_normal;
    batch.add(ax, ay, 240, 72, 0, 0xFFFFFFFF, 0, tex.handle.id);
    r.flush_sprites(batch, tex, r.default_sampler);
}
```

**วิธีที่ 3 — particle stream:**
```cpp
static ParticlePool* g_pool;

static void button_particles(uint16_t id, Renderer& r, SpriteBatch& batch,
                             float ax, float ay, float dt) {
    g_pool->spawn_burst(ax + 100, ay + 30, 3, 10, 60, 0.5f, 0xFFFFAA44);
    g_pool->update(dt, 0, 200);

    for (uint16_t i = 0; i < g_pool->count; ++i) {
        if (!g_pool->active[i]) continue;
        float s = g_pool->scale[i] * 10;
        batch.add(g_pool->px[i], g_pool->py[i],
                  s, s, g_pool->rotation[i],
                  g_pool->color[i], 0);
    }
    r.flush_sprites(batch);
}
```

#### ตัวอย่าง: Multiple Textured Buttons

```cpp
// === ใน game_init() ===
auto tex1 = texture_load_from_file(r.backend, vfs, "ui/play_btn.png");
auto tex2 = texture_load_from_file(r.backend, vfs, "ui/settings_btn.png");
TextureHandle play_tex   = tex1 ? tex1->handle : TextureHandle{};
TextureHandle settings_tex = tex2 ? tex2->handle : TextureHandle{};

// ลงทะเบียน texture ทั้งสอง
batch.register_texture(play_tex);
batch.register_texture(settings_tex);

// สร้าง buttons
uint16_t play_btn = ui.button(100, 200, 240, 72, "PLAY", 0, 0xFFFFFFFF, on_play);
uint16_t set_btn  = ui.button(400, 200, 240, 72, "SETTINGS", 0, 0xFFFFFFFF, on_settings);

// ใส่ on_draw ด้วย texture ต่างกัน
ui.pool[play_btn].on_draw = [](uint16_t id, Renderer& r, SpriteBatch& batch,
                                float ax, float ay, float dt) {
    batch.add(ax, ay, 240, 72, 0, 0xFFFFFFFF, 0,
              static_cast<uint16_t>(play_tex.handle.id));
};
ui.pool[set_btn].on_draw = [](uint16_t id, Renderer& r, SpriteBatch& batch,
                               float ax, float ay, float dt) {
    batch.add(ax, ay, 240, 72, 0, 0xFFFFFFFF, 0,
              static_cast<uint16_t>(settings_tex.handle.id));
};

// === ใน game_frame() — flush เดียว ใช้ texture ต่างกันเอง ===
g_data.ui.handle(input);
g_data.ui.render(r, g_data.batch, dt);
```

#### ตัวอย่าง: Title Screen

```cpp
void create_title_screen(ui::Manager& ui) {
    uint16_t title = ui.label(100, 200, "MARKMOS", 0xFFCC44FF, 3.0f);

    uint16_t play = ui.button(200, 350, 240, 72, "PLAY",
                               0x44AA44FF, 0xFFFFFFFF, [](uint16_t) {
        game_state = GameState::Playing;
    });

    // เพิ่ม animation ให้ปุ่ม
    ui.pool[play].on_draw = button_glow_effect;
}

// ใน game_frame():
g_data.ui.handle(input);
g_data.ui.render(r, g_data.batch, dt);  // อย่าลืมส่ง dt
```

#### Color Helpers

```cpp
uint32_t lighter = ui_lighten(0x44AA44FF, 40);  // เพิ่มความสว่าง
uint32_t darker  = ui_darken(0x44AA44FF, 30);   // ลดความสว่าง
```

#### Input Model

- `handle(input)` — ตรวจจับ pointer (touch/mouse) + `InputAction::Select`
- กดค้าง (`Pressing`/`Moved` phase) → ตั้ง `active` widget → แสดง visual feedback (Pressed state)
- ปล่อย (`Select` action) → ถ้า pointer ยังอยู่บน widget เดิม → เรียก `on_click`
- Widget ที่ `WF_Enabled` จะถูก hit-test เท่านั้น
- Label ไม่ถูก hit-test (ข้ามใน `pick()`)
- ใช้ `ui.was_clicked(id)` หรือ `ui.clicked` (reset ทุก frame)

#### Slot Reuse (Freelist)

เมื่อ `remove(id)` ถูกเรียก, slot ของ widget จะถูกคืนสู่ freelist และถูก reuse โดย `panel()` / `label()` / `button()` ถัดไป — pool 128 slots ไม่รั่วแม้ create/remove บ่อยครั้ง

#### Clipping (Scissor Test)

เด็กของ panel ที่มี `WF_Clip` จะถูกตัดไม่ให้ล้นออกนอกขอบเขต panel:

```cpp
// สร้าง panel ที่ clip children
uint16_t p = ui.panel(100, 100, 300, 200, 0x333333FF);
ui.pool[p].flags |= WF_Clip;

// children ที่อยู่เกินขอบ panel จะถูกตัด
uint16_t child = ui.button(250, 150, 200, 60, "OK", 0x44AA44FF, 0xFFFFFFFF, on_ok, p);
// ↑ ปุ่มนี้จะแสดงแค่ 50px แรก (ที่เหลือถูก clip)
```

`Renderer::set_scissor(x, y, w, h)` สามารถเรียกใช้โดยตรงเพื่อตั้ง scissor rect สำหรับ custom rendering ได้เช่นกัน

#### API Reference

```cpp
// Manager methods
void     init();                                    // reset state
void     clear();                                   // remove all widgets
uint16_t panel(float x, float y, float w, float h,
               uint32_t color, uint16_t parent = UINT16_MAX);
uint16_t label(float x, float y, const char* text,
               uint32_t color, float scale,
               uint16_t parent = UINT16_MAX);
uint16_t button(float x, float y, float w, float h,
                const char* text, uint32_t bg, uint32_t fg,
                void (*on_click)(uint16_t),
                uint16_t parent = UINT16_MAX);
uint16_t toggle(float x, float y, float w, float h,
                const char* text, uint32_t bg_on, uint32_t bg_off,
                uint32_t fg, bool initial, void (*on_click)(uint16_t),
                uint16_t parent = UINT16_MAX);
uint16_t slider(float x, float y, float w, float h,
                float initial, void (*on_change)(uint16_t, float),
                uint16_t parent = UINT16_MAX);
void     remove(uint16_t id);
bool     was_clicked(uint16_t id);
bool     is_toggled(uint16_t id);
float    get_slider_value(uint16_t id);
void     handle(const InputState& input);
void     render(Renderer& r, SpriteBatch& batch, float dt);

// Widget fields (ตั้งค่าหลังสร้างได้)
struct Widget {
    float  x, y, w, h;          // ตำแหน่ง + ขนาด
    float  scale;               // text scale
    uint32_t bg_color;          // 0xAABBGGRR
    uint32_t text_color;        // 0xAABBGGRR
    void (*on_click)(uint16_t);
    DrawCallback on_draw;       // custom draw callback
    char text[48];              // label text
    uint16_t parent;            // UINT16_MAX = root
    uint8_t type;               // WidgetType
    uint8_t flags;              // WF_Visible | WF_Enabled | WF_Focusable | WF_Clip
    uint8_t state;              // BtnState (Normal/Hover/Pressed) หรือ toggle bool
};

// DrawCallback signature
using DrawCallback = void (*)(uint16_t id, Renderer& r,
                              SpriteBatch& batch,
                              float abs_x, float abs_y,
                              float dt);

// Size
static_assert(sizeof(Widget) == 104);
```

#### Focus (Keyboard / Gamepad)

Widgets ที่มี `WF_Focusable` สามารถรับ focus ผ่าน keyboard/gamepad:

```cpp
pool[id].flags |= WF_Focusable;           // ทำให้ focus ได้

// Keyboard: MenuUp/MenuDown (↑/↓ หรือ W/S) → เปลี่ยน focus
// Keyboard: Confirm/MenuConfirm (Enter/Space) → activate focused widget
```

- `Manager::focus_id` = ID ของ widget ที่กำลัง focus (UINT16_MAX = ไม่มี)
- `menu_up`, `menu_down` actions: เลื่อน focus ไป widget focusable ถัดไป/ก่อนหน้า (wrap around)
- `confirm`/`menu_confirm`: trigger `on_click` บน focused widget
- focus indicator: เส้นขอบสีเขียวรอบ focused widget

#### Toggle Widget

```cpp
bool sound_on = true;
uint16_t t = ui.toggle(100, 200, 260, 60, "Sound", 0x44FF88FF, 0x444444FF,
                       0xFFFFFFFF, sound_on, [](uint16_t id) {
    game_data.sound_enabled = ui.is_toggled(id);
});
```

- `toggle()` สร้างปุ่ม toggle on/off
- `initial` = สถานะเริ่มต้น (true = on, false = off)
- `is_toggled(id)` คืนค่า `pool[id].state != 0`
- เมื่อคลิก: flip `state` (0 ↔ 1), flip `bg_color` (on ↔ off), เรียก `on_click`
- แถบ indicator สีขาวที่ขอบล่างเมื่อเปิด (on state)

#### Slider Widget

```cpp
uint16_t s = ui.slider(100, 300, 300, 40, 0.5f, [](uint16_t id, float val) {
    game_data.volume = val;  // val ∈ [0, 1]
});
```

- `slider()` สร้าง slider bar พร้อม thumb ที่ลากได้
- `get_slider_value(id)` คืนค่า float 0.0–1.0
- ลากนิ้วบน slider ค่าจะเปลี่ยนตามตำแหน่ง pointer
- เรียก `on_change(id, value)` ทุก frame ขณะลาก
- `WF_Focusable` โดยอัตโนมัติ → ปรับค่าได้ด้วย keyboard

#### Custom Shader / Pipeline per Widget

ตั้ง `Material` override เพื่อใช้ shader ของตัวเองแทน default sprite pipeline:

```cpp
// สร้าง custom pipeline (เช่น rounded-rect shader, glow, blur)
PipelineHandle custom_pipeline = r.backend.create_pipeline(desc);

// สร้าง Material
Material mat;
mat.pipeline = custom_pipeline;
mat.texture  = r.white_tex;
mat.sampler  = r.default_sampler;

// ตั้งให้ widget ใช้ material นี้ (background pass จะใช้ shader นี้)
ui.set_material(widget_id, mat);

// ลบ override (กลับไปใช้ default)
Material clear_mat = {};
clear_mat.pipeline = PipelineHandle::invalid();
ui.set_material(widget_id, clear_mat);
```

- `set_material()` ใช้ parallel array (`Material widget_material[MAX]`) ไม่เพิ่มขนาด Widget struct
- Widgets ที่ใช้ material เดียวกันจะถูก batch ด้วยกัน; material ต่างกัน → flush แยก
- custom pipeline มีผลเฉพาะ Pass 1 (background rectangles)
- Pass 2 (on_draw) และ Pass 3 (text) ใช้ default pipeline ตามเดิม
- ตัวอย่างการใช้งานจริง: `on_draw` callback เพื่อ custom rendering (ได้ `Renderer&`) สำหรับ Pass 2

#### Layout (HBox / VBox)

จัดตำแหน่ง children อัตโนมัติด้วย HBox (แนวนอน) หรือ VBox (แนวตั้ง):

```cpp
// สร้าง Panel เป็น container
uint16_t menu = ui.panel(100, 200, 400, 300, 0x22000000);

// ตั้ง layout type: 1=HBox, 2=VBox
ui.set_layout(menu, 2, 20, 20);  // VBox, padding=20, spacing=20

// children จะถูกจัดตำแหน่งอัตโนมัติ
auto lbl = ui.label(0, 0, "Title", 0xFFFFFFFF, 2.0f, menu);
ui.pool[lbl].h = 60;  // ต้อง set h ให้ label เพื่อให้ layout คำนวณระยะ
ui.button(0, 0, 260, 80, "PLAY", 0x44FF88FF, 0xFFFFFFFF, nullptr, menu);
```

- `set_layout(id, type, padding, spacing)` — type: `0=None`, `1=HBox`, `2=VBox`
- padding = ระยะจากขอบ parent ถึง child ตัวแรก (ทุกด้าน)
- spacing = ระยะระหว่าง child แต่ละตัว
- layout ทำงานแบบ recursive: container ใน container ก็ใช้ layout ได้
- Widget ที่มี `w=0` หรือ `h=0` จะถูก skip ใน layout (ไม่ advance ตำแหน่ง)
- ต้องเรียก `ui.layout()` ก่อน `ui.handle()` ทุก frame (layout → input → render)
- layout จัดการแค่ตำแหน่ง (x, y) ไม่เปลี่ยนขนาด (w, h)

#### TextField

ช่องกรอกข้อความ — รองรับพิมพ์, ลบ, เลื่อน cursor, cursor blink:

```cpp
uint16_t name = ui.textfield(100, 200, 360, 50, "Player",
                              0x333333FF, 0xFFFFFFFF);
```

- `textfield(x, y, w, h, initial_text, bg, fg)` — สร้าง TextField ด้วยข้อความเริ่มต้น
- คลิกหรือ focus + Enter → เข้าสู่โหมดแก้ไข (cursor กะพริบ)
- พิมพ์ข้อความ → insert ที่ cursor (สูงสุด 47 ตัวอักษร)
- Backspace → ลบตัวอักษรก่อน cursor
- ← → → เลื่อน cursor
- Enter → ยืนยัน (ออกจากโหมดแก้ไข)
- ป้อนข้อความผ่าน `InputEvent::TextInput` → `InputState::text_input[]`
- ต้องเพิ่ม `InputEvent::make_text_input(c)` ใน platform layer (`keyDown:` macOS)
- Widget flags: `WF_Visible | WF_Enabled | WF_Focusable` (ตั้งอัตโนมัติ)

---

## Sound System (Sprite-Bound SFX)

Sound effects can be attached at three levels: **SpriteFrame** (per sprite type), **Scene entity** (per instance), and **game logic** (manual trigger).

### Sound Registry

```cpp
// Register sounds once at init — returns uint8_t ID
uint8_t sfx_explode = g_audio_system.sfx.register_sound("sfx/explosion.wav");
uint8_t sfx_hit     = g_audio_system.sfx.register_sound("sfx/hit.wav");

// Play by ID
g_audio_system.sfx.play_id(sfx_explode, 128);           // priority=128
g_audio_system.sfx.play_id(sfx_hit, 100, 0.9f);          // with pitch shift
```

### SpriteFrame Level

`sfx_id` ใน SpriteFrame เป็น default sound ที่ entity จะ inherit ตอน spawn:

```cpp
SpriteFrame explosion = { tex, 0, 0, 64, 64, 64, 64, 0.5f, 0.5f, sfx_explode };
```

เมื่อ `Scene::spawn(x, y, &explosion)` ถูกเรียก → entity จะ inherit `sfx_spawn = explosion.sfx_id` และ auto-play spawn sound.

### Scene Entity Level

แต่ละ entity มี 3 sound slots:

| Field | Auto-play เมื่อ | ใช้กับ |
|-------|----------------|--------|
| `scene.sfx_spawn[i]` | `spawn()` | เสียงเกิด |
| `scene.sfx_hit[i]` | — (game logic เรียกเอง) | เสียงโดนชน |
| `scene.sfx_death[i]` | `despawn_at()` | เสียงตาย |

```cpp
// Override per entity (ต่างจาก default ของ SpriteFrame)
scene.sfx_hit[idx] = g_audio_system.sfx.register_sound("sfx/block_hit.wav");

// แล้วใน game loop เวลาชน:
if (scene.sfx_hit[idx] != UINT8_MAX)
    g_audio_system.sfx.play_id(scene.sfx_hit[idx]);
```

### Game Logic Level

Manual trigger สำหรับ event ที่ไม่เกี่ยวกับ entity โดยตรง:

```cpp
g_audio_system.sfx.play_id(sfx_combo, 200, 1.2f);  // combo sound
```

---

## 5. ตัวอย่างโค้ด (Code Examples)

ตัวอย่างที่สมบูรณ์อยู่ในไดเรกทอรี `examples/`:

| ตัวอย่าง | คำอธิบาย |
|----------|----------|
| `mm_01_sprite_10k` | Sprite batching 10,000 sprites |
| `mm_02_cube_100` | 3D cube rendering |
| `mm_03_match3_board` | Match-3 board game |
| `mm_04_block_puzzle` | Block puzzle game |
| `mm_05_particle_stress` | Particle system stress test |

## 6. API Reference

### Core (`core/`)

| ไฟล์ | คลาส/ฟังก์ชัน | คำอธิบาย |
|------|--------------|----------|
| `mm_arena.hpp` | `FrameArena` | Linear bump allocator, reset per frame |
| `mm_arena.hpp` | `DoubleArena` | Double-buffered frame arena |
| `mm_pool.hpp` | `Pool` | Fixed-size slab allocator, O(1) alloc/free |
| `mm_pool.hpp` | `GlobalPoolAllocator` | Multi-class (32/48/96/256/4096) pool |
| `mm_slotmap.hpp` | `Slotmap<T>` | Generation-based slotmap, chunked (64/ chunk) |
| `mm_handle.hpp` | `SlotHandle` | 24-bit id + 8-bit gen handle |
| `mm_handle.hpp` | `TypedHandle<Tag>` | Strongly-typed handle wrapper |
| `mm_expected.hpp` | `Expected<T,E>` | Error handling without exceptions |
| `mm_vfs.hpp` | `Vfs` | Platform file abstraction (bundle/doc) |
| `mm_vfs.hpp` | `AssetLoader` | Async file loading via job system |
| `mm_ring_buffer.hpp` | `RingBuffer<T,N>` | Lock-free SPSC ring buffer |
| `mm_job_system.hpp` | `JobSystem` | Thread pool with dependency support |
| `mm_cache_metrics.hpp` | `CacheCounters` | Debug counters for alloc/lookup/overflow |

### RHI (`rhi/`)

| ไฟล์ | คลาส/Concept | คำอธิบาย |
|------|-------------|----------|
| `mm_rhi_concept.hpp` | `RHI_Backend` concept | Interface ที่ backend ต้อง implement |
| `mm_rhi_concept.hpp` | Buffer/Texture/Pipeline/Sampler enums | Resource types |
| `mm_metal_backend.hpp` | `MetalBackend` | Metal backend (iOS/macOS) |
| `mm_vulkan_backend.hpp` | `VulkanBackend` | Vulkan backend (Android) |

### Render (`render/`)

| ไฟล์ | คลาส/ฟังก์ชัน | คำอธิบาย |
|------|-------------|----------|
| `mm_renderer.hpp` | `Renderer` | High-level renderer, manages pipelines + materials, set_scissor() |
| `mm_render_graph.hpp` | `Command`, `CmdType` | Command buffer as tagged union array, includes SetScissor/SetViewport |
| `mm_sort_key.hpp` | `SortKey` | Packed uint64_t draw call sort key |
| `mm_sprite_batch.hpp` | `SpriteBatch` | Sprite batching, 16-bit half-float vertices, per-sprite tex_id via register_texture() |
| `mm_sprite.hpp` | `SpriteFrame`, `SpriteAtlas` | Sprite frame data + atlas container, sfx_id per frame |
| `mm_sprite_loader.hpp` | `load_sprite_atlas()` | `.sprite` text format parser |
| `mm_texture_loader.hpp` | `load_texture()` | stb_image-based texture loader |
| `mm_material.hpp` | `Material`, `Technique` | Pipeline+texture+sampler bundle |
| `mm_sort_key.hpp` | `LAYER_*` constants | Background, Grid, Pieces, Effects, UI, Overlay |

### Game (`game/`)

| ไฟล์ | คลาส | คำอธิบาย |
|------|------|----------|
| `mm_particle_pool.hpp` | `ParticlePool` | SoA particle system, 4096 particles |
| `mm_tween_pool.hpp` | `TweenPool` | SoA tween system, 512 tweens, 10 ease types |
| `mm_scene.hpp` | `Scene` | SoA entity pool, 4096 entities, per-entity sfx_spawn/hit/death auto-play |
| `mm_camera_trauma.hpp` | `CameraTrauma` | Screen shake via Perlin-like noise |
| `mm_game_state.hpp` | `GameState`, `StateMachine` | Hierarchical state machine |

### App (`app/`)

| ไฟล์ | คำอธิบาย |
|------|----------|
| `mm_app.hpp` | Platform abstraction: `AppCallbacks`, `markmos_main()` |
| `mm_app_mac.mm` | macOS: NSApplication + CVDisplayLink + MetalKit |
| `mm_app_ios.mm` | iOS: UIApplication + MetalKit |
| `mm_app_android.cpp` | Android: NativeActivity + AAssetManager |
| `mm_input.hpp` | `InputAction`, `InputState` — action-based input abstraction |
| `mm_audio_system.hpp` | `AudioSystem` | Top-level: owns ma_engine + SfxPool + MusicLayer |
| `mm_audio_system.hpp` | `SfxPool` | 16-voice SFX pool, priority-based, register_sound()/play_id() |
| `mm_audio_system.hpp` | `MusicLayer` | 2-track music crossfade |

### UI (`ui/`)

| ไฟล์ | คลาส/ฟังก์ชัน | คำอธิบาย |
|------|-------------|----------|
| `mm_ui.hpp` | `Widget` | Flat POD widget node (104 bytes), 6 types: Panel/Label/Button/Toggle/Slider/TextField, WF_Clip/WF_Focusable flags |
| `mm_ui.hpp` | `Manager` | Widget pool (128), freelist, focus, cursor, editing, per-widget material override, layout (HBox/VBox), three-pass render + on_draw, scissor clipping |
| `mm_ui.hpp` | `DrawCallback` | `void (*)(uint16_t id, Renderer&, SpriteBatch&, float ax, float ay, float dt)` |
| `mm_ui.hpp` | `ChangeCallback` | `void (*)(uint16_t id, float value)` — slider change callback |
| `mm_ui.hpp` | `ui_lighten()` | เพิ่มความสว่างสีใน engine format 0xAABBGGRR |
| `mm_ui.hpp` | `ui_darken()` | ลดความสว่างสีใน engine format 0xAABBGGRR |

### Entry Point

```
entry/mm_game_entry.cpp     —  ตัวอย่างเกมสมบูรณ์ (block fall + particles + camera shake + UI)
examples/mm_01_sprite_10k   —  Sprite batching demo
examples/mm_02_cube_100     —  3D rendering demo
examples/mm_03_match3_board —  Match-3 demo
examples/mm_04_block_puzzle —  Block puzzle demo
examples/mm_05_particle_stress — Particle system demo
```

---

## Build System

```bash
# macOS
cmake ../engine -DPLATFORM=mac && make -j$(sysctl -n hw.logicalcpu)

# iOS
cmake ../engine -DPLATFORM=ios && make -j$(sysctl -n hw.logicalcpu)

# Android
cmake ../engine -DPLATFORM=android && make -j$(nproc)
```

Options:
- `-DENGINE_ENABLE_ASSERT=ON` — เปิด debug checks + cache metrics
- `-DENGINE_FORCE_LOCAL_EXPECTED=ON` — บังคับใช้ Expected ตัวเอง ไม่ใช้ std::expected
- `-DENGINE_BUILD_EXAMPLES=ON` — สร้าง examples

## Troubleshooting / Diagnostic Commands

### Trigger a rebuild of specific files (touch + cmake --build)

```bash
# Touch changed headers then rebuild the game target
touch path/to/changed.hpp && cmake --build . --target markmos_game 2>&1 | tail -15

# Rebuild from scratch
cmake --build . --target markmos_game 2>&1
```

### Run the game with diagnostic output

```bash
# Run in background, let it render for a few seconds, then kill
cd build && (./markmos_game.app/Contents/MacOS/markmos_game &) && sleep 3 && kill %1 2>/dev/null; wait %1 2>/dev/null

# Check only the first few lines for startup errors
head -5 /path/to/saved/output

# Run with stderr captured
./markmos_game.app/Contents/MacOS/markmos_game 2>&1 | head -30
```

### Add Metal shader / pipeline compile error diagnostics

Insert these blocks into `create_pipeline()` in `engine/rhi/mm_metal_backend.hpp`:

**Vertex shader compile error** (after `device->newLibrary(vs_src, nullptr, &err)`):
```cpp
if (!vs_lib) {
    if (err) {
        const char *emsg = err->localizedDescription()->utf8String();
        fprintf(stderr, "VS COMPILE ERROR (entry=%s): %s\n",
                pdesc.vertex_shader.entry, emsg ? emsg : "???");
    } else {
        fprintf(stderr, "VS COMPILE FAIL (entry=%s) — no error info\n",
                pdesc.vertex_shader.entry);
    }
    return make_unexpected(RHIError::ShaderCompileFail);
}
```

**Fragment shader compile error** (after `device->newLibrary(fs_src, nullptr, &err)`):
```cpp
if (!fs_lib) {
    if (err) {
        const char *emsg = err->localizedDescription()->utf8String();
        fprintf(stderr, "FS COMPILE ERROR (entry=%s): %s\n",
                pdesc.fragment_shader.entry, emsg ? emsg : "???");
    } else {
        fprintf(stderr, "FS COMPILE FAIL (entry=%s) — no error info\n",
                pdesc.fragment_shader.entry);
    }
    return make_unexpected(RHIError::ShaderCompileFail);
}
```

**Pipeline compile error** (after `device->newRenderPipelineState(rpd, &err)`):
```cpp
if (!pipeline) {
    if (err) {
        const char *emsg = err->localizedDescription()->utf8String();
        fprintf(stderr, "PIPELINE COMPILE ERROR: %s\n",
                emsg ? emsg : "???");
    } else {
        fprintf(stderr, "PIPELINE COMPILE FAIL — no error info\n");
    }
    return make_unexpected(RHIError::PipelineCompileFail);
}
```

**Note:** These require `#include <cstdio>` at the top of the file.

### The 4 MSL shader compilation errors found on Metal 3+

| Symptom | Metal Error | Fix |
|---------|-------------|-----|
| `packed_half2` vertex attribute | `type 'packed_half2' is not valid for attribute` | Use `half2` instead |
| `uchar4` with `UChar4Normalized` format | `cannot be read using MTLAttributeFormatUChar4Normalized` | Use `float4` attribute type (Metal normalizes automatically) |
| `static constexpr` at program scope | `program scope variable must reside in constant address space` | Use `constant float X = ...` |
| `sample()` returns `float4` | `cannot initialize float with vec<float,4>` | Use `.r` to get single channel |

### Checking pipeline handle validity

Add this diagnostic to `bind_pipeline` in the render backend or render graph:

```cpp
if (handle.handle.id == 0xFFFFFFFF || handle.handle.id >= pipelines.capacity()) {
    fprintf(stderr, "BINDPIPELINE FAIL: handle.id=%u gen=%u\n",
            handle.handle.id, handle.handle.gen);
    return;
}
```

# Master Prompt: Mobile-First Game Engine v4.6

**Zero Virtual + Cache Optimized + VMA + vk-bootstrap + Casual 2D Ready**

> Changelog v4.5 -> v4.6:
>
> - แก้ `ExpectedLike/ErrorReturnLike` concept ให้คอมไพล์ได้จริง (แก้ syntax; ไม่บังคับ default-constructible ของ return type)
> - นิยาม `LightweightErrorView` ให้ชัดเจน: เป็น view แบบ non-owning / trivially-copyable / no allocation และต้องแปลงเป็น `Error` ได้
> - ปรับรูปแบบการตรวจชนิด `error()` ให้ตรวจที่ `decltype(r.error())` แทนการตรวจที่ตัว expected-type

---

## 0. Requirement Levels

- **MUST**: ต้องทำ และต้อง compile/run ได้ใน Phase ปัจจุบัน
- **SHOULD**: ควรทำเมื่อ toolchain/platform รองรับ หรือเมื่อมี benchmark ยืนยัน
- **MAY**: optional/future phase ห้ามทำให้ Phase 1 ซับซ้อนขึ้นโดยไม่จำเป็น

AI ต้อง generate โค้ดแบบ incremental:

- ทุก step ต้อง compile independently
- เพิ่ม minimal tests ตามความเสี่ยงของ module
- ห้าม implement feature ของ later phase ก่อนเวลาถ้าไม่จำเป็น
- ห้ามเพิ่ม abstraction ถ้ายังไม่มี duplication หรือ complexity จริง

---

## 1. Hard Technical Rules

### Language

- **MUST**: ใช้ C++23 เป็น baseline
- **MUST**: ใช้ `concepts` และ `consteval` เมื่อช่วย enforce compile-time contract ได้จริง
- **SHOULD**: ใช้ `std::expected` และ `std::mdspan` เมื่อ standard library ของ target รองรับ
- **MUST**: มี local fallback เช่น `Expected<T, E>` และ `Span2D/SpanND` สำหรับ mobile toolchain ที่ยังไม่รองรับครบ โดยการพัฒนา local fallback ต้องทำแบบ header-only ในช่วงเริ่มต้นของ Phase 1 และควบคุมด้วย macro toggle (เช่น `ENGINE_USE_STD_EXPECTED`)
- **MAY**: ใช้ `std::execution` หลังจาก verify toolchain แล้วเท่านั้น
- **MUST NOT**: ให้ Phase 1 job system พึ่ง `std::execution`

### Polymorphism

- **MUST NOT**: ใช้ `virtual`, `std::function`, `std::any` ใน hot path
- **MUST**: ใช้ `concept` + `template` สำหรับ closed compile-time interfaces
- **SHOULD**: ใช้ `std::variant` + `std::visit` เฉพาะ finite closed-set state ระดับสูง เช่น game state (Menu, Play, Pause)
- **MUST NOT**: ใช้ `std::variant` + `std::visit` ใน render loop หรือ hot loop อื่นๆ เพื่อหลีกเลี่ยง table-dispatch overhead และ binary size bloat
- **MUST**: command buffer หรือ dynamic command list ใน hot path ต้องใช้ `enum class` + trivially-copyable payload + `switch-case`
- **MUST**: game state ใช้ closed-set type เช่น `std::variant<MenuState, PlayState, PauseState, WinState, FailState>`

### Backend Selection

- **MUST**: เลือก backend หลักแบบ compile-time ด้วย target macro/template
- **MUST NOT**: มี runtime branch เพื่อเลือก Metal vs Vulkan ใน hot path
- **MUST**: แยก backend selection ออกจาก runtime GPU feature detection
- **SHOULD**: หลีกเลี่ยงการทำ Class Template บน High-Level class (เช่น `template<typename Backend> class Renderer`) เพื่อลด compile-time overhead และ header pollution; ให้ใช้ Conditional Type Aliasing แทน แล้วทดสอบด้วย static assert
- **MUST**: `Renderer` เป็น non-template และถือ `ActiveBackend` โดยตรง; การเลือก backend และ `static_assert(RHI_Backend<ActiveBackend>)` ต้องอยู่ในไฟล์เดียวกัน (หรือ module เดียวกัน) เพื่อจำกัด header pollution และ compile-time explosion จาก backend headers
- **SHOULD**: `Renderer` public header หลีกเลี่ยงการ include native backend headers หนัก เช่น Vulkan/Metal headers; ให้ย้าย backend-heavy implementation ไป `.cpp/.mm` หรือ private implementation unit เมื่อทำได้

```cpp
#if defined(TARGET_IOS) || defined(TARGET_MACOS)
  #define USE_METAL_BACKEND
  using ActiveBackend = MetalBackend;
#elif defined(TARGET_ANDROID)
  #define USE_VULKAN_BACKEND
  using ActiveBackend = VulkanBackend;
#endif

// Verified at compile time against RHI concept
static_assert(RHI_Backend<ActiveBackend>, "ActiveBackend must conform to RHI_Backend concept");

class Renderer {
  ActiveBackend backend; // Direct instantiation
};
```

### Data Layout & Memory

- **MUST**: hot data ใช้ SoA หรือ AoSoA
- **MUST NOT**: `new/delete/malloc/free` ใน game loop
- **MUST**: ใช้ Pool + Frame Arena + Ring Buffer สำหรับ allocation ที่ predictable
- **MUST NOT**: เก็บ raw pointer ข้ามระบบหรือ expose raw pointer ใน public API
- **MUST NOT**: public engine API คืน pointer/reference ไปยัง internal storage ที่ทำให้ lifetime เสี่ยง; ให้ใช้ handle หรือ value-return แทน
- **SHOULD**: อนุญาต temporary views/spans เช่น `std::span` ได้เฉพาะเมื่อ lifetime ชัดเจนและไม่สามารถ outlive owner/frame/system; public API ต้อง document view lifetime
- **SHOULD**: backend-private implementation สามารถถือ native pointer/handle ได้ ถ้ามี RAII/lifetime ownership ชัดเจน

### Handle Strategy

- **MUST**: object lifetime ใช้ `Handle = { uint32_t id, uint16_t gen }`
- **MUST**: cold object lookup ใช้ slotmap/chunked slotmap
- **MUST**: hot path data เช่น Particle, Tween, Transform ใช้ direct index + generation/check data แยก cold array
- **MUST**: handle resolution จาก slotmap เป็น dense index ต้องเกิดใน phase แยกก่อนเข้า hot loop; hot loop รับ `std::span<uint32_t>` (หรือ equivalent) ของ dense indices เท่านั้น
- **MUST NOT**: lookup slotmap ซ้ำใน inner loop ถ้าสามารถ resolve เป็น direct index ก่อนเข้า hot loop ได้
- **MUST**: มี instrumentation สำหรับนับ `slotmap_lookup_in_hot_loop` และต้องถูก validate ใน CI ให้เป็น 0 ตาม threshold ในส่วน Cache Metrics

---

## 2. Architecture Layers

```text
[App Layer] mm_app_ios.mm / mm_app_android.cpp / mm_app_mac.mm
  - Window, input, touch, lifecycle, safe area, AAssetManager/NSBundle
  - Custom native platform glue (NOT sokol_app unless explicitly noted)
  - No virtual in hot path
  - Function pointer table allowed only during init/boundary glue

[Platform Glue]
  - Event pump, VFS, fixed-size thread pool, time, assert, log, haptic, save/load
  - Thread-local arena for temporary work

[Game Layer]
  - BoardSystem, GameStateMachine, TweenSystem, AudioSystem
  - Pure logic, no rendering dependency
  - Closed-set state via variant or tagged union

[Renderer]
  - SpriteBatch SoA, ParticleSystem, TextRenderer, LayeredDraw
  - Uses ActiveBackend directly (resolved at compile time via type alias)

[MetalBackend / VulkanBackend]
  - Flat structs, no inheritance, no virtual
  - Native API handles are backend-private

[RHI]
  - BufferHandle, TextureHandle, PipelineHandle = compact IDs
  - Cold resource lifetime via slotmap
  - Hot draw packet uses direct index/resolved references
```

---

## 3. Core Systems

| System | Rule | Reason |
|---|---|---|
| Memory | `rpmalloc` optional + fixed pools `<32B,48B,96B,256B,4KB>` + FrameArena + RingBuffer | Covers Particle, Tween, TextPopup, transient upload |
| Handle | Slotmap cold + direct index hot | Prevents dangling handles while avoiding pointer chasing in hot loops |
| Mesh/Sprite Data | SoA: transforms, bounds, resource IDs, sort keys | Linear iteration, prefetch-friendly |
| Scene | Phase 1 flat array + `uint64_t` sort key | One sort per frame, stable draw order |
| Render Graph | Linear command buffer with enum + payload | Predictable branch pattern |
| Job System | Phase 1 fixed-size thread pool + bounded task queue | Debuggable, deterministic enough, low complexity |
| Job System Phase 2 | MAY use work stealing/fibers only if profiling shows idle CPU >25% | Avoid premature scheduler complexity |
| Assets | VFS + async load + upload ring buffer + per-level preload | Avoid load-all-at-start |
| Math | SIMD wrapper + aligned allocations; `xsimd` SHOULD be used where supported, else fallback to ARM NEON intrinsics on mobile | Keeps SIMD optional per target |
| Cache Metrics | Instrumented counters + optional hardware counters | CI must remain practical across hosts |

---

## 4. RHI Concept

```cpp
// LightweightErrorView:
// - non-owning view of an error, used to avoid copying large error payloads
// - MUST be trivially copyable and MUST NOT allocate
// - MUST be convertible to the engine's canonical Error (or provide `.code()` that maps to Error)
template<typename V, typename E>
concept LightweightErrorView =
  std::is_trivially_copyable_v<std::remove_cvref_t<V>> &&
  (std::same_as<std::remove_cvref_t<E>, Error>) &&
  requires(const std::remove_cvref_t<V>& v) {
    // Either explicit conversion:
    { static_cast<Error>(v) } -> std::same_as<Error>;
  };

template<typename ER, typename E>
concept ErrorReturnLike =
  std::same_as<std::remove_cvref_t<ER>, E> ||
  std::same_as<std::remove_cvref_t<ER>, const E> ||
  std::same_as<std::remove_cvref_t<ER>, const E&> ||
  LightweightErrorView<ER, E>;

// ExpectedLike<R, T, E>:
// - R is the return type (e.g., std::expected<T,E> or fallback Expected<T,E>)
// - MUST NOT require R to be default-constructible
template<typename R, typename T, typename E>
concept ExpectedLike =
  requires(const R& cr) {
    { cr.has_value() } -> std::same_as<bool>;
    requires ErrorReturnLike<decltype(cr.error()), E>;
  } &&
  (std::is_void_v<T> || requires(R& r) {
    { *r } -> std::same_as<T&>;
  });

template<typename T>
concept RHI_Backend = requires(T t) {
  requires ExpectedLike<decltype(t.create_buffer(BufferDesc{})), BufferHandle, Error>;
  { t.destroy_buffer(BufferHandle{}) } -> std::same_as<void>;
  requires ExpectedLike<decltype(t.update_buffer(BufferHandle{}, nullptr, 0, 0)), void, Error>;
  requires ExpectedLike<decltype(t.begin_frame()), void, Error>;
  requires ExpectedLike<decltype(t.end_frame()), void, Error>;
  requires std::is_nothrow_destructible_v<T>;
};
```

`ExpectedLike<R, T, E>` ต้องรองรับได้ทั้ง `std::expected<T, E>` และ fallback `Expected<T, E>` โดยอย่างน้อยต้องมี:

- `bool has_value() const`
- `T& operator*()` หรือ `const T& operator*() const` สำหรับ non-void
- `error()` ที่คืน `E`, `const E&`, หรือ lightweight error view ได้ โดยต้องไม่ allocate ใน hot path

และสำหรับ `ExpectedLike<R, void, E>` ต้องมี:

- `bool has_value() const`
- `error()` ที่คืน `E`, `const E&`, หรือ lightweight error view ได้ โดยต้องไม่ allocate ใน hot path

Backend object ไม่จำเป็นต้อง trivially destructible เพราะ backend จริงอาจถือ RAII/native resources; แต่ destructor ต้องเป็น `noexcept` และ ownership ต้องชัดเจน

### Draw Call Sort Key

```cpp
key = (layer_id << 56)
    | (pipeline_id << 44)
    | (material_id << 28)
    | depth;
```

Layers:

```text
BACKGROUND(0) -> GRID(1) -> PIECES(2) -> EFFECTS(3) -> UI(4) -> OVERLAY(5)
```

---

## 5. Cache Rules

- **MUST**: hot/cold split ข้อมูลที่อ่านทุกเฟรมออกจาก metadata/debug/string/lifetime data
- **MUST**: batch update APIs รับ `start_index, count` เพื่อเปิดทาง SIMD/prefetch
- **MUST**: no string in hot path; ใช้ `StringID = uint32_t hash`
- **MUST**: UBO/constant buffer layout ต้องอยู่ใน limit ของ backend; ถ้าเกินใช้ SSBO/storage buffer หรือ texture buffer
- **MUST**: pool size ต้องอิงจาก object จริงและมี assert เมื่อ overflow
- **SHOULD**: align allocation base pointer สำหรับ SoA arrays เป็น 16/32/64 bytes ตาม SIMD/cache need
- **SHOULD**: ใช้ `alignas(64)` เฉพาะ shared counters, per-thread queues, allocator metadata, หรือ structs ที่เสี่ยง false sharing
- **MUST NOT**: `alignas(64)` ทุก element ใน hot array โดยไม่มี benchmark เพราะอาจทำให้ memory bloat และ cache density แย่ลง

---

## 6. Backend Requirements & Fallback

| Backend | Core Lib | MUST | MUST NOT |
|---|---|---|---|
| Metal | `metal-cpp` | command buffer, heap/resource reuse, pipeline cache/binary archive where available | Obj-C message send in frame hot loop, NSObject subclass in engine core |
| Vulkan | Vulkan 1.1 minimum (Phase 1), Vulkan 1.3 preferred for future phases, VMA, vk-bootstrap | VMA allocator, vk-bootstrap setup, pipeline cache, explicit sync | validation layers in release, manual per-frame allocation, direct `vkAllocateMemory` |

### Vulkan Rules

- **MUST**: ใช้ `vk-bootstrap` สำหรับ instance/device/swapchain setup แทน hand-written setup boilerplate; backend-private code ยังสามารถใช้ raw `Vk*` handles ได้เมื่อ ownership/lifetime ชัดเจน
- **MUST**: ใช้ VMA สำหรับ buffer/image memory
- **MUST NOT**: เขียน `vkAllocateMemory` เอง ยกเว้น test/fallback ที่มีเหตุผลและ tag อธิบายชัด
- **MUST NOT**: ใช้ `vkDeviceWaitIdle` ใน frame loop
- **MUST**: Phase 1 รองรับอย่างน้อย Vulkan 1.1 พร้อม explicit render pass/framebuffer path และ sync แบบ binary semaphore/fence ที่ไม่มี timeline/dynamic rendering
- **SHOULD**: ใช้ timeline semaphore และ dynamic rendering เมื่อ runtime device รองรับ และแยกออกเป็น path เพิ่มใน Phase 1.5/Phase 2 โดยไม่ทำให้ Phase 1 ซับซ้อนเกินจำเป็น
- **MUST**: ตรวจ format/compression support ตอน runtime เช่น ASTC/ETC2

Runtime feature detection example:

```cpp
struct VulkanFeatures {
  bool dynamic_rendering;
  bool timeline_semaphore;
  bool astc;
  bool etc2;
};

// Backend selection is compile-time.
// GPU feature fallback is runtime and happens outside hot draw loops.
```

### Shader Pipeline

- **MUST**: single source shader path ชัดเจน เช่น HLSL -> SPIR-V -> SPIRV-Cross -> MSL
- **MUST**: cache compiled bytecode/pipeline artifacts
- **SHOULD**: hot reload เฉพาะ debug/profile builds

---

## 7. Third Party Libraries

Allowed:

```text
stb_image
stb_truetype
metal-cpp
simdjson
miniaudio
imgui debug-only
rpmalloc optional
xsimd optional
spirv-cross
tracy optional/profile
VulkanMemoryAllocator
vk-bootstrap
```

Rules:

- **MUST**: public engine API wrap third-party resources with handles or owned RAII wrappers
- **MUST NOT**: leak third-party raw pointers through game-facing APIs
- **SHOULD**: backend-private code may use native handles directly when ownership and lifetime are local and documented

---

## 8. Casual 2D Game Systems

- **Board/Grid**: SoA + flat array + bitmask scan
- **Particle**: GPU instanced when possible + swap-with-last CPU pool
- **Tween**: compact pool + ease function table + chain/parallel support
- **TextPopup**: SoA + float-up + fade + max concurrent cap
- **CameraTrauma**: `trauma *= 0.9f`, offset proportional to `trauma * trauma`, haptic boundary
- **Touch Gesture**: FSM `Idle -> Press -> Tap/Swipe/Drag/LongPress`, max 5 fingers
- **Audio**: `miniaudio`, SfxPool, music crossfade, priority queue
- **Save/Load**: binary `SaveData` + version + CRC32 + async write + atomic rename

Every effect MUST declare:

- `spawn_pool`
- `max_lifetime`
- `max_concurrent_instances`
- perf tag/profiling marker point

---

## 9. Text Rendering & Thai Support

### Layer Strategy

- **Layer A**: Bitmap font for score/timer/digits
- **Layer B**: SDF font for UI/popup/button text

### Thai / Unicode

- **MUST**: support fixed vocabulary Thai text in Phase 1
- **MUST**: atlas split:
  - `atlas_en`: ASCII + digits, always loaded
  - `atlas_th`: U+0E00..U+0E7F, load-on-demand
- **MUST**: รองรับ fixed vocabulary Thai text ด้วย static layout map เป็นเส้นทางหลัก รวมถึงข้อมูล 4-Level Vertical Stacking (Level 0: ล่าง, Level 1: ฐาน, Level 2: บนสระ, Level 3: บนวรรณยุกต์) สำหรับคำที่กำหนดไว้ล่วงหน้า
- **SHOULD**: ใช้การคำนวณสะสม Y-offset เป็น heuristic fallback เมื่อไม่มี entry ใน layout map โดยจำกัดเฉพาะ subset ที่ทดสอบแล้ว
- **SHOULD**: ลดการทับซ้อนด้วย X-offset เล็กน้อยทางซ้ายเมื่อตรวจพบคู่พยัญชนะฐานที่มีหางยาว (ป, ฝ, ฟ, ฬ) กับสระบน/วรรณยุกต์ (Ascender Collision Avoidance) เฉพาะชุดคู่ตัวอักษรที่ระบุในตาราง lookup ที่แนบมากับเกม (ไม่อ้างว่า general-purpose Thai shaping)
- **MUST**: UTF-8 -> codepoint -> glyph lookup -> quad emit into SpriteBatch
- **SHOULD**: use precomposed/static layout map for known game vocabulary
- **MAY**: add HarfBuzz-lite/full HarfBuzz for dynamic text in Phase 2
- **MUST NOT**: claim full Thai shaping, bidi, or complex line breaking in Phase 1

---

## 10. Phase 1 Feature Set

Phase 1 is strictly casual 2D-first:

- 2D RHI skeleton
- Sprite batch
- Board/grid
- Tween
- Particle
- Text popup
- Touch input
- Audio
- Save/load
- State machine
- Mobile basics: safe area, lifecycle, haptic, dynamic resolution hooks, thermal hooks
- Texture compression fallback: ASTC preferred, ETC2 fallback where needed

### Phase 1.5 / Phase 2

- 3D basic rendering
- PBR
- IBL
- shadows
- fiber job system
- full text shaping
- editor
- scripting VM

---

## 11. Non-Goals Phase 1

Network, physics engine, scripting VM, editor, complex animation graph, full ECS, multiplayer, bidi text, full dynamic complex text shaping, complex particle physics, PBR/IBL/shadow renderer.

---

## 12. Performance Targets & CI Validation

Target devices:

- iPhone 12 class
- Snapdragon 865 class

Targets:

- CPU frame time < 4ms for core 2D test scene
- GPU frame time < 12ms for core 2D test scene
- Memory < 150MB for sample game scene
- Zero allocations per frame after warmup

### Cache Metrics

Hardware cache metrics are platform-dependent. Therefore:

- **MUST**: CI validate zero allocations/frame
- **MUST**: CI validate stable microbench frame time for hot systems
- **MUST**: CI validate handle lookup ratio / pointer-chase instrumentation
- **MUST**: CI validate pool overflow behavior
- **SHOULD**: validate hardware counters such as L2 hit and D-cache miss on hosts/devices that support them
- **MAY**: use device-specific baselines after real measurements exist

Suggested thresholds before device-specific baselines:

```text
allocations_per_frame == 0 after warmup
slotmap_lookup_in_hot_loop == 0
ptr_chase_ratio < 2% in instrumented hot systems
frame_time_regression < 10% from checked-in baseline
```

Example CI shape:

```yaml
steps:
  - cmake --preset ci -DENGINE_ENABLE_ASSERT=ON -DENGINE_ENABLE_TRACY=OFF
  - ninja
  - ninja backend_concept_tests
  - ninja hot_system_microbench
  - ninja allocation_regression_test
  - ninja handle_lookup_regression_test
```

Profile builds:

```yaml
steps:
  - cmake --preset profile -DENGINE_ENABLE_TRACY=ON
  - ninja
  - run_profile_capture_optional
```

---

## 13. Build & Config Matrix

```cmake
option(ENGINE_ENABLE_TRACY "Enable Tracy profiler markers" ON)
option(ENGINE_ENABLE_ASSERT "Enable runtime assert/bounds checks" ON)
option(ENGINE_HOT_RELOAD_SHADERS "Enable filesystem watch for SPIR-V/MSL" OFF)
option(ENGINE_FORCE_SIMD_BACKEND "Force SIMD backend: auto/xsimd/neon/sse/scalar" "auto")
option(ENGINE_USE_STD_EXPECTED "Use std::expected when available" ON)
option(ENGINE_USE_STD_MDSPAN "Use std::mdspan when available" ON)
option(ENGINE_ENABLE_EXCEPTIONS "Enable C++ exceptions in non-core code" OFF)
option(ENGINE_ENABLE_RTTI "Enable RTTI in non-core code" OFF)
```

Presets:

- `debug`: asserts, validation layers, optional Tracy
- `profile`: O2, Tracy, symbols, no validation layers unless requested
- `release`: O3/LTO, strip, no Tracy, no validation layers
- `ci`: deterministic tests, concept tests, allocation/cache regression tests

---

## 14. Deliverable Structure

```text
engine/
├── rhi/
│   ├── rhi_concept.hpp
│   ├── metal_backend.hpp
│   └── vulkan_backend.hpp
├── core/
│   ├── expected.hpp
│   ├── span2d.hpp
│   ├── slotmap.hpp
│   ├── arena.hpp
│   ├── handle.hpp
│   ├── ring_buffer.hpp
│   ├── cache_metrics.hpp
│   └── save_data.hpp
├── game/
│   ├── board_grid.hpp
│   ├── game_state.hpp
│   ├── tween_pool.hpp
│   ├── particle_pool.hpp
│   ├── text_popup_pool.hpp
│   └── camera_trauma.hpp
├── render/
│   ├── render_graph.hpp
│   ├── sort_key.hpp
│   ├── sprite_batch.hpp
│   └── text_renderer.hpp
├── audio/
│   └── audio_system.hpp
├── input/
│   └── touch_gesture.hpp
├── app/
│   ├── mm_app_ios.mm
│   ├── mm_app_android.cpp
│   └── mm_app_mac.mm
├── tools/
│   └── shader_hotloader.hpp
└── examples/
    ├── 01_sprite_10k.cpp
    ├── 02_match3_board.cpp
    ├── 03_block_puzzle.cpp
    └── 04_particle_stress.cpp
```

---

## 15. Implementation Order

1. Build system + `expected.hpp` + `span2d.hpp` + `handle.hpp` compile (Develop fallback first in header-only format)
2. `rhi_concept.hpp` + backend concept tests compile
3. `slotmap.hpp` + `arena.hpp` + `ring_buffer.hpp` + allocation tests
4. `cache_metrics.hpp` with instrumentation-only counters
5. `MetalBackend` stub passes concept
6. `VulkanBackend` setup with `vk-bootstrap` + VMA, no hand-written instance/device/swapchain boilerplate, no direct `vkAllocateMemory`
7. Vulkan runtime feature detection + fallback flags
8. `sort_key.hpp` + `render_graph.hpp`
9. `sprite_batch.hpp` renders quads
10. `board_grid.hpp` + `game_state.hpp`
11. `tween_pool.hpp` + `particle_pool.hpp` + `text_popup_pool.hpp`
12. `text_renderer.hpp`: bitmap first, then SDF, then fixed-vocabulary Thai combining support (4-level stacking & ascender collision check)
13. `audio_system.hpp` + `touch_gesture.hpp` + `save_data.hpp`
14. `mm_app_*.mm/.cpp` entry points
15. examples + CI regression tests
16. optional profile/Tracy capture

---

## 16. Design Decision Tags

Every non-trivial system header MUST answer:

1. `@cache_reason`: ทำไม layout นี้ cache-friendly กว่า object graph/OOP แบบเดิม
2. `@zero_virtual`: ทำไมไม่ใช้ virtual และ dispatch ด้วยอะไร
3. `@handle_strategy`: ใช้ slotmap หรือ direct index เพราะอะไร
4. `@pool_bound`: spawn จาก pool ใด, lifetime <= X, max instances <= Y
5. `@fallback`: fallback low-end/mobile/toolchain คืออะไร
6. `@instrumentation`: มี metric/test อะไรวัด regression

Example:

```cpp
/// @cache_reason SoA arrays keep transform update linear and prefetch-friendly.
/// @zero_virtual Closed command set uses enum dispatch instead of virtual calls.
/// @handle_strategy Slotmap owns lifetime; hot loop receives resolved direct indices.
/// @pool_bound ParticlePool, lifetime <= 1.25s, max instances <= 4096.
/// @fallback Scalar update path when SIMD backend is unavailable.
/// @instrumentation Tracks allocations/frame and hot-loop slotmap lookups.
```

Hard constraints for core engine code:

- **MUST**: ไม่ใช้ C++ exceptions และ engine core target ต้อง force `-fno-exceptions` (หรือเทียบเท่า) เสมอ ไม่ขึ้นกับ app-level option
- **MUST**: ไม่ใช้ RTTI (`dynamic_cast`, `typeid`) ใน engine core และ engine core target ต้อง force `-fno-rtti` (หรือเทียบเท่า) เสมอ ไม่ขึ้นกับ app-level option
- **MUST NOT**: ให้ public engine API คืน pointer/reference ไปยัง internal storage ที่ทำให้ lifetime เสี่ยง; ให้ใช้ handle, value-return, หรือ documented temporary view/span แทน

Tracy:

- **SHOULD**: markers at subsystem boundaries in profile builds
- **MUST NOT**: make CI depend on Tracy server availability
- **MUST**: compile out cleanly in release

---

## 17. Output Requirements For Code Generation

When asked to generate code:

- Generate one implementation step at a time unless explicitly asked for all files
- Include exact files changed/created
- Include compile/test commands
- Include known limitations
- Keep code minimal but extensible
- Do not add Phase 2 systems while implementing Phase 1
- Prefer correctness and compilability over theoretical maximum performance

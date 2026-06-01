# Markmos Engine Audit Checklist Against `master_prompt_v4_6.md`

วันที่ตรวจ: 2026-05-28  
ขอบเขต: ตรวจเฉพาะโค้ดโปรเจกต์หลัก (`core/`, `rhi/`, `render/`, `game/`, `audio/`, `input/`, `app/`, `examples/`, `shaders/`, `CMakeLists.txt`) ไม่รวม vendor code ใน `thirdparty/`

## สรุปผู้บริหาร

โปรเจกต์นี้เขียนตามเจตนาของ `master_prompt_v4_6.md` ได้ค่อนข้างชัดในระดับโครงสร้างและแนวคิดหลัก เช่น C++23, zero virtual, SoA, handle, slotmap, frame arena, render command enum, Metal/Vulkan backend, VMA, vk-bootstrap, ระบบ casual 2D หลัก และ Thai text skeleton

แต่สถานะปัจจุบันยังไม่ผ่าน master prompt แบบสมบูรณ์ เพราะมี build blocker, CI/test ยังไม่มี, instrumentation ยังไม่ถูกผูกเข้ากับ hot path, บาง hot path มี stack allocation เสี่ยง, audio ยัง allocate ตอน play, Thai layout ยังไม่ถึงข้อกำหนด 4-level/static vocabulary, และ Vulkan path ใช้ Vulkan 1.3/dynamic rendering/timeline semaphore เป็น baseline ซึ่งข้ามข้อกำหนด Phase 1 ที่ต้องรองรับ Vulkan 1.1 fallback

คำตัดสินรวม: **Partial Compliance / Prototype Skeleton**  
ควรแก้ก่อนถือว่าเป็น Phase 1-ready: **compile correctness, Expected/std::expected behavior, slotmap correctness, render graph safety, CI tests, no per-frame allocation metrics**

## หลักฐานจากการ build

- Configure ผ่านด้วยคำสั่ง:
  - `cmake -S . -B /private/tmp/markmos-audit-build -DBUILD_EXAMPLES=OFF`
- Configure เตือนว่าไม่มี shader tools:
  - `dxc or spirv-cross not found - shaders will not be compiled`
- Build ไม่ผ่านด้วยคำสั่ง:
  - `cmake --build /private/tmp/markmos-audit-build --target markmos`
- สาเหตุหลัก:
  - `rhi/mm_metal_backend.hpp` คืน `Error::...` ตรง ๆ จากฟังก์ชันที่คืน `Expected<T, Error>` เมื่อ `Expected = std::expected` ทำให้ compile ไม่ผ่าน
  - `game/mm_tween_pool.hpp` ใช้ `TweenHandle{Handle{idx, 0, 0}}` ทั้งที่ไม่มี type `Handle` ในโค้ดชุดนี้
  - macro `USE_METAL_BACKEND` ถูก define ซ้ำจาก CMake และ `rhi/mm_rhi_concept.hpp`

## Checklist ระดับ Must-Fix

### 1. Build & Config

- [x] ใช้ C++23 baseline ใน `CMakeLists.txt`
  - หลักฐาน: `CMAKE_CXX_STANDARD 23`
- [x] engine target force no exceptions/no RTTI
  - หลักฐาน: `-fno-exceptions`, `-fno-rtti`
- [ ] แก้ build ให้ผ่านเมื่อ `ENGINE_USE_STD_EXPECTED=ON`
  - ปัญหา: `std::expected` ต้องคืน error ด้วย `std::unexpected(Error::...)`
  - จุดที่เห็นชัด: `rhi/mm_metal_backend.hpp` บรรทัดที่คืน `Error::BackendError`, `Error::OutOfMemory`, `Error::InvalidHandle`, `Error::DeviceLost`
  - Acceptance: `cmake --build /private/tmp/markmos-audit-build --target markmos` ผ่านบน macOS
- [ ] ทำให้ fallback `Expected<T,E>` และ `std::expected<T,E>` ใช้ call-site รูปแบบเดียวกัน
  - ทางเลือกที่แนะนำ: เพิ่ม helper เช่น `unexpected_error(Error e)` หรือ `make_error<T>(Error e)` แล้วใช้สม่ำเสมอ
  - Acceptance: build ผ่านทั้ง `ENGINE_USE_STD_EXPECTED=ON` และ `ENGINE_FORCE_LOCAL_EXPECTED=ON`
- [ ] แก้ `TweenHandle` construction
  - ปัญหา: `game/mm_tween_pool.hpp` ใช้ `Handle{...}` แต่ type จริงคือ `SlotHandle`
  - Acceptance: `TweenPool::spawn()` คืน `TweenHandle{SlotHandle{idx, 0, 0}}` หรือ factory ที่ชัดเจน
- [ ] แก้ macro redefinition ของ backend
  - ปัญหา: CMake define `USE_METAL_BACKEND` แล้ว `rhi/mm_rhi_concept.hpp` define ซ้ำ
  - Acceptance: ไม่มี warning `macro redefined`
- [ ] เพิ่ม CMake options ให้ครบตาม prompt
  - ขาด/ไม่ตรงชื่อ: `ENGINE_ENABLE_TRACY`, `ENGINE_HOT_RELOAD_SHADERS`, `ENGINE_USE_STD_MDSPAN`, `ENGINE_ENABLE_EXCEPTIONS`, `ENGINE_ENABLE_RTTI`, `ENGINE_FORCE_SIMD_BACKEND`
- [ ] เพิ่ม presets หรือ CI build matrix
  - ต้องมีอย่างน้อย: `debug`, `profile`, `release`, `ci`

### 2. Expected / RHI Concept

- [x] มี `ExpectedLike`, `ErrorReturnLike`, `LightweightErrorView`
- [x] ไม่บังคับ default-constructible return type
- [x] backend destructor ตรวจด้วย `std::is_nothrow_destructible_v<T>`
- [ ] ปรับ `LightweightErrorView` ให้ตรง prompt v4.6 มากขึ้น
  - Prompt ระบุว่า error view ต้องสัมพันธ์กับ canonical `Error` และ convertible เป็น `Error`
  - โค้ดปัจจุบันยืดหยุ่นกว่าโดย `static_cast<E>(v)` แต่ควรล็อกกับ `Error` ตามข้อกำหนดถ้า engine มี error เดียว
- [ ] เพิ่ม backend concept tests
  - Acceptance: target เช่น `backend_concept_tests` compile ทั้ง `MetalBackend` และ `VulkanBackend` ตาม platform/toolchain

### 3. Zero Virtual / Polymorphism

- [x] ไม่พบ `virtual`, `std::function`, `std::any` ในโค้ดโปรเจกต์หลัก
- [x] RHI ใช้ concept/static assert
- [x] Game state ใช้ `std::variant`
- [x] Render command ใช้ `enum class` + union payload + switch
- [ ] จำกัด `std::variant/std::visit` ให้อยู่ระดับ state เท่านั้นและเพิ่ม test/grep guard
  - Acceptance: CI grep guard ยืนยันว่าไม่มี `std::visit` ใน `render/` hot loop

### 4. Backend Selection

- [x] มี compile-time backend alias `ActiveBackend`
- [x] `Renderer` เป็น non-template และถือ `ActiveBackend` ตรง ๆ
- [ ] ลด header pollution จาก backend-heavy includes
  - ปัญหา: `render/mm_renderer.hpp` include `mm_metal_backend.hpp` หรือ `mm_vulkan_backend.hpp` โดยตรง ทำให้ public renderer header หนัก
  - Acceptance: ย้าย backend-heavy implementation ไป `.cpp/.mm` หรือ private implementation unit ถ้าเป็นไปได้
- [ ] แยก backend macro definition ให้มี single source of truth
  - Acceptance: CMake/platform header ไม่ define ซ้ำกัน

### 5. Memory / Allocation / Hot Path

- [x] มี `FrameArena`, `DoubleArena`, `RingBuffer`
- [x] หลายระบบใช้ fixed-size arrays และ SoA
- [ ] แก้ `Slotmap` freelist ให้ถูกต้องจริง
  - ปัญหา: `free()` ตั้ง `free_head_ = idx` แต่ไม่ได้เก็บ next pointer; `emplace()` scan หา inactive ตัวถัดไป จึงไม่ใช่ freelist จริง และอาจเสีย reuse pattern
  - ปัญหาเพิ่มเติม: `Slot` ใช้ `alignas(64)` กับทุก element อาจขัด prompt ที่ห้าม `alignas(64)` ทุก element ใน hot array โดยไม่มี benchmark
  - Acceptance: slotmap มี explicit next-free index หรือ free stack และมี unit test create/free/reuse/generation
- [ ] ห้าม allocate ใน game loop อย่างมี metric พิสูจน์
  - ปัญหา: มี counters แต่ยังไม่มีการเรียก `TRACK_ALLOC()` ใน allocator/hot systems
  - Acceptance: allocation regression test ยืนยัน `allocations_per_frame == 0 after warmup`
- [ ] แก้ `RenderGraph::sort_radix()` ที่ใช้ `__builtin_alloca`
  - ปัญหา: alloca ขนาด `command_count * sizeof(Command)` อาจกิน stack หลาย MB เมื่อ command เยอะ และไม่ใช่ arena/ring buffer ตาม prompt
  - Acceptance: ใช้ temp buffer จาก `FrameArena` หรือ preallocated scratch buffer
- [ ] เพิ่ม bounds check/overflow handling ให้ `RenderGraph::add()`
  - ปัญหา: `commands[command_count++]` ไม่มี guard
  - Acceptance: overflow เพิ่ม metric `TRACK_POOL_OVERFLOW()` หรือคืนสถานะ error ใน debug/assert build
- [ ] แก้ Audio SFX ให้ไม่ heap allocate ระหว่าง `play()`
  - ปัญหา: `ma_malloc` ใน `SfxPool::play()`
  - Acceptance: preallocate `ma_sound` pool หรือใช้ owned fixed pool แล้ว decode/load ล่วงหน้า

### 6. Cache Metrics & CI

- [x] มี `CacheCounters` สำหรับ allocations, hot slotmap lookup, pool overflow
- [ ] นิยาม `g_cache_counters` ใน `.cpp`
  - ปัจจุบันมี `extern` แต่ไม่พบ definition ในโค้ดหลัก
- [ ] ผูก metrics เข้ากับ allocator/pool/slotmap hot lookup
  - Acceptance: `TRACK_ALLOC`, `TRACK_HOT_LOOKUP`, `TRACK_POOL_OVERFLOW` ถูกใช้จริง ไม่ใช่ macro เปล่า
- [ ] เพิ่ม CI validation ตาม prompt
  - ต้องมี target/test:
    - `backend_concept_tests`
    - `hot_system_microbench`
    - `allocation_regression_test`
    - `handle_lookup_regression_test`
    - `pool_overflow_test`

### 7. Renderer / Sprite / Render Graph

- [x] SpriteBatch ใช้ SoA
- [x] SortKey เป็น `uint64_t` และมี layer/pipeline/material/depth layout
- [x] RenderGraph ใช้ command buffer flat array
- [ ] `flush_sprites()` ยังไม่ upload vertex/index buffer หรือ submit draw จริง
  - ปัญหา: generate vertices แล้ว `temp_arena.reset()` ทิ้งข้อมูลโดยไม่เรียก `update_buffer()`/`draw_indexed()`
  - Acceptance: flush path สร้าง/ใช้ GPU buffers และ emit draw commands จริง
- [ ] แก้ atlas size hardcode `1024`
  - Acceptance: SpriteBatch/TextRenderer ใช้ atlas actual width/height
- [ ] ตรวจ sort correctness หลัง radix sort
  - ปัญหา: `sort_radix()` สลับ pointer `commands` กับ temp ที่มาจาก stack; ต้อง audit ว่าหลัง 4 pass pointer กลับมาถูกเสมอ และไม่ชี้ stack buffer หลัง return
  - Acceptance: unit test sort random keys และ nearly-sorted keys

### 8. Vulkan Backend

- [x] ใช้ `vk-bootstrap`
- [x] ใช้ VMA
- [x] ไม่พบ direct `vkAllocateMemory`
- [x] `vkDeviceWaitIdle` ใช้ตอน shutdown ไม่ใช่ frame loop
- [ ] ต้องรองรับ Vulkan 1.1 Phase 1 fallback
  - ปัญหา: backend require Vulkan 1.3, dynamic rendering, timeline semaphore เป็น baseline
  - Acceptance: มี path Vulkan 1.1 render pass/framebuffer + binary semaphore/fence และ runtime feature detection แยก path 1.3
- [ ] Surface creation ยังเป็น comment/stub
  - Acceptance: Android `ANativeWindow` surface path ทำงานจริง; mac/iOS ถ้าใช้ Vulkan ต้องมี surface path หรือปิดไม่ให้ build ผิด platform
- [ ] Runtime format/compression detection ยังไม่ครบ
  - Acceptance: ตรวจ ASTC/ETC2 support และเลือก fallback texture format
- [ ] ลด `std::vector` ใน backend hot/lifetime sensitive path หรือจำกัดเฉพาะ init/cold path พร้อม document

### 9. Metal Backend

- [x] ใช้ Metal backend-private native handles
- [x] ไม่มี NSObject subclass ใน engine core
- [ ] แก้ compile errors จาก `std::expected`
- [ ] ตรวจ ownership/release ให้ครบ
  - ปัญหา: `destroy_buffer()` เรียก `buffers.free()` แต่ไม่ได้ `[buffer release]` ก่อน free
  - Acceptance: ทุก native object มี explicit release หรือ RAII wrapper
- [ ] ลด Objective-C message send ใน frame hot loop เท่าที่ทำได้
  - Acceptance: audit `begin_frame`, `draw`, bind path และ document boundary ที่จำเป็น

### 10. Casual 2D Systems

- [x] Board/Grid เป็น flat array + SoA
- [x] ParticlePool เป็น SoA + swap-with-last
- [x] TweenPool เป็น compact pool + ease table
- [x] TextPopupPool มีในโครงสร้างโปรเจกต์
- [x] CameraTrauma มีในโครงสร้างโปรเจกต์
- [x] Touch gesture FSM รองรับ max 5 fingers
- [x] Audio ใช้ miniaudio wrapper
- [x] SaveData เป็น binary flat struct + CRC
- [ ] BoardGrid match encoding มี bug
  - ปัญหา: horizontal path บางจุดใช้ `matches[++match_count]` ทำให้ index 0 ถูกข้าม และ vertical/horizontal ไม่มี direction flag ที่เชื่อถือได้
  - Acceptance: unit test horizontal/vertical matches, mark matched, gravity
- [ ] `would_match()` copy ทั้ง `BoardGrid`
  - อาจรับได้เพราะ 16x16 เล็ก แต่ควร benchmark/document ถ้าอยู่ hot input path
- [ ] Tween ใช้ raw `float* target`
  - Prompt ห้าม raw pointer ข้ามระบบ/public API ที่ lifetime เสี่ยง
  - Acceptance: ใช้ handle/index หรือ documented temporary/local-only contract
- [ ] ทุก effect ต้องประกาศ `spawn_pool`, `max_lifetime`, `max_concurrent_instances`, perf tag/profiling marker
  - ปัจจุบันมี comment บางส่วนแต่ยังไม่ครบตาม required tags

### 11. Text Rendering & Thai

- [x] มี bitmap font + SDF font skeleton
- [x] มี UTF-8 decoder และ Thai codepoint lookup range `U+0E00..U+0E7F`
- [ ] Atlas split ยังไม่ชัดเป็น `atlas_en` และ `atlas_th` พร้อม load-on-demand
- [ ] ยังไม่มี fixed vocabulary static layout map
- [ ] ยังไม่มี 4-Level Vertical Stacking
  - ต้องมี Level 0/1/2/3 สำหรับ fixed Thai vocabulary
- [ ] ยังไม่มี ascender collision lookup สำหรับ ป/ฝ/ฟ/ฬ กับสระบน/วรรณยุกต์
- [ ] ต้อง document ว่า Phase 1 ไม่ claim full Thai shaping/bidi/complex line breaking
- [ ] เพิ่ม visual/unit tests สำหรับคำไทยที่ใช้จริงในเกม

### 12. App / Platform Glue

- [x] มี `mm_app_ios.mm`, `mm_app_android.cpp`, `mm_app_mac.mm`
- [x] ไม่ใช้ sokol_app
- [ ] `new/delete` ใน app boundary ควรจำกัดเฉพาะ init/shutdown และ document
  - พบใน `app/mm_app_mac.mm`, `app/mm_app_ios.mm`, `app/mm_app_android.cpp`
- [ ] เพิ่ม lifecycle/safe area/haptic/dynamic resolution/thermal hooks ให้ครบ Phase 1
- [ ] macOS touch identity ใช้ pointer bitmask ซึ่ง compiler เตือน deprecated pointer introspection
  - Acceptance: ใช้ stable mapping table สำหรับ touch identity แทน cast/mask pointer

### 13. Documentation Tags

- [ ] เพิ่ม design decision tags ให้ครบทุก non-trivial system header
  - Prompt ต้องมี:
    - `@cache_reason`
    - `@zero_virtual`
    - `@handle_strategy`
    - `@pool_bound`
    - `@fallback`
    - `@instrumentation`
  - ปัจจุบันมีครบเพียงบางไฟล์ เช่น `rhi/mm_rhi_concept.hpp`, `render/mm_renderer.hpp`, `core/mm_expected.hpp`, `core/mm_span2d.hpp`, `core/mm_cache_metrics.hpp`
- [ ] เพิ่ม policy ว่า comments เหล่านี้ต้องอยู่ใน PR checklist

## Priority Plan

### P0 - ต้องทำก่อนพัฒนา feature ต่อ

- [ ] แก้ `Expected`/`std::expected` error return ให้ compile ผ่าน
- [ ] แก้ `TweenHandle` construction
- [ ] แก้ macro redefinition backend
- [ ] เพิ่ม smoke build target บน macOS
- [ ] เพิ่ม minimal unit tests สำหรับ `ExpectedLike`, `Slotmap`, `BoardGrid`, `RenderGraph`

### P1 - ทำให้ตรง master prompt Phase 1

- [ ] แก้ Slotmap freelist/generation tests
- [ ] เอา `alloca` ออกจาก render graph hot path
- [ ] ผูก cache metrics เข้ากับ allocator/pool/hot lookup จริง
- [ ] ทำ CI targets ตาม prompt
- [ ] ปรับ Audio SFX ให้ preallocated
- [ ] ทำ Vulkan 1.1 fallback path หรือแยกเป็น Phase 1.5 อย่างชัดเจน
- [ ] ทำ Sprite flush/upload/draw path ให้ครบ

### P2 - เพิ่มความครบเชิง product/mobile

- [ ] Thai static vocabulary + 4-level stacking + visual tests
- [ ] Texture compression detection ASTC/ETC2
- [ ] Safe area/lifecycle/haptic/thermal/dynamic resolution hooks
- [ ] Shader pipeline cache และ shader tool validation
- [ ] Tracy/profile markers ที่ subsystem boundaries

## Acceptance Criteria สำหรับบอกว่า "ผ่าน master_prompt_v4_6 Phase 1"

- [ ] Build ผ่านอย่างน้อย macOS/Metal และ Android/Vulkan config
- [ ] `ENGINE_USE_STD_EXPECTED=ON` และ `ENGINE_FORCE_LOCAL_EXPECTED=ON` ผ่านทั้งคู่
- [ ] ไม่มี `virtual`, `std::function`, `std::any` ใน engine hot path
- [ ] ไม่มี allocation ต่อ frame หลัง warmup จาก regression test
- [ ] `slotmap_lookup_in_hot_loop == 0` จาก regression test
- [ ] RenderGraph overflow ถูกจับได้ด้วย assert/metric
- [ ] BoardGrid/Tween/Particle/TextPopup มี unit tests
- [ ] Vulkan backend รองรับ Phase 1 fallback หรือ document ว่า Vulkan 1.3 path เป็น Phase 1.5 และไม่ block Phase 1
- [ ] Thai fixed vocabulary render ผ่าน snapshot/visual test
- [ ] เอกสาร design tags ครบทุก non-trivial system header


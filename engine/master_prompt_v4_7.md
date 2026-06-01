# Master Prompt: Mobile-First Game Engine v4.7

**Zero Virtual \+ Cache Optimized \+ VMA \+ vk-bootstrap \+ Casual 2D Ready**

Changelog v4.6 \-\> v4.7:

- เพิ่ม `Error` struct definition ที่ชัดเจน: trivially copyable, ไม่ allocate, ใช้ใน hot path ได้  
- แก้ Draw Call Sort Key: ระบุ bit-width ชัดเจนทุก field พร้อม `static_assert` ป้องกัน overlap  
- เพิ่ม Metal minimum version (Metal 3 \+ iOS 16 \+ A13) ใน Section 6 ให้ชัดเจนเทียบเท่า Vulkan  
- คืน struct layout skeleton ของ Casual 2D Systems (Section 8\) ที่ถูกตัดออกใน v4.6  
- คืน Effects Catalog Priority 1 (7 effects ที่ MUST มี) พร้อม trigger / implementation / data  
- ชี้แจง Handle resolve phase ใน Section 3 ให้ชัดเจน: resolve ก่อนเข้า hot loop เสมอ  
- เพิ่ม Thai codepoint reference table ใน Section 9  
- เพิ่ม pool overflow behavior contract (debug vs release)  
- เพิ่ม `@api_compat` design tag สำหรับ API version compatibility  
- เพิ่ม ambiguity handling rule ใน Section 17

---

## 0\. Requirement Levels

- **MUST**: ต้องทำ และต้อง compile/run ได้ใน Phase ปัจจุบัน  
- **SHOULD**: ควรทำเมื่อ toolchain/platform รองรับ หรือเมื่อมี benchmark ยืนยัน  
- **MAY**: optional/future phase ห้ามทำให้ Phase 1 ซับซ้อนขึ้นโดยไม่จำเป็น

AI ต้อง generate โค้ดแบบ incremental:

- ทุก step ต้อง compile independently  
- เพิ่ม minimal tests ตามความเสี่ยงของ module  
- ห้าม implement feature ของ later phase ก่อนเวลาถ้าไม่จำเป็น  
- ห้ามเพิ่ม abstraction ถ้ายังไม่มี duplication หรือ complexity จริง

---

## 1\. Hard Technical Rules

### Language

- **MUST**: ใช้ C++23 เป็น baseline  
- **MUST**: ใช้ `concepts` และ `consteval` เมื่อช่วย enforce compile-time contract ได้จริง  
- **SHOULD**: ใช้ `std::expected` และ `std::mdspan` เมื่อ standard library ของ target รองรับ  
- **MUST**: มี local fallback เช่น `Expected<T, E>` และ `Span2D/SpanND` สำหรับ mobile toolchain ที่ยังไม่รองรับครบ โดยการพัฒนา local fallback ต้องทำแบบ header-only ในช่วงเริ่มต้นของ Phase 1 และควบคุมด้วย macro toggle (เช่น `ENGINE_USE_STD_EXPECTED`)  
- **MAY**: ใช้ `std::execution` หลังจาก verify toolchain แล้วเท่านั้น  
- **MUST NOT**: ให้ Phase 1 job system พึ่ง `std::execution`

### Polymorphism

- **MUST NOT**: ใช้ `virtual`, `std::function`, `std::any` ใน hot path  
- **MUST**: ใช้ `concept` \+ `template` สำหรับ closed compile-time interfaces  
- **SHOULD**: ใช้ `std::variant` \+ `std::visit` เฉพาะ finite closed-set state ระดับสูง เช่น game state (Menu, Play, Pause)  
- **MUST NOT**: ใช้ `std::variant` \+ `std::visit` ใน render loop หรือ hot loop อื่นๆ เพื่อหลีกเลี่ยง table-dispatch overhead และ binary size bloat  
- **MUST**: command buffer หรือ dynamic command list ใน hot path ต้องใช้ `enum class` \+ trivially-copyable payload \+ `switch-case`  
- **MUST**: game state ใช้ closed-set type เช่น `std::variant<MenuState, PlayState, PauseState, WinState, FailState>`

### Backend Selection

- **MUST**: เลือก backend หลักแบบ compile-time ด้วย target macro/template  
- **MUST NOT**: มี runtime branch เพื่อเลือก Metal vs Vulkan ใน hot path  
- **MUST**: แยก backend selection ออกจาก runtime GPU feature detection  
- **SHOULD**: หลีกเลี่ยงการทำ Class Template บน High-Level class (เช่น `template<typename Backend> class Renderer`) เพื่อลด compile-time overhead และ header pollution; ให้ใช้ Conditional Type Aliasing แทน แล้วทดสอบด้วย static assert  
- **MUST**: `Renderer` เป็น non-template และถือ `ActiveBackend` โดยตรง; การเลือก backend และ `static_assert(RHI_Backend<ActiveBackend>)` ต้องอยู่ในไฟล์เดียวกัน (หรือ module เดียวกัน) เพื่อจำกัด header pollution และ compile-time explosion จาก backend headers  
- **SHOULD**: `Renderer` public header หลีกเลี่ยงการ include native backend headers หนัก เช่น Vulkan/Metal headers; ให้ย้าย backend-heavy implementation ไป `.cpp/.mm` หรือ private implementation unit เมื่อทำได้

\#if defined(TARGET\_IOS) || defined(TARGET\_MACOS)

  \#define USE\_METAL\_BACKEND

  using ActiveBackend \= MetalBackend;

\#elif defined(TARGET\_ANDROID)

  \#define USE\_VULKAN\_BACKEND

  using ActiveBackend \= VulkanBackend;

\#endif

// Verified at compile time against RHI concept

static\_assert(RHI\_Backend\<ActiveBackend\>, "ActiveBackend must conform to RHI\_Backend concept");

class Renderer {

  ActiveBackend backend; // Direct instantiation

};

### Data Layout & Memory

- **MUST**: hot data ใช้ SoA หรือ AoSoA  
- **MUST NOT**: `new/delete/malloc/free` ใน game loop  
- **MUST**: ใช้ Pool \+ Frame Arena \+ Ring Buffer สำหรับ allocation ที่ predictable  
- **MUST NOT**: เก็บ raw pointer ข้ามระบบหรือ expose raw pointer ใน public API  
- **MUST NOT**: public engine API คืน pointer/reference ไปยัง internal storage ที่ทำให้ lifetime เสี่ยง; ให้ใช้ handle หรือ value-return แทน  
- **SHOULD**: อนุญาต temporary views/spans เช่น `std::span` ได้เฉพาะเมื่อ lifetime ชัดเจนและไม่สามารถ outlive owner/frame/system; public API ต้อง document view lifetime  
- **SHOULD**: backend-private implementation สามารถถือ native pointer/handle ได้ ถ้ามี RAII/lifetime ownership ชัดเจน

### Handle Strategy

- **MUST**: object lifetime ใช้ `Handle = { uint32_t id, uint16_t gen }`  
- **MUST**: cold object lookup ใช้ slotmap/chunked slotmap  
- **MUST**: hot path data เช่น Particle, Tween, Transform ใช้ direct index \+ generation/check data แยก cold array  
- **MUST**: handle resolution จาก slotmap เป็น dense index ต้องเกิดใน phase แยกก่อนเข้า hot loop; hot loop รับ `std::span<uint32_t>` (หรือ equivalent) ของ dense indices เท่านั้น  
- **MUST NOT**: lookup slotmap ซ้ำใน inner loop ถ้าสามารถ resolve เป็น direct index ก่อนเข้า hot loop ได้  
- **MUST**: มี instrumentation สำหรับนับ `slotmap_lookup_in_hot_loop` และต้องถูก validate ใน CI ให้เป็น 0 ตาม threshold ในส่วน Cache Metrics

---

## 2\. Architecture Layers

\[App Layer\] mm\_app\_ios.mm / mm\_app\_android.cpp / mm\_app\_mac.mm

  \- Window, input, touch, lifecycle, safe area, AAssetManager/NSBundle

  \- Custom native platform glue (NOT sokol\_app unless explicitly noted)

  \- No virtual in hot path

  \- Function pointer table allowed only during init/boundary glue

\[Platform Glue\]

  \- Event pump, VFS, fixed-size thread pool, time, assert, log, haptic, save/load

  \- Thread-local arena for temporary work

\[Game Layer\]

  \- BoardSystem, GameStateMachine, TweenSystem, AudioSystem

  \- Pure logic, no rendering dependency

  \- Closed-set state via variant or tagged union

\[Renderer\]

  \- SpriteBatch SoA, ParticleSystem, TextRenderer, LayeredDraw

  \- Uses ActiveBackend directly (resolved at compile time via type alias)

\[MetalBackend / VulkanBackend\]

  \- Flat structs, no inheritance, no virtual

  \- Native API handles are backend-private

\[RHI\]

  \- BufferHandle, TextureHandle, PipelineHandle \= compact IDs

  \- Cold resource lifetime via slotmap

  \- Hot draw packet uses direct index/resolved references

---

## 3\. Core Systems

| System | Rule | Reason |
| :---- | :---- | :---- |
| Memory | `rpmalloc` optional \+ fixed pools `<32B,48B,96B,256B,4KB>` \+ FrameArena \+ RingBuffer | Covers Particle, Tween, TextPopup, transient upload |
| Handle | Slotmap cold \+ direct index hot | Prevents dangling handles while avoiding pointer chasing in hot loops |
| Mesh/Sprite Data | SoA: transforms, bounds, resource IDs, sort keys | Linear iteration, prefetch-friendly |
| Scene | Phase 1 flat array \+ `uint64_t` sort key | One sort per frame, stable draw order |
| Render Graph | Linear command buffer with enum \+ payload | Predictable branch pattern |
| Job System | Phase 1 fixed-size thread pool \+ bounded task queue | Debuggable, deterministic enough, low complexity |
| Job System Phase 2 | MAY use work stealing/fibers only if profiling shows idle CPU \>25% | Avoid premature scheduler complexity |
| Assets | VFS \+ async load \+ upload ring buffer \+ per-level preload | Avoid load-all-at-start |
| Math | SIMD wrapper \+ aligned allocations; `xsimd` SHOULD be used where supported, else fallback to ARM NEON intrinsics on mobile | Keeps SIMD optional per target |
| Cache Metrics | Instrumented counters \+ optional hardware counters | CI must remain practical across hosts |

---

## 4\. RHI Concept

### Error Type

`Error` คือ canonical error type ของ engine ต้องเป็น trivially copyable และ MUST NOT allocate เพื่อให้ใช้ใน hot path ได้ปลอดภัย:

// engine/core/error.hpp

// @cache\_reason  trivially copyable — ส่งผ่าน register ได้, ไม่ allocate ใน hot path

// @zero\_virtual  ไม่มี virtual method หรือ inheritance

struct Error {

    uint32\_t    code;    // error code; 0 \= success (unused in error path)

    const char\* msg;     // static string literal หรือ nullptr; MUST NOT point to heap

    // Convenience factories

    static constexpr Error ok()                          { return {0, nullptr}; }

    static constexpr Error make(uint32\_t c, const char\* m \= nullptr) { return {c, m}; }

    explicit operator bool() const { return code \!= 0; }

};

static\_assert(std::is\_trivially\_copyable\_v\<Error\>,

              "Error must be trivially copyable for safe use in hot path");

// Common error codes — extend per-subsystem with distinct ranges

namespace ErrorCode {

    inline constexpr uint32\_t None         \= 0;

    inline constexpr uint32\_t OutOfMemory  \= 1;

    inline constexpr uint32\_t InvalidHandle= 2;

    inline constexpr uint32\_t DeviceLost   \= 3;

    inline constexpr uint32\_t NotSupported \= 4;

    // 100–199: RHI; 200–299: Asset; 300–399: Audio; …

}

**Rules:**

- **MUST NOT**: `msg` ชี้ไปยัง heap-allocated string — ใช้ string literal หรือ static buffer เท่านั้น  
- **MUST NOT**: ใส่ `std::string`, `std::vector`, หรือ owning type ใดๆ ใน `Error`  
- **MUST**: `Error` ต้องผ่าน `static_assert(std::is_trivially_copyable_v<Error>)`

---

### LightweightErrorView / ExpectedLike Concepts

// LightweightErrorView:

// \- non-owning view of an error, used to avoid copying large error payloads

// \- MUST be trivially copyable and MUST NOT allocate

// \- MUST be convertible to the engine's canonical Error (or provide \`.code()\` that maps to Error)

template\<typename V, typename E\>

concept LightweightErrorView \=

  std::is\_trivially\_copyable\_v\<std::remove\_cvref\_t\<V\>\> &&

  (std::same\_as\<std::remove\_cvref\_t\<E\>, Error\>) &&

  requires(const std::remove\_cvref\_t\<V\>& v) {

    // Either explicit conversion:

    { static\_cast\<Error\>(v) } \-\> std::same\_as\<Error\>;

  };

template\<typename ER, typename E\>

concept ErrorReturnLike \=

  std::same\_as\<std::remove\_cvref\_t\<ER\>, E\> ||

  std::same\_as\<std::remove\_cvref\_t\<ER\>, const E\> ||

  std::same\_as\<std::remove\_cvref\_t\<ER\>, const E&\> ||

  LightweightErrorView\<ER, E\>;

// ExpectedLike\<R, T, E\>:

// \- R is the return type (e.g., std::expected\<T,E\> or fallback Expected\<T,E\>)

// \- MUST NOT require R to be default-constructible

template\<typename R, typename T, typename E\>

concept ExpectedLike \=

  requires(const R& cr) {

    { cr.has\_value() } \-\> std::same\_as\<bool\>;

    requires ErrorReturnLike\<decltype(cr.error()), E\>;

  } &&

  (std::is\_void\_v\<T\> || requires(R& r) {

    { \*r } \-\> std::same\_as\<T&\>;

  });

template\<typename T\>

concept RHI\_Backend \= requires(T t) {

  requires ExpectedLike\<decltype(t.create\_buffer(BufferDesc{})), BufferHandle, Error\>;

  { t.destroy\_buffer(BufferHandle{}) } \-\> std::same\_as\<void\>;

  requires ExpectedLike\<decltype(t.update\_buffer(BufferHandle{}, nullptr, 0, 0)), void, Error\>;

  requires ExpectedLike\<decltype(t.begin\_frame()), void, Error\>;

  requires ExpectedLike\<decltype(t.end\_frame()), void, Error\>;

  requires std::is\_nothrow\_destructible\_v\<T\>;

};

`ExpectedLike<R, T, E>` ต้องรองรับได้ทั้ง `std::expected<T, E>` และ fallback `Expected<T, E>` โดยอย่างน้อยต้องมี:

- `bool has_value() const`  
- `T& operator*()` หรือ `const T& operator*() const` สำหรับ non-void  
- `error()` ที่คืน `E`, `const E&`, หรือ lightweight error view ได้ โดยต้องไม่ allocate ใน hot path

และสำหรับ `ExpectedLike<R, void, E>` ต้องมี:

- `bool has_value() const`  
- `error()` ที่คืน `E`, `const E&`, หรือ lightweight error view ได้ โดยต้องไม่ allocate ใน hot path

Backend object ไม่จำเป็นต้อง trivially destructible เพราะ backend จริงอาจถือ RAII/native resources; แต่ destructor ต้องเป็น `noexcept` และ ownership ต้องชัดเจน

### Draw Call Sort Key

Bit layout ต้อง explicit และมี `static_assert` ป้องกัน overlap:

// engine/render/sort\_key.hpp

//

// uint64\_t sort key layout (total 64 bits):

//

//  \[63..56\]  layer\_id    : 8 bits  → max 256 layers

//  \[55..48\]  pipeline\_id : 8 bits  → max 256 pipelines

//  \[47..32\]  material\_id : 16 bits → max 65536 materials

//  \[31.. 0\]  depth       : 32 bits → normalized depth or z-order

//

// ห้ามแก้ shift โดยไม่อัปเดต static\_assert ด้านล่างด้วย

inline constexpr int SORT\_LAYER\_SHIFT    \= 56;

inline constexpr int SORT\_PIPELINE\_SHIFT \= 48;

inline constexpr int SORT\_MATERIAL\_SHIFT \= 32;

inline constexpr int SORT\_DEPTH\_SHIFT    \= 0;

inline constexpr uint64\_t SORT\_LAYER\_BITS    \= 8;

inline constexpr uint64\_t SORT\_PIPELINE\_BITS \= 8;

inline constexpr uint64\_t SORT\_MATERIAL\_BITS \= 16;

inline constexpr uint64\_t SORT\_DEPTH\_BITS    \= 32;

// Verify no overlap and full 64-bit coverage

static\_assert(SORT\_LAYER\_BITS \+ SORT\_PIPELINE\_BITS \+ SORT\_MATERIAL\_BITS \+ SORT\_DEPTH\_BITS \== 64,

              "sort key fields must sum to 64 bits");

static\_assert(SORT\_LAYER\_SHIFT    \== SORT\_PIPELINE\_SHIFT \+ SORT\_PIPELINE\_BITS);

static\_assert(SORT\_PIPELINE\_SHIFT \== SORT\_MATERIAL\_SHIFT \+ SORT\_MATERIAL\_BITS);

static\_assert(SORT\_MATERIAL\_SHIFT \== SORT\_DEPTH\_SHIFT    \+ SORT\_DEPTH\_BITS);

\[\[nodiscard\]\] constexpr uint64\_t make\_sort\_key(

    uint8\_t  layer\_id,

    uint8\_t  pipeline\_id,

    uint16\_t material\_id,

    uint32\_t depth) noexcept

{

    return (static\_cast\<uint64\_t\>(layer\_id)    \<\< SORT\_LAYER\_SHIFT)

         | (static\_cast\<uint64\_t\>(pipeline\_id) \<\< SORT\_PIPELINE\_SHIFT)

         | (static\_cast\<uint64\_t\>(material\_id) \<\< SORT\_MATERIAL\_SHIFT)

         | (static\_cast\<uint64\_t\>(depth)        \<\< SORT\_DEPTH\_SHIFT);

}

Layer IDs:

BACKGROUND(0) \-\> GRID(1) \-\> PIECES(2) \-\> EFFECTS(3) \-\> UI(4) \-\> OVERLAY(5)

---

## 5\. Cache Rules

- **MUST**: hot/cold split ข้อมูลที่อ่านทุกเฟรมออกจาก metadata/debug/string/lifetime data  
- **MUST**: batch update APIs รับ `start_index, count` เพื่อเปิดทาง SIMD/prefetch  
- **MUST**: no string in hot path; ใช้ `StringID = uint32_t hash`  
- **MUST**: UBO/constant buffer layout ต้องอยู่ใน limit ของ backend; ถ้าเกินใช้ SSBO/storage buffer หรือ texture buffer  
- **MUST**: pool size ต้องอิงจาก object จริงและมี assert เมื่อ overflow โดย behavior ต้องชัดเจน:  
  - **debug build**: `ENGINE_ASSERT(false, "pool overflow: <PoolName>")` → crash ทันที เพื่อให้ detect ได้เร็ว  
  - **release build**: return `Handle::invalid()` → caller ต้องตรวจ `handle.is_valid()` ก่อนใช้; silent drop ถ้า caller ไม่ตรวจ (documented behavior)  
  - **MUST**: document ใน header ว่า function นั้น can return invalid handle เมื่อ pool เต็ม  
- **SHOULD**: align allocation base pointer สำหรับ SoA arrays เป็น 16/32/64 bytes ตาม SIMD/cache need  
- **SHOULD**: ใช้ `alignas(64)` เฉพาะ shared counters, per-thread queues, allocator metadata, หรือ structs ที่เสี่ยง false sharing  
- **MUST NOT**: `alignas(64)` ทุก element ใน hot array โดยไม่มี benchmark เพราะอาจทำให้ memory bloat และ cache density แย่ลง

---

## 6\. Backend Requirements & Fallback

| Backend | Core Lib | MUST | MUST NOT |
| :---- | :---- | :---- | :---- |
| Metal | `metal-cpp`, minimum **Metal 3 \+ iOS 16 (A13 Bionic / iPhone 11+)** | command buffer, heap/resource reuse, pipeline cache/binary archive where available | Obj-C message send in frame hot loop, NSObject subclass in engine core |
| Vulkan | Vulkan 1.1 minimum (Phase 1), Vulkan 1.3 preferred for future phases, VMA, vk-bootstrap | VMA allocator, vk-bootstrap setup, pipeline cache, explicit sync | validation layers in release, manual per-frame allocation, direct `vkAllocateMemory` |

### Metal Rules

- **MUST**: minimum deployment target \= iOS 16 / macOS Ventura 13 (Metal 3\)  
- **MUST**: ตรวจ Metal family ตอน init: `[device supportsFamily:MTLGPUFamilyApple6]` (A13) ขึ้นไปเป็น baseline  
- **SHOULD**: enable Metal 4 features (requires A14+ / iOS 26\) ผ่าน runtime check เท่านั้น:  
    
  struct MetalFeatures {  
    
      bool metal4;          // A14+ / iOS 26+  
    
      bool mesh\_shader;     // Metal 3+, Apple7+  
    
      bool ray\_tracing;     // Apple6+ (limited), Apple7+ (full)  
    
      bool astc;            // Apple2+ (always true on supported devices)  
    
  };  
    
  // ตรวจผ่าน \[device supportsFamily:MTLGPUFamilyApple7\] ฯลฯ ตอน init  
    
  // อย่า hardcode feature ตาม iOS version — ให้ query device โดยตรง  
    
- **MUST NOT**: ใช้ Metal 4-only API โดยไม่มี runtime guard — ทำให้ app crash บน iPhone 11 (A13)  
- **SHOULD**: MTLBinaryArchive สำหรับ pipeline cache บน Metal 3 ขึ้นไป  
- **SHOULD**: MTLHeap สำหรับ texture/buffer sub-allocation บน Metal 3 ขึ้นไป

### Vulkan Rules

- **MUST**: ใช้ `vk-bootstrap` สำหรับ instance/device/swapchain setup แทน hand-written setup boilerplate; backend-private code ยังสามารถใช้ raw `Vk*` handles ได้เมื่อ ownership/lifetime ชัดเจน  
- **MUST**: ใช้ VMA สำหรับ buffer/image memory  
- **MUST NOT**: เขียน `vkAllocateMemory` เอง ยกเว้น test/fallback ที่มีเหตุผลและ tag อธิบายชัด  
- **MUST NOT**: ใช้ `vkDeviceWaitIdle` ใน frame loop  
- **MUST**: Phase 1 รองรับอย่างน้อย Vulkan 1.1 พร้อม explicit render pass/framebuffer path และ sync แบบ binary semaphore/fence ที่ไม่มี timeline/dynamic rendering  
- **SHOULD**: ใช้ timeline semaphore และ dynamic rendering เมื่อ runtime device รองรับ และแยกออกเป็น path เพิ่มใน Phase 1.5/Phase 2 โดยไม่ทำให้ Phase 1 ซับซ้อนเกินจำเป็น  
- **MUST**: ตรวจ format/compression support ตอน runtime เช่น ASTC/ETC2

Runtime feature detection example:

struct VulkanFeatures {

  bool dynamic\_rendering;

  bool timeline\_semaphore;

  bool astc;

  bool etc2;

};

// Backend selection is compile-time.

// GPU feature fallback is runtime and happens outside hot draw loops.

### Shader Pipeline

- **MUST**: single source shader path ชัดเจน เช่น HLSL \-\> SPIR-V \-\> SPIRV-Cross \-\> MSL  
- **MUST**: cache compiled bytecode/pipeline artifacts  
- **SHOULD**: hot reload เฉพาะ debug/profile builds

---

## 7\. Third Party Libraries

Allowed:

stb\_image

stb\_truetype

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

Rules:

- **MUST**: public engine API wrap third-party resources with handles or owned RAII wrappers  
- **MUST NOT**: leak third-party raw pointers through game-facing APIs  
- **SHOULD**: backend-private code may use native handles directly when ownership and lifetime are local and documented

---

## 8\. Casual 2D Game Systems

Every system MUST include design tags (Section 16\) in its header. Every effect/pool-spawned object MUST declare: `spawn_pool`, `max_lifetime`, `max_concurrent_instances`, and a profiling marker point.

### Handle Resolve Rule (clarification)

ใน Section 3 "Handle: Slotmap cold \+ direct index hot" หมายความว่า:

- **resolve phase** (ก่อนเข้า hot loop): `slotmap.resolve(handle) → uint32_t dense_index`  
- **hot loop** รับ `std::span<uint32_t> dense_indices` เท่านั้น — ห้าม lookup slotmap ใน inner loop  
- pattern: `for (uint32_t i : dense_indices) { process(soa_array[i]); }`

### 8.1 Board / Grid System

// engine/game/board\_grid.hpp

// @cache\_reason  flat 1D SoA — neighbor lookup \= index arithmetic, ไม่ pointer chase

// @zero\_virtual  enum dispatch สำหรับ cell state transitions

// @handle\_strategy  cell ใช้ direct 2D→1D index: idx \= row\*cols \+ col (O(1))

// @pool\_bound  BoardGrid เดียว per game scene; cells อยู่ใน FrameArena

// Hot arrays (อ่านทุกเฟรมใน match scan / gravity)

struct BoardGrid {

    uint8\_t\* cell\_type;   // \[cols\*rows\] tile color/type (0=empty)

    uint8\_t\* cell\_state;  // \[cols\*rows\] EMPTY|IDLE|FALLING|MATCHED|CLEARING

    uint8\_t\* dirty;       // \[cols\*rows\] re-render flag

    int8\_t\*  fall\_dist;   // \[cols\*rows\] gravity target distance in cells

    uint8\_t  cols, rows;

    uint16\_t capacity;    // cols\*rows, pre-computed

    // Cold metadata — อ่านน้อย

    uint32\_t move\_count;

    uint32\_t score;

};

// Cell state enum — switch-case dispatch, ไม่มี virtual

enum class CellState : uint8\_t { Empty=0, Idle, Falling, Matched, Clearing };

// Match scan ใช้ row/col bitmask สำหรับ fast flood-fill

// gravity: RingBuffer\<uint8\_t\> column indices ที่ต้องตก — ไม่ alloc

### 8.2 Particle Pool

// engine/game/particle\_pool.hpp

// @cache\_reason  SoA — update loop วิ่ง linear, upload เป็น instance buffer เดียว

// @zero\_virtual  update \= SIMD batch; spawn/despawn \= swap-with-last

// @handle\_strategy  direct index; handle ไม่จำเป็น — particle เป็น fire-and-forget

// @pool\_bound  ParticlePool, lifetime \<= 2.0s, max instances \<= 4096

// @fallback  scalar update เมื่อ SIMD ไม่พร้อม

struct ParticlePool {

    // Hot SoA — อ่านทุก frame update \+ GPU upload

    float    px\[MAX\_PARTICLES\], py\[MAX\_PARTICLES\];   // position

    float    vx\[MAX\_PARTICLES\], vy\[MAX\_PARTICLES\];   // velocity

    float    ax\[MAX\_PARTICLES\], ay\[MAX\_PARTICLES\];   // acceleration (gravity)

    float    life\[MAX\_PARTICLES\];                    // remaining lifetime (s)

    float    life\_max\[MAX\_PARTICLES\];                // initial lifetime (for alpha lerp)

    float    scale\[MAX\_PARTICLES\];

    uint32\_t color\[MAX\_PARTICLES\];                   // RGBA packed

    uint8\_t  atlas\_id\[MAX\_PARTICLES\];               // sprite index in atlas

    uint8\_t  active\[MAX\_PARTICLES\];                  // slot occupied flag

    uint32\_t count;  // active count (slots may be non-contiguous; use swap-with-last)

};

// Emitter types (no virtual — caller fills EmitterParams)

struct EmitterParams {

    enum class Shape : uint8\_t { Burst, Cone, Rain, Orbit } shape;

    float  origin\_x, origin\_y;

    float  angle\_min, angle\_max;  // cone: degrees; burst: full 360

    float  speed\_min, speed\_max;

    float  life\_min,  life\_max;

    uint8\_t count;

    uint8\_t atlas\_id;

    uint32\_t color;

};

### 8.3 Tween Pool

// engine/game/tween\_pool.hpp

// @cache\_reason  SoA — update ทุก slot linear ใน single pass

// @zero\_virtual  EaseFunc \= plain function pointer, ไม่ใช้ std::function

// @handle\_strategy  TweenHandle \= {uint32\_t id, uint16\_t gen} สำหรับ cancel/chain

// @pool\_bound  TweenPool, lifetime \<= duration field, max instances \<= 512

using EaseFunc \= float(\*)(float t);  // t ∈ \[0,1\] → output ∈ \[0,1\] (approx)

enum class EaseType : uint8\_t {

    Linear, QuadIn, QuadOut, QuadInOut,

    CubicOut, ElasticOut, BounceOut, SineInOut, BackOut

};

struct TweenPool {

    // Hot SoA

    float\*   target\[MAX\_TWEENS\];       // pointer to float being animated

    float    start\[MAX\_TWEENS\];

    float    end\[MAX\_TWEENS\];

    float    t\[MAX\_TWEENS\];            // elapsed / duration ∈ \[0,1\]

    float    duration\[MAX\_TWEENS\];

    uint8\_t  ease\[MAX\_TWEENS\];         // EaseType index → EaseFunc table lookup

    uint8\_t  loop\[MAX\_TWEENS\];         // 0=one-shot, 1=loop, 2=ping-pong

    uint8\_t  active\[MAX\_TWEENS\];

    // Cold: chain support

    TweenHandle next\[MAX\_TWEENS\];      // invalid \= no chain

    uint32\_t count;

};

// API — ไม่ alloc:

// TweenHandle tween\_to(float\* target, float end, float duration, EaseType ease);

// void        tween\_sequence(TweenHandle\* handles, uint8\_t count); // links chain

// void        tween\_parallel(TweenHandle\* handles, uint8\_t count); // fires all at once

// void        tween\_cancel(TweenHandle h);

### 8.4 Text Popup Pool

// engine/game/text\_popup\_pool.hpp

// @cache\_reason  SoA — float/fade update vectorizable; glyph quads batched ใน SpriteBatch

// @pool\_bound  TextPopupPool, lifetime \<= 1.2s, max instances \<= 32

struct TextPopupPool {

    // Hot SoA

    float    px\[MAX\_POPUP\], py\[MAX\_POPUP\];

    float    vy\[MAX\_POPUP\];           // float-up velocity (px/s, negative \= up)

    float    alpha\[MAX\_POPUP\];

    float    scale\[MAX\_POPUP\];        // pop-in animation scale

    uint32\_t color\[MAX\_POPUP\];        // RGBA tint

    uint8\_t  active\[MAX\_POPUP\];

    // Cold: text content (short strings only)

    char     text\_buf\[MAX\_POPUP\]\[16\]; // max 15 chars \+ null

    uint8\_t  text\_len\[MAX\_POPUP\];

};

### 8.5 Camera Trauma (Screen Shake)

// engine/game/camera\_trauma.hpp

// @pool\_bound  single CameraTrauma per scene; no pool needed

// @fallback  no haptic if platform not supported (iOS CoreHaptics / Android VibrationEffect)

struct CameraTrauma {

    float trauma;       // 0..1, additive; decays: trauma \*= decay\_rate per frame

    float offset\_x, offset\_y;  // computed: max\_offset \* trauma^2 \* sin(time \* freq)

    float angle;        // optional rotation shake (radians)

    float time\_accum;   // accumulated time for sin oscillation

};

// API:

// void camera\_add\_trauma(CameraTrauma& c, float amount);  // clamp to \[0,1\]

// void camera\_update(CameraTrauma& c, float dt);

// void camera\_get\_offset(const CameraTrauma& c, float& ox, float& oy);

//

// Recommended values: max\_offset=8px, decay\_rate=0.88, shake\_freq=50Hz

// Haptic: trigger when trauma \> 0.3 threshold (avoid spamming)

### 8.6 Touch Gesture

// engine/input/touch\_gesture.hpp

// @zero\_virtual  FSM via switch-case, ไม่มี virtual/callback heap allocation

enum class GestureType : uint8\_t { None, Tap, SwipeUp, SwipeDown, SwipeLeft, SwipeRight, LongPress, Drag };

struct FingerState {

    enum class Phase : uint8\_t { Idle, Pressing, Resolved } phase;

    GestureType resolved;

    float start\_x, start\_y;

    float curr\_x,  curr\_y;

    float duration;          // seconds held

};

struct TouchTracker {

    FingerState fingers\[5\];  // max 5 simultaneous touches

    uint8\_t active\_count;

    // Thresholds (configurable at init)

    float swipe\_min\_dist;    // default 20px

    float tap\_max\_time;      // default 0.25s

    float long\_press\_time;   // default 0.5s

};

### 8.7 Audio System

// engine/audio/audio\_system.hpp

// @zero\_virtual  priority queue \= sorted fixed array, ไม่ใช้ virtual

// @pool\_bound  SfxPool max 16 simultaneous voices; MusicLayer 2 tracks

struct SfxPool {

    ma\_sound voices\[MAX\_SFX\_VOICES\];  // pre-allocated miniaudio sounds

    uint8\_t  priority\[MAX\_SFX\_VOICES\];

    uint8\_t  active\[MAX\_SFX\_VOICES\];

    uint8\_t  voice\_count;

    // เมื่อ pool เต็ม: แทนที่ voice ที่มี priority ต่ำสุด (preemptive)

};

struct MusicLayer {

    ma\_sound track\[2\];

    float    volume\[2\];

    float    crossfade\_t;  // 0→1 interpolation

    uint8\_t  active\_track;

};

### 8.8 Save / Load

// engine/core/save\_data.hpp

// binary flat struct \+ CRC32; async write via thread pool; atomic rename

struct SaveData {

    uint32\_t magic;              // 0xCAFE2D00 — version guard

    uint8\_t  version;            // increment on breaking change

    uint8\_t  \_pad\[3\];

    uint32\_t level\_stars\[200\];   // 2 bits per level (max 200 levels)

    uint32\_t high\_scores\[200\];

    uint32\_t settings\_flags;     // bit 0=sound, 1=haptic, 2=music, …

    uint32\_t checksum;           // CRC32 of all preceding bytes

};

static\_assert(sizeof(SaveData) \< 4096, "SaveData should fit in one 4KB pool block");

// Async write: enqueue to thread pool; atomic: write temp → fsync → rename

// Platform path: iOS=NSSearchPathForDirectoriesInDomains; Android=getFilesDir()

### 8.9 Effects Catalog — Priority 1 (MUST implement)

These 7 effects are required for the game to feel complete. Each MUST be implemented using the pools above and MUST NOT allocate in the spawn call.

| Effect | Trigger | Implementation | Pool / Data |
| :---- | :---- | :---- | :---- |
| **Tile clear burst** | tile matched/cleared | `EmitterParams{Burst, 8–16 particles, lifetime 0.4s}`, color \= tile type | `ParticlePool`; `@pool_bound lifetime<=0.4s, max=128` |
| **Block fall \+ squash** | block lands | `tween_to(&scale_y, 0.7, 0.08, BounceOut)` → `tween_to(&scale_y, 1.0, 0.12, ElasticOut)` | `TweenPool`; `@pool_bound lifetime<=0.25s, max=64` |
| **Score popup float** | score gained | spawn `TextPopupPool`: vy=-80px/s, alpha 1→0 over 0.8s, scale 0.5→1.0 | `TextPopupPool`; `@pool_bound lifetime<=1.0s, max=32` |
| **Combo chain flash** | combo ≥ 2 | white overlay sprite on matched tiles, alpha tween 1→0 over 0.3s | overlay `SpriteBatch` \+ `TweenPool`; `@pool_bound lifetime<=0.3s` |
| **Invalid swap shake** | swap rejected | `tween_to(&pos_x, pos_x±4, 0.05, SineInOut)` × 3 cycles \+ haptic light | `TweenPool`; `@pool_bound lifetime<=0.3s, max=8 per frame` |
| **Level clear celebration** | level complete | confetti `EmitterParams{Rain}` from top (128 particles, gravity \+200px/s²) \+ star burst `{Burst}` from center \+ screen flash 0.1s | `ParticlePool`; `@pool_bound lifetime<=3.0s, max=256` |
| **Screen shake** | bomb / big combo | `camera_add_trauma(0.4–0.8)` | `CameraTrauma`; decay auto; no pool |

### 8.10 Effects Catalog — Priority 2 (SHOULD implement)

| Effect | Trigger | Implementation |
| :---- | :---- | :---- |
| Tile hover highlight | touch begin | outline sprite, scale pulse 1.0→1.05 loop tween |
| Power-up charge idle | special tile | slow rotation loop tween \+ orbit `EmitterParams{Orbit, 2–4 particles}` |
| Row clear sweep line | row complete (block puzzle) | tween X scan line sprite across row, then burst per tile |
| Ambient idle sparkle | no input 3s+ | low-rate emitter 2–4 particle/s on random board positions |
| Block slide smooth | gravity fill gap | tween pos Y, EaseCubicOut, speed ∝ fall distance |

### 8.11 Effects Catalog — Priority 3 (MAY implement in Phase 2\)

- Drag trail (ring buffer trail positions, ghost sprite alpha decay)  
- Tile color morph / UV anim frame for wildcard tile  
- Background parallax (gyro input → layer offset at different speeds)  
- Hint pulse (no move 10s → matched pair pulse \+ glow outline)

---

## 9\. Text Rendering & Thai Support

### Layer Strategy

- **Layer A**: Bitmap font for score/timer/digits  
- **Layer B**: SDF font for UI/popup/button text

### Thai / Unicode

- **MUST**: support fixed vocabulary Thai text in Phase 1  
- **MUST**: atlas split:  
  - `atlas_en`: ASCII \+ digits, always loaded  
  - `atlas_th`: U+0E00..U+0E7F, load-on-demand  
- **MUST**: รองรับ fixed vocabulary Thai text ด้วย static layout map เป็นเส้นทางหลัก รวมถึงข้อมูล 4-Level Vertical Stacking (Level 0: ล่าง, Level 1: ฐาน, Level 2: บนสระ, Level 3: บนวรรณยุกต์) สำหรับคำที่กำหนดไว้ล่วงหน้า  
- **SHOULD**: ใช้การคำนวณสะสม Y-offset เป็น heuristic fallback เมื่อไม่มี entry ใน layout map โดยจำกัดเฉพาะ subset ที่ทดสอบแล้ว  
- **SHOULD**: ลดการทับซ้อนด้วย X-offset เล็กน้อยทางซ้ายเมื่อตรวจพบคู่พยัญชนะฐานที่มีหางยาว (ป, ฝ, ฟ, ฬ) กับสระบน/วรรณยุกต์ (Ascender Collision Avoidance) เฉพาะชุดคู่ตัวอักษรที่ระบุในตาราง lookup ที่แนบมากับเกม (ไม่อ้างว่า general-purpose Thai shaping)  
- **MUST**: UTF-8 \-\> codepoint \-\> glyph lookup \-\> quad emit into SpriteBatch  
- **SHOULD**: use precomposed/static layout map for known game vocabulary  
- **MAY**: add HarfBuzz-lite/full HarfBuzz for dynamic text in Phase 2  
- **MUST NOT**: claim full Thai shaping, bidi, or complex line breaking in Phase 1

#### Thai Codepoint Reference Table

ใช้ตารางนี้เป็น baseline สำหรับ 4-level stacking และ ascender collision lookup:

── Stacking Levels ────────────────────────────────────────────────

Level 0  (below baseline):   สระล่าง

  U+0E38  ุ   U+0E39  ู   U+0E3A  ฺ

Level 1  (baseline):         พยัญชนะฐาน (consonants) ทั้งหมด

  U+0E01..U+0E2E  (ก–ฮ)

Level 2  (above consonant):  สระบน / นิคหิต

  U+0E31  ั   U+0E34  ิ   U+0E35  ี   U+0E36  ึ   U+0E37  ื

  U+0E47  ็   U+0E4D  ํ   U+0E48  ่   U+0E49  ้   U+0E4A  ๊

  U+0E4B  ๋   U+0E4C  ์   U+0E4E  ๎

Level 3  (above level 2):    วรรณยุกต์ที่ต้องขึ้นสูงกว่าสระบน

  (ใช้เมื่อ level 2 ถูกครอบครองโดยสระแล้ว)

  U+0E48  ่   U+0E49  ้   U+0E4A  ๊   U+0E4B  ๋

  หมายเหตุ: วรรณยุกต์เดียวกัน อาจอยู่ level 2 หรือ 3 ขึ้นอยู่กับว่ามีสระบนหรือไม่

── Ascender Collision Avoidance ───────────────────────────────────

พยัญชนะที่มี "หางยาวขึ้นบน" (ascender) ซึ่งอาจชนกับสระ/วรรณยุกต์ level 2–3:

  U+0E1B  ป   U+0E1D  ฝ   U+0E1F  ฟ   U+0E2C  ฬ

กฎ Ascender Collision: เมื่อ base consonant ∈ {ป,ฝ,ฟ,ฬ} และ

combining mark ∈ level 2 หรือ level 3 ให้เลื่อน mark ออกทางซ้าย

X-offset ≈ \-2px (หรือค่าจาก lookup table เฉพาะเกม)

── Zero-Width Combining (advance \= 0\) ─────────────────────────────

Codepoints เหล่านี้ไม่เลื่อน cursor — วาดทับ base consonant ก่อนหน้า:

  U+0E31  ั   U+0E34..U+0E37  ิ ี ึ ื

  U+0E38..U+0E3A  ุ ู ฺ

  U+0E47..U+0E4E  ็ ่ ้ ๊ ๋ ์ ํ ๎

---

## 10\. Phase 1 Feature Set

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

## 11\. Non-Goals Phase 1

Network, physics engine, scripting VM, editor, complex animation graph, full ECS, multiplayer, bidi text, full dynamic complex text shaping, complex particle physics, PBR/IBL/shadow renderer.

---

## 12\. Performance Targets & CI Validation

Target devices:

- iPhone 12 class  
- Snapdragon 865 class

Targets:

- CPU frame time \< 4ms for core 2D test scene  
- GPU frame time \< 12ms for core 2D test scene  
- Memory \< 150MB for sample game scene  
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

allocations\_per\_frame \== 0 after warmup

slotmap\_lookup\_in\_hot\_loop \== 0

ptr\_chase\_ratio \< 2% in instrumented hot systems

frame\_time\_regression \< 10% from checked-in baseline

Example CI shape:

steps:

  \- cmake \--preset ci \-DENGINE\_ENABLE\_ASSERT=ON \-DENGINE\_ENABLE\_TRACY=OFF

  \- ninja

  \- ninja backend\_concept\_tests

  \- ninja hot\_system\_microbench

  \- ninja allocation\_regression\_test

  \- ninja handle\_lookup\_regression\_test

Profile builds:

steps:

  \- cmake \--preset profile \-DENGINE\_ENABLE\_TRACY=ON

  \- ninja

  \- run\_profile\_capture\_optional

---

## 13\. Build & Config Matrix

option(ENGINE\_ENABLE\_TRACY "Enable Tracy profiler markers" ON)

option(ENGINE\_ENABLE\_ASSERT "Enable runtime assert/bounds checks" ON)

option(ENGINE\_HOT\_RELOAD\_SHADERS "Enable filesystem watch for SPIR-V/MSL" OFF)

option(ENGINE\_FORCE\_SIMD\_BACKEND "Force SIMD backend: auto/xsimd/neon/sse/scalar" "auto")

option(ENGINE\_USE\_STD\_EXPECTED "Use std::expected when available" ON)

option(ENGINE\_USE\_STD\_MDSPAN "Use std::mdspan when available" ON)

option(ENGINE\_ENABLE\_EXCEPTIONS "Enable C++ exceptions in non-core code" OFF)

option(ENGINE\_ENABLE\_RTTI "Enable RTTI in non-core code" OFF)

Presets:

- `debug`: asserts, validation layers, optional Tracy  
- `profile`: O2, Tracy, symbols, no validation layers unless requested  
- `release`: O3/LTO, strip, no Tracy, no validation layers  
- `ci`: deterministic tests, concept tests, allocation/cache regression tests

---

## 14\. Deliverable Structure

engine/

├── rhi/

│   ├── rhi\_concept.hpp

│   ├── metal\_backend.hpp

│   └── vulkan\_backend.hpp

├── core/

│   ├── expected.hpp

│   ├── span2d.hpp

│   ├── slotmap.hpp

│   ├── arena.hpp

│   ├── handle.hpp

│   ├── ring\_buffer.hpp

│   ├── cache\_metrics.hpp

│   └── save\_data.hpp

├── game/

│   ├── board\_grid.hpp

│   ├── game\_state.hpp

│   ├── tween\_pool.hpp

│   ├── particle\_pool.hpp

│   ├── text\_popup\_pool.hpp

│   └── camera\_trauma.hpp

├── render/

│   ├── render\_graph.hpp

│   ├── sort\_key.hpp

│   ├── sprite\_batch.hpp

│   └── text\_renderer.hpp

├── audio/

│   └── audio\_system.hpp

├── input/

│   └── touch\_gesture.hpp

├── app/

│   ├── mm\_app\_ios.mm

│   ├── mm\_app\_android.cpp

│   └── mm\_app\_mac.mm

├── tools/

│   └── shader\_hotloader.hpp

└── examples/

    ├── 01\_sprite\_10k.cpp

    ├── 02\_match3\_board.cpp

    ├── 03\_block\_puzzle.cpp

    └── 04\_particle\_stress.cpp

---

## 15\. Implementation Order

1. Build system \+ `expected.hpp` \+ `span2d.hpp` \+ `handle.hpp` compile (Develop fallback first in header-only format)  
2. `rhi_concept.hpp` \+ backend concept tests compile  
3. `slotmap.hpp` \+ `arena.hpp` \+ `ring_buffer.hpp` \+ allocation tests  
4. `cache_metrics.hpp` with instrumentation-only counters  
5. `MetalBackend` stub passes concept  
6. `VulkanBackend` setup with `vk-bootstrap` \+ VMA, no hand-written instance/device/swapchain boilerplate, no direct `vkAllocateMemory`  
7. Vulkan runtime feature detection \+ fallback flags  
8. `sort_key.hpp` \+ `render_graph.hpp`  
9. `sprite_batch.hpp` renders quads  
10. `board_grid.hpp` \+ `game_state.hpp`  
11. `tween_pool.hpp` \+ `particle_pool.hpp` \+ `text_popup_pool.hpp`  
12. `text_renderer.hpp`: bitmap first, then SDF, then fixed-vocabulary Thai combining support (4-level stacking & ascender collision check)  
13. `audio_system.hpp` \+ `touch_gesture.hpp` \+ `save_data.hpp`  
14. `mm_app_*.mm/.cpp` entry points  
15. examples \+ CI regression tests  
16. optional profile/Tracy capture

---

## 16\. Design Decision Tags

Every non-trivial system header MUST answer:

1. `@cache_reason`: ทำไม layout นี้ cache-friendly กว่า object graph/OOP แบบเดิม  
2. `@zero_virtual`: ทำไมไม่ใช้ virtual และ dispatch ด้วยอะไร  
3. `@handle_strategy`: ใช้ slotmap หรือ direct index เพราะอะไร  
4. `@pool_bound`: spawn จาก pool ใด, lifetime \<= X, max instances \<= Y  
5. `@fallback`: fallback low-end/mobile/toolchain คืออะไร  
6. `@instrumentation`: มี metric/test อะไรวัด regression  
7. `@api_compat`: minimum API version ที่ feature นี้ต้องการ (Vulkan 1.x / Metal N) และ fallback สำหรับ older version

Example:

/// @cache\_reason SoA arrays keep transform update linear and prefetch-friendly.

/// @zero\_virtual Closed command set uses enum dispatch instead of virtual calls.

/// @handle\_strategy Slotmap owns lifetime; hot loop receives resolved direct indices.

/// @pool\_bound ParticlePool, lifetime \<= 1.25s, max instances \<= 4096\.

/// @fallback Scalar update path when SIMD backend is unavailable.

/// @instrumentation Tracks allocations/frame and hot-loop slotmap lookups.

/// @api\_compat Vulkan 1.1+ (Phase 1 path); Dynamic Rendering requires Vulkan 1.3+.

///             Metal 3+ (iOS 16 / A13); MTLBinaryArchive requires Metal 3+.

Hard constraints for core engine code:

- **MUST**: ไม่ใช้ C++ exceptions และ engine core target ต้อง force `-fno-exceptions` (หรือเทียบเท่า) เสมอ ไม่ขึ้นกับ app-level option  
- **MUST**: ไม่ใช้ RTTI (`dynamic_cast`, `typeid`) ใน engine core และ engine core target ต้อง force `-fno-rtti` (หรือเทียบเท่า) เสมอ ไม่ขึ้นกับ app-level option  
- **MUST NOT**: ให้ public engine API คืน pointer/reference ไปยัง internal storage ที่ทำให้ lifetime เสี่ยง; ให้ใช้ handle, value-return, หรือ documented temporary view/span แทน

Tracy:

- **SHOULD**: markers at subsystem boundaries in profile builds  
- **MUST NOT**: make CI depend on Tracy server availability  
- **MUST**: compile out cleanly in release

---

## 17\. Output Requirements For Code Generation

When asked to generate code:

- Generate one implementation step at a time unless explicitly asked for all files  
- Include exact files changed/created  
- Include compile/test commands  
- Include known limitations  
- Keep code minimal but extensible  
- Do not add Phase 2 systems while implementing Phase 1  
- Prefer correctness and compilability over theoretical maximum performance  
- **Ambiguity handling**: If the spec is ambiguous or underspecified for a particular decision, state the assumption explicitly in a `// ASSUMPTION:` comment at the point of decision before proceeding. Do not silently pick an interpretation. Example:  
    
  // ASSUMPTION: pool overflow in release returns Handle::invalid() and caller  
    
  // is responsible for checking validity. Spec Section 5 documents this contract.


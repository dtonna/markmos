# markmos

Cross-platform game engine with Metal backend (macOS/iOS) and Vulkan backend (Android).

## Features

- **C++23** without exceptions or RTTI
- **Renderer abstraction** - Metal (Apple) / Vulkan (Android)
- **Audio** via miniaudio
- **Memory management** via rpmalloc
- **Job system** for multithreaded tasks
- **Sprite batching** with efficient sorting

## Build

### macOS/iOS

```bash
cd engine
./build-xcode.sh
# or
cmake -G Xcode -S . -B build-xcode
```

### Android

```bash
cd engine
cmake -S . -B build-android -DANDROID=ON
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `USE_TRACY` | `OFF` | Enable Tracy profiling |
| `USE_IMGUI` | `OFF` | Include debug ImGui |
| `BUILD_EXAMPLES` | `ON` | Build example programs |
| `BUILD_TESTS` | `ON` | Build test targets |

## Project Structure

```
engine/
├── core/          # Memory, VFS, job system, utilities
├── render/        # Renderer, shaders, sprite batch
├── rhi/           # Metal/Vulkan backends
├── app/           # Platform entry points
├── thirdparty/     # rpmalloc, miniaudio, simdjson, stb, metal-cpp, VMA
├── shaders/       # GLSL sources (Vulkan)
└── examples/      # Demo programs
```
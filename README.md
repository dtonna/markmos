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

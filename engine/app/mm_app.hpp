// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../input/mm_input_event.hpp"
#include "../input/mm_input_state.hpp"

// Engine globals — platform defines them, game code accesses via extern
#if defined(USE_METAL_BACKEND)
struct MetalBackend;
extern MetalBackend* g_backend;
#elif defined(USE_VULKAN_BACKEND)
class VulkanBackend;
extern VulkanBackend* g_backend;
#elif defined(MM_PLATFORM_MACOS) || defined(MM_PLATFORM_IPHONE)
// clangd fallback: Apple platforms always use Metal
struct MetalBackend;
extern MetalBackend* g_backend;
#else
// clangd fallback: assume Vulkan on other platforms
class VulkanBackend;
extern VulkanBackend* g_backend;
#endif
extern InputEventQueue* g_input_queue;
extern InputState* g_input_state;
extern float g_content_scale;

// App Callbacks — Sokol-style entry point
// Platform code creates window/input/backend, then calls these at appropriate lifecycle points
// User defines markmos_main() in their game code to wire up callbacks

struct AppCallbacks {
    void* user_data;

    // Called once when the platform is ready (backend, input, audio initialized)
    void (*init)(void* user_data);

    // Called every frame — update game logic + issue draw commands
    void (*frame)(void* user_data, float dt, InputState& input);

    // Called on resize/orientation change — update width/height
    void (*resize)(void* user_data, uint32_t w, uint32_t h);

    // Called once on shutdown — destroy game state
    void (*cleanup)(void* user_data);
};

// Request a clean app exit (platform-specific; may be a no-op on mobile)
extern void app_quit() noexcept;

// User must define this — returns AppCallbacks for the platform to invoke
extern "C" AppCallbacks markmos_main(int argc, char* argv[]);

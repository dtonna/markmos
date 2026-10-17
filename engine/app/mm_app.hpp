// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../game/mm_event_bus.hpp"
#include "core/mm_types.h"
#include "../input/mm_input_event.hpp"
#include "../input/mm_input_state.hpp"
#include "../rhi/mm_rhi_concept.hpp"

// Engine globals — platform defines them, game code accesses via extern
#if defined(USE_METAL_BACKEND)
struct MetalBackend;
extern MetalBackend *g_backend;
#elif defined(USE_VULKAN_BACKEND)
class VulkanBackend;
extern VulkanBackend *g_backend;
#elif defined(MM_PLATFORM_MACOS) || defined(MM_PLATFORM_IPHONE)
// clangd fallback: Apple platforms always use Metal
struct MetalBackend;
extern MetalBackend *g_backend;
#else
// clangd fallback: assume Vulkan on other platforms
class VulkanBackend;
extern VulkanBackend *g_backend;
#endif
extern InputEventQueue *g_input_queue;
extern InputState      *g_input_state;
extern f32            g_content_scale;

// Safe-area inset for the TOP edge, in points.
//
// Android phones put the camera in the display itself — a notch or a punch hole
// — and that area is still screen. A fullscreen activity draws into it, so any UI
// anchored to y=0 (a top bar, a row of buttons) lands underneath the lens and is
// unreadable there. Hosts report how deep the obstruction reaches; everything else
// is 0, because a desktop window has no such thing.
extern f32            g_ui_inset_top;
extern f32            g_ui_inset_bottom;
extern EventBus        *g_event_bus_ptr;

// App Callbacks — Sokol-style entry point
// Platform code creates window/input/backend, then calls these at appropriate lifecycle points
// User defines markmos_main() in their game code to wire up callbacks

struct AppCallbacks {
    void *user_data;

    // Window title (macOS host). Optional: nullptr keeps the "Markmos"
    // default, so apps that do not care need no change. Must have static
    // storage duration (a string literal) - the host converts it once
    // during window creation. Unused on platforms without a window title
    // (iOS and Android take theirs from the OS / manifest).
    const char *title;

    // Initial window size in points, content area. 0 (either field) keeps
    // the 900x640 default. The window stays resizable either way.
    u32    width  = 0;
    u32    height = 0;

    // Called once when the platform is ready (backend, input, audio initialized)
    void (*init)(void *user_data);

    // Called every frame — update game logic + issue draw commands
    void (*frame)(void *user_data, f32 dt, InputState &input);

    // Called on resize/orientation change — update width/height
    void (*resize)(void *user_data, u32 w, u32 h);

    // Called once on shutdown — destroy game state
    void (*cleanup)(void *user_data);

    void (*pause)(void *user_data);      // optional: called when app is backgrounded
    void (*resume)(void *user_data);     // optional: called when app is foreground
    void (*low_memory)(void *user_data); // optional: called when OS signals low memory

    // Optional: the graphics device went away and a new one has been created.
    // The app is NOT being destroyed — this is followed by a second init() on the
    // SAME user_data, and everything allocated at init() is expected to still be
    // there. Release only what the device owned (textures, pipelines, buffers)
    // and rebuild it; anything you free here cannot be recreated by init() if it
    // was app state rather than device state.
    //
    // This exists because device recovery used to call cleanup(), which is
    // documented above as "destroy game state" — and two of the apps implement
    // that by deleting the object user_data points at. The next init() then wrote
    // through the freed pointer.
    void (*device_lost)(void *user_data) = nullptr;
};

// Request a clean app exit (platform-specific; may be a no-op on mobile)
extern void             app_quit() noexcept;

// User must define this — returns AppCallbacks for the platform to invoke
extern "C" AppCallbacks markmos_main(int argc, char *argv[]);

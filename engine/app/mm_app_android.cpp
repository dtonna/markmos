// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Android App Entry — Sokol-style: calls user-defined markmos_main() for callbacks
// Platform handles: Vulkan backend, input queue, audio, native activity lifecycle

#include "../audio/mm_audio_system.hpp"
#include "core/mm_types.h"
#include "../core/mm_log.hpp"
#include "../core/mm_pool.hpp"
#include "../core/mm_vfs.hpp"
#include "../input/mm_input_event.hpp"
#include "../input/mm_input_state.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include "../rhi/mm_vulkan_backend.hpp"
#include "game/mm_event_bus.hpp"
#include "mm_frame_perf.hpp"
#include "mm_app.hpp"
#include "../core/mm_job_system.hpp"

#include <android_native_app_glue.h>
#include <time.h> // clock_gettime, CLOCK_MONOTONIC
#include <vulkan/vulkan_android.h>

// ─── Engine globals ───────────────────────────────────────────────────────────
// Note: g_event_bus is defined once in engine/game/mm_event_bus.cpp,
// declared extern via game/mm_event_bus.hpp — do NOT redefine here.
VulkanBackend      *g_backend       = nullptr;
EventBus           *g_event_bus_ptr = nullptr;
// ─── App callbacks ────────────────────────────────────────────────────────────
static AppCallbacks g_callbacks{};

// Defined here rather than beside the other globals because the cutout query below
// reads it, and that query runs from the resize path, which is earlier in the file.
static android_app *g_android_app = nullptr;

// ─── Timing helpers ───────────────────────────────────────────────────────────
// Uses CLOCK_MONOTONIC — unaffected by wall-clock adjustments, available on all
// Android API levels we support (≥26). Returns seconds as f64.
// How far the display cutout reaches down from the top edge, in POINTS.
//
// Android draws the camera as part of the display, and a fullscreen activity
// draws right through it, so the top of the screen is not usable. There is no NDK
// API for this — the cutout lives on the Java side (WindowInsets) — so this walks
// the Activity through JNI. native_app_glue exposes no JNIEnv either: it hands
// over an ANativeActivity, and the VM and the Activity object hang off THAT.
//
// Every step can fail independently, and every failure means the same thing: we
// do not know, so report 0. That is the correct answer on a device with no cutout,
// and it is also the right failure mode for a broken query — a wrong-but-large
// number pushes the whole board down the screen, whereas 0 merely restores the
// behaviour the app had before insets existed.
// Both edges come from one pass down the chain — the JNI walk costs five calls
    // and there is no reason to make it twice. `edges` is not named `insets`
    // because the chain already has a `jobject insets` two levels in, and inside
    // that lambda `insets.top` would resolve to the jobject.
    struct cutout_insets {
        f32 top    = 0.0f;
        f32 bottom = 0.0f;
    };

    static cutout_insets query_cutout_insets(ANativeActivity *activity) noexcept {
    if (activity == nullptr || activity->vm == nullptr || activity->clazz == nullptr) {
        return {};
    }

    // This runs on native_app_glue's own thread, which is NOT attached to the VM:
    // GetEnv returned JNI_EDETACHED (-2) here, measured. ANativeActivity::env is
    // no help either — the header restricts it to the process MAIN thread, and
    // reading a View field through another thread's JNIEnv returned a null window.
    // So the thread is attached for the duration of the walk and detached after.
    JNIEnv              *env = nullptr;
    JavaVMAttachArgs     args{};
    args.version = JNI_VERSION_1_6;
    args.name    = const_cast<char *>("freecell");
    args.group   = nullptr;
    const jint attached = activity->vm->AttachCurrentThread(&env, &args);
    if (attached != JNI_OK || env == nullptr) {
        MM_LOG("Display cutout: AttachCurrentThread failed (%d)", static_cast<int>(attached));
        return {};
    }

    cutout_insets edges;

    // Every early return below has to leave the thread as it found it. RAII, because
    // there are four of them and a missed Detach leaks a thread reference into the
    // VM for the life of the process.
    struct Detach {
        JavaVM *vm;
        ~Detach() {
            vm->DetachCurrentThread();
        }
    } detach{activity->vm};

    // Call a no-argument object method, run `body` on the result, then release the
    // local ref and swallow any exception the call raised. Every leg of the chain
    // below is one of these, so the reference bookkeeping and the exception
    // handling are written once instead of five times.
    auto chain = [&](jobject owner, jclass owner_cls, const char *name, const char *sig, const auto &body) noexcept -> bool {
        const jmethodID mid = env->GetMethodID(owner_cls, name, sig);
        if (mid == nullptr) {
            env->ExceptionClear();
            return false;
        }
        jobject result = env->CallObjectMethod(owner, mid);
        if (result == nullptr || env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        body(result);
        env->DeleteLocalRef(result);
        return true;
    };

    jclass activity_cls = env->GetObjectClass(activity->clazz);
    chain(activity->clazz, activity_cls, "getWindow", "()Landroid/view/Window;", [&](jobject window) {
        jclass window_cls = env->GetObjectClass(window);
        chain(window, window_cls, "getDecorView", "()Landroid/view/View;", [&](jobject decor) {
            jclass decor_cls = env->GetObjectClass(decor);
            chain(decor, decor_cls, "getRootWindowInsets", "()Landroid/view/WindowInsets;", [&](jobject insets) {
                jclass insets_cls = env->GetObjectClass(insets);
                chain(insets, insets_cls, "getDisplayCutout", "()Landroid/view/DisplayCutout;", [&](jobject cutout) {
                    jclass cutout_cls = env->GetObjectClass(cutout);
                    // Safe insets are in PIXELS. Everything the app lays out is in
                    // points, so divide by the same density the host already
                    // derived from the display metrics.
                    auto read_edge = [&](const char *method, f32 &out) noexcept {
                        const jmethodID mid = env->GetMethodID(cutout_cls, method, "()I");
                        if (mid == nullptr) {
                            env->ExceptionClear();
                            return;
                        }
                        const jint px = env->CallIntMethod(cutout, mid);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                        } else if (px > 0) {
                            out = static_cast<f32>(px) / fmaxf(g_content_scale, 0.01f);
                        }
                    };
                    read_edge("getSafeInsetTop", edges.top);
                    read_edge("getSafeInsetBottom", edges.bottom);
                    env->DeleteLocalRef(cutout_cls);

                    // getSafeInsetBottom() is the DISPLAY CUTOUT — a notch or a
                    // punch-hole. It is 0 on a phone whose only bottom hazard is the
                    // gesture bar or the three-button navigation bar, which is the
                    // common case and the one that actually reaches the action row.
                    // Those live in WindowInsets. Measured on a Pixel_10_Pro
                    // (gestural): cutout bottom 0, buttons 17.9pt off the edge,
                    // gesture handle 11.4pt.
                    //
                    // navigationBars is a static METHOD on WindowInsets.Type, not a
                    // field, and every failed lookup here clears the exception —
                    // ART aborts the NEXT JNI call made with one still pending, which
                    // is the same rule the chain lambda above follows.
                    if (jclass type_cls = env->FindClass("android/view/WindowInsets$Type")) {
                        const jmethodID nav_m = env->GetStaticMethodID(type_cls, "navigationBars", "()I");
                        if (nav_m == nullptr) {
                            env->ExceptionClear();
                        } else if (const jmethodID gi = env->GetMethodID(insets_cls, "getInsets", "(I)Landroid/graphics/Insets;")) {
                            const jint nav_type = env->CallStaticIntMethod(type_cls, nav_m);
                            jobject    nav_insets = env->CallObjectMethod(insets, gi, nav_type);
                            if (nav_insets == nullptr) {
                                env->ExceptionClear();
                            } else {
                                jclass nav_cls = env->GetObjectClass(nav_insets);
                                if (const jfieldID bot = env->GetFieldID(nav_cls, "bottom", "I")) {
                                    const jint px = env->GetIntField(nav_insets, bot);
                                    if (env->ExceptionCheck()) {
                                        env->ExceptionClear();
                                    } else if (px > 0) {
                                        edges.bottom = static_cast<f32>(px) / fmaxf(g_content_scale, 0.01f);
                                    }
                                } else {
                                    env->ExceptionClear();
                                }
                                env->DeleteLocalRef(nav_cls);
                                env->DeleteLocalRef(nav_insets);
                            }
                        } else {
                            env->ExceptionClear();
                        }
                        env->DeleteLocalRef(type_cls);
                    } else {
                        env->ExceptionClear();
                    }
                });
                env->DeleteLocalRef(insets_cls);
            });
            env->DeleteLocalRef(decor_cls);
        });
        env->DeleteLocalRef(window_cls);
    });
    env->DeleteLocalRef(activity_cls);
    return edges;
}

static f64       monotonic_now() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<f64>(ts.tv_sec) + static_cast<f64>(ts.tv_nsec) * 1e-9;
}

// ─── Keycode mapping ──────────────────────────────────────────────────────────
static KeyCode keycode_from_android(i32 kc) noexcept {
    switch (kc) {
    case 29:
        return KeyCode::A;
    case 30:
        return KeyCode::B;
    case 31:
        return KeyCode::C;
    case 32:
        return KeyCode::D;
    case 33:
        return KeyCode::E;
    case 34:
        return KeyCode::F;
    case 35:
        return KeyCode::G;
    case 36:
        return KeyCode::H;
    case 37:
        return KeyCode::I;
    case 38:
        return KeyCode::J;
    case 39:
        return KeyCode::K;
    case 40:
        return KeyCode::L;
    case 41:
        return KeyCode::M;
    case 42:
        return KeyCode::N;
    case 43:
        return KeyCode::O;
    case 44:
        return KeyCode::P;
    case 45:
        return KeyCode::Q;
    case 46:
        return KeyCode::R;
    case 47:
        return KeyCode::S;
    case 48:
        return KeyCode::T;
    case 49:
        return KeyCode::U;
    case 50:
        return KeyCode::V;
    case 51:
        return KeyCode::W;
    case 52:
        return KeyCode::X;
    case 53:
        return KeyCode::Y;
    case 54:
        return KeyCode::Z;
    case 7:
        return KeyCode::D0;
    case 8:
        return KeyCode::D1;
    case 9:
        return KeyCode::D2;
    case 10:
        return KeyCode::D3;
    case 11:
        return KeyCode::D4;
    case 12:
        return KeyCode::D5;
    case 13:
        return KeyCode::D6;
    case 14:
        return KeyCode::D7;
    case 15:
        return KeyCode::D8;
    case 16:
        return KeyCode::D9;
    case 19:
        return KeyCode::Up;
    case 20:
        return KeyCode::Down;
    case 21:
        return KeyCode::Left;
    case 22:
        return KeyCode::Right;
    case 59: // KEYCODE_SHIFT_LEFT
        return KeyCode::Shift;
    case 60: // KEYCODE_SHIFT_RIGHT - both shift keys map to one KeyCode, so a
        return KeyCode::Shift; // right-shift chord works like a left-shift one
    case 113:
        return KeyCode::Ctrl;
    case 57:
        return KeyCode::Alt;
    case 62:
        return KeyCode::Space;
    case 66:
        return KeyCode::Enter;
    case 111: // KEYCODE_ESCAPE (was 131, which is KEYCODE_F1 - F1 produced Escape
        return KeyCode::Escape; // and the real Escape key was unmapped)
    case 67:
        return KeyCode::Backspace;
    case 61:
        return KeyCode::Tab;
    case 92: // KEYCODE_PAGE_UP
        return KeyCode::PageUp;
    case 93: // KEYCODE_PAGE_DOWN
        return KeyCode::PageDown;
    case 134: // KEYCODE_F4
        return KeyCode::F4;
    case 112: // KEYCODE_FORWARD_DEL
        return KeyCode::Delete;
    case 122: // KEYCODE_MOVE_HOME
        return KeyCode::Home;
    case 123: // KEYCODE_MOVE_END
        return KeyCode::End;
    default:
        return KeyCode::Unknown;
    }
}

// ─── AndroidApp ───────────────────────────────────────────────────────────────
struct AndroidApp {
    ANativeWindow   *window          = nullptr;
    AAssetManager   *asset_manager   = nullptr;
    // Kept so resize() can re-ask the cutout: the notch is on the left in
    // landscape and the navigation bar appears and disappears.
    ANativeActivity   *activity       = nullptr;
    VulkanBackend   *backend         = nullptr;
    InputEventQueue *input_queue     = nullptr;
    InputState      *input_state     = nullptr;

    f64           last_frame_time = 0.0; // monotonic seconds; 0 = not yet set

    // Shared with the macOS and iOS hosts — see mm_frame_perf.hpp.
    FramePerf     perf;

    bool             active          = false;
    bool             paused          = false;
    i32          width           = 0;
    i32          height          = 0;
    f32            scale_factor    = 1.0f;

    void             init() noexcept {
        backend     = new VulkanBackend();
        g_backend   = backend;

        input_queue = new InputEventQueue();
        input_state = new InputState();
        input_state->init();

        g_audio_system.init();

        // Reset timing — will be seeded on the first frame() call
        last_frame_time = 0.0;
    }

    void shutdown(bool is_destroy = false) noexcept {
        if (g_callbacks.cleanup) {
            g_callbacks.cleanup(g_callbacks.user_data);
        }
        g_audio_system.shutdown();
        if (backend) {
            backend->shutdown();
            delete backend;
            backend   = nullptr;
            g_backend = nullptr;
        }
        delete input_state;
        input_state = nullptr;
        delete input_queue;
        input_queue     = nullptr;
        last_frame_time = 0.0;
        g_event_bus.clear();
        if (is_destroy) {
            JobSystemShutdown();
            PoolShutdown();
        }
    }

    void resize() noexcept {
        if (!window || !backend) {
            return;
        }
        i32 new_w = ANativeWindow_getWidth(window);
        i32 new_h = ANativeWindow_getHeight(window);
        // A window that was released (backgrounded) has no surface, so a swapchain
        // rebuild alone would fail: SwapchainBuilder needs a VkSurfaceKHR. Reset
        // width/height when the window is released, so this branch is reached
        // again on the next CONTENT_RECT_CHANGED and the window is rebuilt.
        if (backend->has_surface()) {
            if (new_w == width && new_h == height) {
                return;
            }
        } else {
            auto attached = backend->attach_surface(window);
            if (!attached) {
                MM_ERROR("resize: attach_surface failed (window %dx%d)", new_w, new_h);
                return;
            }
            MM_LOG("resize: window reattached at %dx%d", new_w, new_h);
        }
        width  = new_w;
        height = new_h;
        SurfaceInfo info{
            .native_handle = window,
            .width         = static_cast<u32>(width),
            .height        = static_cast<u32>(height),
            .content_scale = scale_factor > 1.0f ? scale_factor : 1.0f,
        };
        backend->resize(info);
        g_content_scale = scale_factor;

        // The cutout moves with the screen — a notch is on the left in landscape,
        // and the navigation bar only comes and goes — so it is asked here rather
        // than only at init and on focus gain.
        if (activity) {
            const cutout_insets e = query_cutout_insets(activity);
            g_ui_inset_top    = e.top;
            g_ui_inset_bottom = e.bottom;
            MM_LOG("Display cutout: inset_top=%.1fpt inset_bottom=%.1fpt", g_ui_inset_top, g_ui_inset_bottom);
        }

        const f32 cs = g_content_scale > 1.0f ? g_content_scale : 1.0f;
        if (g_callbacks.resize) {
            g_callbacks.resize(g_callbacks.user_data, static_cast<u32>(static_cast<f32>(width) / cs + 0.5f),
                               static_cast<u32>(static_cast<f32>(height) / cs + 0.5f));
        }
    }

    // resync_time() — call after any pause/resume so the first frame back
    // doesn't get a huge dt equal to however long the app was suspended.
    void resync_time() noexcept { last_frame_time = monotonic_now(); }

    void frame() noexcept {
        if (!active || paused || !backend) {
            return;
        }

        // ── Delta time ────────────────────────────────────────────────────────
        // Seed timing on first call (last_frame_time == 0 only once).
        f64 now = monotonic_now();
        if (last_frame_time == 0.0) {
            last_frame_time = now;
        }

        f32 dt        = static_cast<f32>(now - last_frame_time);
        last_frame_time = now;

        // Clamp: prevents spiral-of-death after a hiccup or a debugger break.
        // 0.1 s cap = max 10-frame catch-up, consistent with iOS/macOS.
        if (dt > 0.1f) {
            dt = 0.1f;
        }
        // Guard against clock glitches (backward jump, first-frame == 0)
        if (dt <= 0.0f) {
            dt = 1.0f / 60.0f;
        }

        // Recorded AFTER the clamp and the glitch guard: inside the guard it
        // never runs, because the line above it guarantees dt > 0. The first
        // version was inside, which compiled, put the string in the binary, and
        // reported nothing — indistinguishable from "nothing is slow".
        perf.record(dt * 1000.0f);

        // ── Update ────────────────────────────────────────────────────────────
        if (input_state && input_queue) {
            input_state->process(*input_queue, dt);
        }
        g_audio_system.update(dt);

        // ── Render ────────────────────────────────────────────────────────────
        auto begin_res = backend->begin_frame();
        if (!begin_res) {
            if (begin_res.error() == RHIError::DeviceLost) {
                MM_LOG("Device lost in begin_frame — triggering recovery");
                recover_device();
            }
            return;
        }
        if (g_callbacks.frame) {
            g_callbacks.frame(g_callbacks.user_data, dt, *input_state);
        }
        auto end_res = backend->end_frame();
        if (!end_res) {
            if (end_res.error() == RHIError::DeviceLost) {
                MM_LOG("Device lost in end_frame — triggering recovery");
                recover_device();
            }
        }
    }

    void recover_device() noexcept {
        MM_LOG("Recovering from Vulkan device lost...");
        active = false;

        // NOT cleanup(). cleanup() is documented as "destroy game state" and the
        // apps implement it by deleting the object user_data points at — which is
        // this very app. init() below then runs on the SAME pointer, so cleanup()
        // here turned device recovery into a use-after-free.
        //
        // An app that does not opt in keeps whatever it had: wrong, but
        // survivable. An app that opts in releases only what the device owned.
        if (g_callbacks.device_lost) {
            g_callbacks.device_lost(g_callbacks.user_data);
        }

        backend->shutdown();

        auto result = backend->init(window);
        if (!result) {
            MM_ERROR("Device recovery failed — cannot re-init Vulkan backend");
            app_quit();
            return;
        }
        MM_LOG("Device recovery: Vulkan backend re-initialized");

        g_content_scale = scale_factor > 1.0f ? scale_factor : 1.0f;
        const cutout_insets e = query_cutout_insets(g_android_app != nullptr ? g_android_app->activity : nullptr);
        g_ui_inset_top        = e.top;
        g_ui_inset_bottom     = e.bottom;
        if (g_callbacks.init) {
            g_callbacks.init(g_callbacks.user_data);
        }

        f32 cs = g_content_scale > 1.0f ? g_content_scale : 1.0f;
        if (g_callbacks.resize) {
            g_callbacks.resize(g_callbacks.user_data, static_cast<u32>(static_cast<f32>(width) / cs + 0.5f),
                               static_cast<u32>(static_cast<f32>(height) / cs + 0.5f));
        }

        active = true;
        resync_time();
        MM_LOG("Device recovery complete");
    }
};

static AndroidApp   g_app;

void                app_quit() noexcept {
    if (g_android_app && g_android_app->activity) {
        ANativeActivity_finish(g_android_app->activity);
    }
}

// ─── Command & input handlers ─────────────────────────────────────────────────
extern "C" {

void handle_cmd(android_app *app, i32 cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW: {
        g_app.window        = app->window;
        g_app.asset_manager = app->activity->assetManager;
        g_app.activity     = app->activity;
        g_app.width         = ANativeWindow_getWidth(app->window);
        g_app.height        = ANativeWindow_getHeight(app->window);
        MM_LOG("Window size: %dx%d", g_app.width, g_app.height);

        g_app.scale_factor = AConfiguration_getDensity(app->config) / 160.0f;
        g_content_scale    = g_app.scale_factor;

        // The cutout is a property of the WINDOW the activity just received, and it
        // is divided by the density read on the line above, so it has to be asked
        // for here and not at device-init. Taken from `app` rather than the
        // file-static: handle_cmd carries the handle it was given, and whether the
        // global is assigned before the first command is processed is an ordering
        // detail of native_app_glue that has no business being load-bearing.
        const cutout_insets e = query_cutout_insets(app->activity);
        g_ui_inset_top        = e.top;
        g_ui_inset_bottom     = e.bottom;
        MM_LOG("Display cutout: inset_top=%.1fpt", g_ui_inset_top);

        if (!g_app.backend) {
            g_app.init();
            auto result = g_app.backend->init(g_app.window);
            if (!result) {
                MM_ERROR("Vulkan backend initialization FAILED!");
                app_quit();
                break;
            }
            MM_LOG("Vulkan backend initialized successfully");
        } else {
            // A second window on a device that is already running. Rebuild the
            // surface and swapchain against the new ANativeWindow and leave every
            // texture, pipeline, shader and descriptor exactly as it is — they
            // belong to the device, not to the window they were first drawn into.
            auto result = g_app.backend->attach_surface(g_app.window);
            if (!result) {
                // Not fatal: the window may simply not have its final size yet.
                // g_app.resize() retries through attach_surface on the next
                // CONTENT_RECT_CHANGED, and quitting here would turn a transient
                // condition into a lost game.
                MM_ERROR("attach_surface failed on resume — will retry on resize");
                g_app.active = false;
                break;
            }
        }

        Vfs::set_asset_manager(g_app.asset_manager);
        g_vfs.init("", app->activity->internalDataPath);

        // Once per PROCESS, not once per window. Android delivers
        // APP_CMD_INIT_WINDOW again every time the activity returns from the
        // background — a Home press and return IS a new window — and the engine
        // above already guards its own init(). This callback was not guarded, so
        // every resume re-ran game_init: re-created the renderer, the audio
        // system and the card atlas on top of live ones. It crashed on the first
        // write through one of them, and would have thrown the board away even if
        // it had not. The device-recovery path above deliberately re-inits, and
        // is a different statement for that reason.
        static bool s_callbacks_inited = false;
        if (!s_callbacks_inited) {
            s_callbacks_inited = true;
            if (g_callbacks.init) {
                g_callbacks.init(g_callbacks.user_data);
            }
        }

        f32 cs = g_content_scale > 1.0f ? g_content_scale : 1.0f;
        if (g_callbacks.resize) {
            g_callbacks.resize(g_callbacks.user_data, static_cast<u32>(static_cast<f32>(g_app.width) / cs + 0.5f),
                               static_cast<u32>(static_cast<f32>(g_app.height) / cs + 0.5f));
        }

        // Seed timing right before we start rendering, not at init(),
        // so the window-setup time doesn't inflate the very first dt.
        g_app.resync_time();
        g_app.active = true;
        break;
    }

    case APP_CMD_CONTENT_RECT_CHANGED: {
      // A rotated device hands us a NEW surface at the new size, and this app
      // never noticed: the render loop kept going at the old size and after a
      // rotation the game simply looked wrong. Invisible as long as the manifest
      // pinned an orientation, which it did — and which is why the case was never
      // written in the first place. Unpinning it exposed a gap that had been
      // there since the first Android build.
      //
      // APP_CMD_CONTENT_RECT_CHANGED is the modern spelling and the only one the
      // NDK still declares; the older APP_CMD_WINDOW_SIZE_CHANGED was removed,
      // and this event covers both a real resize and an inset appearing.
      //
      // This used to carry its own copy of the size check, the backend rebuild and
      // the callbacks.resize() call. Two copies put the early-out that skips an
      // unchanged size in one of them but not the other, so a backgrounded app —
      // whose remembered size is deliberately reset — could still be told "nothing
      // to do" here and never rebuild the window it had just released. resize() is
      // the only path now.
      g_app.resize();
      g_app.resync_time();
      break;
  }

  case APP_CMD_TERM_WINDOW:
        // NOT a shutdown. Android sends this every time the activity goes to the
        // background — a Home press and return is a window teardown — and
        // shutdown() answers it by running cleanup (which deletes the app),
        // destroying the Vulkan instance and device with every texture, pipeline
        // and descriptor set in it, and clearing the event bus and its
        // subscribers. android_main does not run again, so nothing could rebuild
        // any of it: the next INIT_WINDOW wrote through a dangling user_data.
        //
        // Release the window's own objects and keep the device. That is the whole
        // difference, and it is the same split Vulkan itself draws.
        g_app.active = false;
        if (g_app.backend) {
            g_app.backend->release_surface();
        }
        // Forget the size too, so a later resize() sees a change and rebuilds the
        // window instead of deciding there is nothing to do.
        g_app.width  = 0;
        g_app.height = 0;
        g_app.window = nullptr;
        break;

    case APP_CMD_PAUSE:
        g_app.paused = true;
        // The host owns the audio engine (it calls g_audio_system.shutdown()
        // itself), so it is the host that suspends it. An app that has to
        // remember to do this gets it wrong on exactly the platform where a
        // phone in a pocket is loudest.
        g_audio_system.pause();
        // The first frames back are not representative of anything.
        g_app.perf.reset();
        if (g_app.backend) {
            vkDeviceWaitIdle(g_app.backend->device);
        }
        // Android can kill a backgrounded process with no further notice, so this
        // is the last point at which an app can put something on disk. The
        // callback existed on AppCallbacks and no host ever called it, which is
        // why a save could sit in a work queue that never drained.
        if (g_callbacks.pause != nullptr) {
            g_callbacks.pause(g_callbacks.user_data);
        }
        break;

    case APP_CMD_RESUME:
        // Resync so the first frame after resume has a sane dt,
        // not the full duration the app spent suspended.
        g_app.resync_time();
        g_app.paused = false;
        g_audio_system.resume();
        if (g_callbacks.resume != nullptr) {
            g_callbacks.resume(g_callbacks.user_data);
        }
        break;

    case APP_CMD_GAINED_FOCUS:
        g_app.active = true;
        // Re-ask for the cutout here rather than only at INIT_WINDOW. WindowInsets
        // are attached when the decor view is first laid out, which happens AFTER
        // the window is created — so at INIT_WINDOW the chain returns a null
        // cutout and the answer is a 0 that is indistinguishable from "this device
        // has no notch". GAINED_FOCUS is the first moment the window is actually
        // up. Cheap, and it also picks up a rotation: the manifest takes
        // orientation changes without recreating the activity, and a notch can move.
        {
            const cutout_insets e = query_cutout_insets(app->activity);
            if (e.top != g_ui_inset_top) {
                g_ui_inset_top = e.top;
                MM_LOG("Display cutout: inset_top=%.1fpt (changed)", g_ui_inset_top);
            }
            if (e.bottom != g_ui_inset_bottom) {
                g_ui_inset_bottom = e.bottom;
                MM_LOG("Display cutout: inset_bottom=%.1fpt (changed)", g_ui_inset_bottom);
            }
        }
        break;

    case APP_CMD_LOST_FOCUS:
        if (g_app.input_state) {
            g_app.input_state->touch.reset();
        }
        if (g_app.input_queue) {
            g_app.input_queue->reset();
        }
        g_app.active = false;
        break;

    case APP_CMD_CONFIG_CHANGED:
        g_app.resize();
        break;

    case APP_CMD_LOW_MEMORY:
        break;
    }
}

i32 handle_input(android_app *app, AInputEvent *event) {
    if (!g_app.input_queue) {
        return 0;
    }

    i32 type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        i32 action        = AMotionEvent_getAction(event);
        i32 pointer_index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        i32 pointer_id    = AMotionEvent_getPointerId(event, pointer_index);
        f32   cs            = g_content_scale > 1.0f ? g_content_scale : 1.0f;
        f32   x             = AMotionEvent_getX(event, pointer_index) / cs;
        f32   y             = AMotionEvent_getY(event, pointer_index) / cs;

        switch (action & AMOTION_EVENT_ACTION_MASK) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            g_app.input_queue->push(InputEvent::make_touch_down(static_cast<u8>(pointer_id), x, y));
            break;

        case AMOTION_EVENT_ACTION_MOVE: {
            size_t count = AMotionEvent_getPointerCount(event);
            for (size_t i = 0; i < count && i < 5; ++i) {
                i32 pid = AMotionEvent_getPointerId(event, i);
                f32   px  = AMotionEvent_getX(event, i) / cs;
                f32   py  = AMotionEvent_getY(event, i) / cs;
                g_app.input_queue->push(InputEvent::make_touch_move(static_cast<u8>(pid), px, py));
            }
            break;
        }

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            g_app.input_queue->push(InputEvent::make_touch_up(static_cast<u8>(pointer_id)));
            break;

        case AMOTION_EVENT_ACTION_CANCEL:
            g_app.input_queue->push(InputEvent::make_touch_cancel(static_cast<u8>(pointer_id)));
            break;
        }
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        i32 action = AKeyEvent_getAction(event);
        i32 kc     = AKeyEvent_getKeyCode(event);
        KeyCode code   = keycode_from_android(kc);
        if (code == KeyCode::Unknown) {
            return 0;
        }
        if (action == AKEY_EVENT_ACTION_DOWN) {
            g_app.input_queue->push(InputEvent::make_key_down(code));
        } else if (action == AKEY_EVENT_ACTION_UP) {
            g_app.input_queue->push(InputEvent::make_key_up(code));
        }
        return 1;
    }

    return 0;
}

// ─── Main loop ────────────────────────────────────────────────────────────────
void android_main(android_app *app) {
    MM_LOG("android_main() started");
    PoolInit();
    JobSystemInit(0);
    g_event_bus.clear();
    g_event_bus_ptr   = &g_event_bus;

    g_android_app     = app;
    g_callbacks       = markmos_main(0, nullptr);

    app->onAppCmd     = handle_cmd;
    app->onInputEvent = handle_input;

    bool window_ready = false;

    while (true) {
        int                  events;
        android_poll_source *source;

        // Block until an event arrives when idle; poll without blocking when rendering
        int                  timeout     = (window_ready && g_app.active && !g_app.paused) ? 0 : -1;

        int                  poll_result = ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void **>(&source));

        if (poll_result >= 0) {
            if (source) {
                source->process(app, source);
            }

            if (app->destroyRequested) {
                g_app.shutdown(true);
                return;
            }

            if (!window_ready && app->window != nullptr) {
                window_ready = true;
            }
        }

        if (window_ready && g_app.active && !g_app.paused) {
            g_app.frame(); // dt computed inside — no argument needed
        }
    }
}

} // extern "C"

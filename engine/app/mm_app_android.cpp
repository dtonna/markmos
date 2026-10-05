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
#include "mm_app.hpp"
#include "mm_job_system.hpp"

#include <android_native_app_glue.h>
#include <time.h> // clock_gettime, CLOCK_MONOTONIC
#include <vulkan/vulkan_android.h>

// ─── Engine globals ───────────────────────────────────────────────────────────
// Note: g_event_bus is defined once in engine/game/mm_event_bus.cpp,
// declared extern via game/mm_event_bus.hpp — do NOT redefine here.
f32               g_content_scale = 1.0f;
VulkanBackend      *g_backend       = nullptr;
EventBus           *g_event_bus_ptr = nullptr;
// ─── App callbacks ────────────────────────────────────────────────────────────
static AppCallbacks g_callbacks{};

// ─── Timing helpers ───────────────────────────────────────────────────────────
// Uses CLOCK_MONOTONIC — unaffected by wall-clock adjustments, available on all
// Android API levels we support (≥26). Returns seconds as f64.
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
    VulkanBackend   *backend         = nullptr;
    InputEventQueue *input_queue     = nullptr;
    InputState      *input_state     = nullptr;

    f64           last_frame_time = 0.0; // monotonic seconds; 0 = not yet set

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
        if (new_w == width && new_h == height) {
            return;
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
        f32 cs        = g_content_scale > 1.0f ? g_content_scale : 1.0f;
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

        if (g_callbacks.cleanup) {
            g_callbacks.cleanup(g_callbacks.user_data);
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
static android_app *g_android_app = nullptr;

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
        g_app.width         = ANativeWindow_getWidth(app->window);
        g_app.height        = ANativeWindow_getHeight(app->window);
        MM_LOG("Window size: %dx%d", g_app.width, g_app.height);

        g_app.scale_factor = AConfiguration_getDensity(app->config) / 160.0f;
        g_content_scale    = g_app.scale_factor;

        if (!g_app.backend) {
            g_app.init();
        }

        {
            auto result = g_app.backend->init(g_app.window);
            if (!result) {
                MM_ERROR("Vulkan backend initialization FAILED!");
                app_quit();
                break;
            }
            MM_LOG("Vulkan backend initialized successfully");
        }

        Vfs::set_asset_manager(g_app.asset_manager);
        g_vfs.init("", app->activity->internalDataPath);

        if (g_callbacks.init) {
            g_callbacks.init(g_callbacks.user_data);
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

    case APP_CMD_TERM_WINDOW:
        g_app.active = false;
        g_app.shutdown(false);
        g_app.window = nullptr;
        break;

    case APP_CMD_PAUSE:
        g_app.paused = true;
        if (g_app.backend) {
            vkDeviceWaitIdle(g_app.backend->device);
        }
        break;

    case APP_CMD_RESUME:
        // Resync so the first frame after resume has a sane dt,
        // not the full duration the app spent suspended.
        g_app.resync_time();
        g_app.paused = false;
        break;

    case APP_CMD_GAINED_FOCUS:
        g_app.active = true;
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

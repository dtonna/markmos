// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// Android App Entry — Sokol-style: calls user-defined markmos_main() for callbacks
// Platform handles: Vulkan backend, input queue, audio, native activity lifecycle

#include "mm_app.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include "../rhi/mm_vulkan_backend.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../audio/mm_audio_system.hpp"
#include "../input/mm_input_event.hpp"
#include "../input/mm_input_state.hpp"
#include "../core/mm_vfs.hpp"

#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

// Engine globals (accessible to user code via extern)
VulkanBackend* g_backend = nullptr;

// App callbacks (set by android_main() from user's markmos_main())
static AppCallbacks g_callbacks{};

static KeyCode keycode_from_android(int32_t kc) noexcept {
    switch (kc) {
        case 29: return KeyCode::A; case 30: return KeyCode::B;
        case 31: return KeyCode::C; case 32: return KeyCode::D;
        case 33: return KeyCode::E; case 34: return KeyCode::F;
        case 35: return KeyCode::G; case 36: return KeyCode::H;
        case 37: return KeyCode::I; case 38: return KeyCode::J;
        case 39: return KeyCode::K; case 40: return KeyCode::L;
        case 41: return KeyCode::M; case 42: return KeyCode::N;
        case 43: return KeyCode::O; case 44: return KeyCode::P;
        case 45: return KeyCode::Q; case 46: return KeyCode::R;
        case 47: return KeyCode::S; case 48: return KeyCode::T;
        case 49: return KeyCode::U; case 50: return KeyCode::V;
        case 51: return KeyCode::W; case 52: return KeyCode::X;
        case 53: return KeyCode::Y; case 54: return KeyCode::Z;
        case 7:  return KeyCode::D0; case 8:  return KeyCode::D1;
        case 9:  return KeyCode::D2; case 10: return KeyCode::D3;
        case 11: return KeyCode::D4; case 12: return KeyCode::D5;
        case 13: return KeyCode::D6; case 14: return KeyCode::D7;
        case 15: return KeyCode::D8; case 16: return KeyCode::D9;
        case 19: return KeyCode::Up;     case 20: return KeyCode::Down;
        case 21: return KeyCode::Left;   case 22: return KeyCode::Right;
        case 59: return KeyCode::Shift;  case 113: return KeyCode::Ctrl;
        case 57: return KeyCode::Alt;
        case 62: return KeyCode::Space;  case 66: return KeyCode::Enter;
        case 131: return KeyCode::Escape; case 67: return KeyCode::Backspace;
        case 61: return KeyCode::Tab;
        default:  return KeyCode::Unknown;
    }
}

struct AndroidApp {
    ANativeWindow*    window;
    AAssetManager*    asset_manager;
    VulkanBackend*    backend;
    InputEventQueue*  input_queue;
    InputState*       input_state;
    float             time;
    bool              active;
    int32_t           width;
    int32_t           height;
    float             scale_factor;

    void init() noexcept {
        backend = new VulkanBackend();
        g_backend = backend;
        input_queue = new InputEventQueue();
        input_state = new InputState();
        input_state->init();
        g_audio_system.init();
        time    = 0.0f;
    }

    void shutdown() noexcept {
        if (g_callbacks.cleanup) g_callbacks.cleanup(g_callbacks.user_data);
        g_audio_system.shutdown();
        if (backend) {
            backend->shutdown();
            delete backend;
            backend = nullptr;
            g_backend = nullptr;
        }
        delete input_state;
        input_state = nullptr;
        delete input_queue;
        input_queue = nullptr;
    }

    void resize() noexcept {
        if (!window || !backend) return;
        int32_t new_w = ANativeWindow_getWidth(window);
        int32_t new_h = ANativeWindow_getHeight(window);
        if (new_w == width && new_h == height) return;
        width = new_w;
        height = new_h;
        backend->resize();
        if (g_callbacks.resize) {
            g_callbacks.resize(g_callbacks.user_data,
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height));
        }
    }

    void frame(float dt) noexcept {
        if (!active || !backend) return;
        time += dt;

        // Process input
        if (input_state && input_queue) {
            input_state->process(*input_queue, dt);
        }
        g_audio_system.update(dt);

        // Render lifecycle
        backend->begin_frame();
        PassDesc pass{};
        pass.clear_color[0] = 0.1f; pass.clear_color[1] = 0.1f;
        pass.clear_color[2] = 0.2f; pass.clear_color[3] = 1.0f;
        pass.color_load = LoadOp::Clear;
        backend->begin_pass(pass);

        // User frame callback — game logic + draw calls (inside render pass)
        if (g_callbacks.frame) {
            g_callbacks.frame(g_callbacks.user_data, dt, *input_state);
        }

        backend->end_pass();
        backend->end_frame();
    }
};

static AndroidApp g_app;

extern "C" {

void handle_cmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            g_app.window = app->window;
            g_app.asset_manager = app->activity->assetManager;
            g_app.width = ANativeWindow_getWidth(app->window);
            g_app.height = ANativeWindow_getHeight(app->window);
            g_app.scale_factor = AConfiguration_getDensity(app->config) / 160.0f;

            if (!g_app.backend) g_app.init();
            g_app.backend->init(g_app.window);
            g_app.active = true;

            // Init VFS
            Vfs::set_asset_manager(g_app.asset_manager);
            g_vfs.init("", app->activity->internalDataPath);

            // User init callback
            if (g_callbacks.init) {
                g_callbacks.init(g_callbacks.user_data);
            }

            // Notify user code of initial size
            if (g_callbacks.resize) {
                g_callbacks.resize(g_callbacks.user_data,
                    static_cast<uint32_t>(g_app.width),
                    static_cast<uint32_t>(g_app.height));
            }
            break;

        case APP_CMD_TERM_WINDOW:
            g_app.active = false;
            g_app.shutdown();
            g_app.window = nullptr;
            break;

        case APP_CMD_GAINED_FOCUS:
            g_app.active = true;
            break;

        case APP_CMD_LOST_FOCUS:
            if (g_app.input_state) g_app.input_state->touch.reset();
            if (g_app.input_queue) g_app.input_queue->reset();
            g_app.active = false;
            break;

        case APP_CMD_LOW_MEMORY:
            break;

        case APP_CMD_CONFIG_CHANGED:
            g_app.resize();
            break;
    }
}

int32_t handle_input(android_app* app, AInputEvent* event) {
    if (!g_app.input_queue) return 0;

    int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t pointer_index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        int32_t pointer_id = AMotionEvent_getPointerId(event, pointer_index);
        float x = AMotionEvent_getX(event, pointer_index);
        float y = AMotionEvent_getY(event, pointer_index);

        switch (action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                g_app.input_queue->push(
                    InputEvent::make_touch_down(static_cast<uint8_t>(pointer_id), x, y));
                break;

            case AMOTION_EVENT_ACTION_MOVE: {
                size_t count = AMotionEvent_getPointerCount(event);
                for (size_t i = 0; i < count && i < 5; ++i) {
                    int32_t pid = AMotionEvent_getPointerId(event, i);
                    float px = AMotionEvent_getX(event, i);
                    float py = AMotionEvent_getY(event, i);
                    g_app.input_queue->push(
                        InputEvent::make_touch_move(static_cast<uint8_t>(pid), px, py));
                }
                break;
            }

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                g_app.input_queue->push(
                    InputEvent::make_touch_up(static_cast<uint8_t>(pointer_id)));
                break;

            case AMOTION_EVENT_ACTION_CANCEL:
                g_app.input_queue->push(
                    InputEvent::make_touch_cancel(static_cast<uint8_t>(pointer_id)));
                break;
        }
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t action = AKeyEvent_getAction(event);
        int32_t kc = AKeyEvent_getKeyCode(event);
        KeyCode code = keycode_from_android(kc);
        if (code == KeyCode::Unknown) return 0;

        if (action == AKEY_EVENT_ACTION_DOWN) {
            g_app.input_queue->push(InputEvent::make_key_down(code));
        } else if (action == AKEY_EVENT_ACTION_UP) {
            g_app.input_queue->push(InputEvent::make_key_up(code));
        }
        return 1;
    }

    return 0;
}

void android_main(android_app* app) {
    g_callbacks = markmos_main(0, nullptr);

    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    g_app.active = true;

    while (true) {
        int events;
        android_poll_source* source;
        while (ALooper_pollOnce(g_app.active ? 0 : -1, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                g_app.shutdown();
                return;
            }
        }

        if (g_app.active) {
            float dt = 1.0f / 60.0f;
            g_app.frame(dt);
        }
    }
}

}  // extern "C"

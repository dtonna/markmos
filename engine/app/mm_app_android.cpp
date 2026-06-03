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
#include "../core/mm_pool.hpp"
#include "../core/mm_log.hpp"

#include <android_native_app_glue.h>
#include <vulkan/vulkan_android.h>

// Engine globals (accessible to user code via extern)
float            g_content_scale = 1.0f;
VulkanBackend*   g_backend = nullptr;

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
    bool              paused;          // APP_CMD_PAUSE/RESUME state
    int32_t           width;
    int32_t           height;
    float             scale_factor;

    void init() noexcept {
//        MM_LOG("AndroidApp::init() - Creating VulkanBackend");
        backend = new VulkanBackend();
        g_backend = backend;

//        MM_LOG("AndroidApp::init() - Creating InputEventQueue");
        input_queue = new InputEventQueue();

//        MM_LOG("AndroidApp::init() - Creating InputState");
        input_state = new InputState();
        input_state->init();

//        MM_LOG("AndroidApp::init() - Initializing AudioSystem");
        g_audio_system.init();

        time    = 0.0f;
//        MM_LOG("AndroidApp::init() - Finished base initialization");
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
        g_content_scale = this->scale_factor;
        float cs = g_content_scale > 1.0f ? g_content_scale : 1.0f;
        if (g_callbacks.resize) {
            g_callbacks.resize(g_callbacks.user_data,
                static_cast<uint32_t>(static_cast<float>(width) / cs + 0.5f),
                static_cast<uint32_t>(static_cast<float>(height) / cs + 0.5f));
        }
    }

    void frame(float dt) noexcept {
//        MM_LOG("frame() START: active=%d paused=%d backend=%p dt=%.3f", active, paused, backend, dt);
        if (!active || paused || !backend) {
//            MM_LOG("frame() early return: active=%d paused=%d backend=%p", active, paused, backend);
            return;
        }
        time += dt;

        // Process input
//        MM_LOG("frame() processing input...");
        if (input_state && input_queue) {
//            MM_LOG("frame() input_state=%p input_queue=%p", input_state, input_queue);
            input_state->process(*input_queue, dt);
//            MM_LOG("frame() input processed OK");
        }
        
//        MM_LOG("frame() updating audio...");
        g_audio_system.update(dt);
//        MM_LOG("frame() audio updated OK");

        // Render lifecycle
//        MM_LOG("frame() calling backend->begin_frame()...");
        auto begin_res = backend->begin_frame();
//        MM_LOG("frame() begin_frame() returned: %d", (int)begin_res.error());
        if (!begin_res) {
            MM_ERROR("frame() begin_frame() failed: %d", (int)begin_res.error());
            return;
        }

        // User frame callback — game logic + draw calls (responsible for its own passes)
//        MM_LOG("frame() calling user callback: cb=%p", g_callbacks.frame);
        if (g_callbacks.frame) {
//            MM_LOG("frame() inside user callback");
            g_callbacks.frame(g_callbacks.user_data, dt, *input_state);
//            MM_LOG("frame() user callback returned OK");
        }

//        MM_LOG("frame() calling backend->end_frame()...");
        backend->end_frame();
//        MM_LOG("frame() END - completed successfully");
    }
};

static AndroidApp g_app;
static android_app* g_android_app = nullptr;

void app_quit() noexcept {
    if (g_android_app && g_android_app->activity) {
        ANativeActivity_finish(g_android_app->activity);
    }
}

extern "C" {

void handle_cmd(android_app* app, int32_t cmd) {
//    MM_LOG("handle_cmd() called: cmd=%d, active=%d", cmd, g_app.active);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: {
//            MM_LOG("APP_CMD_INIT_WINDOW received. Window: %p", app->window);
            g_app.window = app->window;
            g_app.asset_manager = app->activity->assetManager;
            g_app.width = ANativeWindow_getWidth(app->window);
            g_app.height = ANativeWindow_getHeight(app->window);
            MM_LOG("Window size: %dx%d", g_app.width, g_app.height);
            g_app.scale_factor = AConfiguration_getDensity(app->config) / 160.0f;
            g_content_scale = g_app.scale_factor;

            if (!g_app.backend) {
//                MM_LOG("g_app.backend is null, calling g_app.init()");
                g_app.init();
            }

//            MM_LOG("Calling g_app.backend->init(window)");
            {
                auto result = g_app.backend->init(g_app.window);
                if (!result) {
                    MM_ERROR("Vulkan backend initialization FAILED!");
                    app_quit(); // Abort if backend fails
                    break;
                } else {
                    MM_LOG("Vulkan backend initialized successfully");
                }
            }
            g_app.active = true;

            // Init VFS
//            MM_LOG("Initializing VFS (Internal Data Path: %s)", app->activity->internalDataPath);
            Vfs::set_asset_manager(g_app.asset_manager);
            g_vfs.init("", app->activity->internalDataPath);

            // User init callback
            if (g_callbacks.init) {
//                MM_LOG("Calling user init callback...");
                g_callbacks.init(g_callbacks.user_data);
//                MM_LOG("User init callback finished");
            }

            // Notify user code of initial size (logical DIPs)
            float cs = g_content_scale > 1.0f ? g_content_scale : 1.0f;
            if (g_callbacks.resize) {
                g_callbacks.resize(g_callbacks.user_data,
                    static_cast<uint32_t>(static_cast<float>(g_app.width) / cs + 0.5f),
                    static_cast<uint32_t>(static_cast<float>(g_app.height) / cs + 0.5f));
            }
            break;
        }

        case APP_CMD_TERM_WINDOW:
            g_app.active = false;
            g_app.shutdown();
            g_app.window = nullptr;
            break;

        case APP_CMD_RESUME:
//            MM_LOG("APP_CMD_RESUME: resuming rendering");
            g_app.paused = false;
            break;

        case APP_CMD_PAUSE:
//            MM_LOG("APP_CMD_PAUSE: pausing rendering");
            g_app.paused = true;
            if (g_app.backend) {
//                MM_LOG("APP_CMD_PAUSE: calling vkDeviceWaitIdle");
                vkDeviceWaitIdle(g_app.backend->device);
//                MM_LOG("APP_CMD_PAUSE: vkDeviceWaitIdle complete");
            }
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
        float cs = g_content_scale > 1.0f ? g_content_scale : 1.0f;
        float x = AMotionEvent_getX(event, pointer_index) / cs;
        float y = AMotionEvent_getY(event, pointer_index) / cs;

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
                    float cs2 = g_content_scale > 1.0f ? g_content_scale : 1.0f;
                    float px = AMotionEvent_getX(event, i) / cs2;
                    float py = AMotionEvent_getY(event, i) / cs2;
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
//    MM_LOG("android_main() started");

    // Initialize pool allocator (and rpmalloc)
    PoolInit();
//    MM_LOG("Pool allocator initialized");

    g_android_app = app;
    g_callbacks = markmos_main(0, nullptr);

    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    // Wait for window to be initialized before setting active
    bool window_initialized = false;

    while (true) {
//        MM_LOG("=== MAIN LOOP START ===");
        int events;
        android_poll_source* source;
        // If window not initialized, use -1 timeout to wait indefinitely
        // Once initialized, use 0 for non-blocking high-freq updates
        int timeout = (window_initialized && g_app.active) ? 0 : -1;
        
//        MM_LOG("main_loop: calling ALooper_pollOnce(timeout=%d)", timeout);
        int poll_result = ALooper_pollOnce(timeout, nullptr, &events,
                                reinterpret_cast<void**>(&source));
//        MM_LOG("main_loop: ALooper_pollOnce returned %d", poll_result);
        
        if (poll_result >= 0) {
//            MM_LOG("main_loop: processing source=%p", source);
            if (source) source->process(app, source);
//            MM_LOG("main_loop: source processed");
            
            if (app->destroyRequested) {
//                MM_LOG("main_loop: destroyRequested=true, shutting down");
                g_app.shutdown();
                return;
            }
            
            // Check if window just became available
            if (!window_initialized && app->window != nullptr) {
//                MM_LOG("Window became available, setting active=true");
                window_initialized = true;
                g_app.active = true;
            }
        }

        if (g_app.active && window_initialized) {
//            MM_LOG("main_loop: about to call g_app.frame()");
            float dt = 1.0f / 60.0f;
            g_app.frame(dt);
//            MM_LOG("main_loop: g_app.frame() returned successfully");
        }
//        MM_LOG("=== MAIN LOOP END ===");
    }
}

}  // extern "C"

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// iOS App Entry — Sokol-style: calls user-defined markmos_main() for callbacks
// Platform handles: Metal backend, input queue, audio, CADisplayLink lifecycle

#include "game/mm_event_bus.hpp"
#include "core/mm_types.h"
#include "mm_app.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include "../rhi/mm_metal_backend.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../audio/mm_audio_system.hpp"
#include "../input/mm_input_event.hpp"
#include "../input/mm_input_state.hpp"
#include "../core/mm_vfs.hpp"
#include "../core/mm_pool.hpp"
#include "../core/mm_job_system.hpp"
#include "../core/mm_log.hpp"

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreHaptics/CoreHaptics.h>
#import <objc/message.h>

// ─── Engine globals (accessible to user code via extern) ─────────────────────
// Note: g_event_bus is defined once in engine/game/mm_event_bus.cpp,
// declared extern via game/mm_event_bus.hpp — do NOT redefine here.
MetalBackend*    g_backend       = nullptr;
InputEventQueue* g_input_queue   = nullptr;
InputState*      g_input_state   = nullptr;
f32            g_content_scale = 1.0f;
EventBus*        g_event_bus_ptr = nullptr;

// ─── App callbacks ────────────────────────────────────────────────────────────
static AppCallbacks g_callbacks{};

void app_quit() noexcept {
    // iOS does not support programmatic app termination
}

// ─── Forward declarations ─────────────────────────────────────────────────────
@class MetalView;

// ─── GameViewController ───────────────────────────────────────────────────────
// Handles orientation change — notifies backend + game code with consistent units
// backend receives pixels, callback receives logical points (same as Android DIP pattern)

@interface GameViewController : UIViewController
@property (strong, nonatomic) MetalView *metalView;
@end

@implementation GameViewController
- (void)viewWillTransitionToSize:(CGSize)size
       withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator {
    [super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];
    CGFloat scale = self.view.window.screen.scale;
    g_content_scale = static_cast<f32>(scale);
    CAMetalLayer* layer = (CAMetalLayer*)self.view.layer;
    // backend expects pixels
    auto px_w = static_cast<u32>(size.width  * scale);
    auto px_h = static_cast<u32>(size.height * scale);
        SurfaceInfo info{
        .native_handle = (__bridge void*)layer,
        .width         = px_w,
        .height        = px_h,
        .content_scale = static_cast<f32>(scale > 1.0f ? scale : 1.0f),
    };
    if (g_backend) g_backend->resize(info);

    // game code works in logical points (consistent with touch coordinates)
    if (g_callbacks.resize) {
        g_callbacks.resize(g_callbacks.user_data,
            static_cast<u32>(size.width),
            static_cast<u32>(size.height));
    }
}
@end

// ─── MetalView ────────────────────────────────────────────────────────────────
@interface MetalView : UIView {
    CFTimeInterval _lastFrameTime;

    // Slot-based touch ID table — avoids hash collisions from the old touch.hash approach.
    // Maps UITouch* (weak, pointer identity) → NSNumber slot index 0..4.
    // iOS supports max 5 simultaneous touches; slot is recycled on touchesEnded/Cancelled.
    NSMapTable<UITouch*, NSNumber*>* _touchIDMap;
    bool _initialized;
}
@property (strong, nonatomic) CADisplayLink  *displayLink;
@property (strong, nonatomic) CHHapticEngine *hapticEngine;
@end

@implementation MetalView

+ (Class)layerClass { return [CAMetalLayer class]; }

// ─── Init ─────────────────────────────────────────────────────────────────────
- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    self.multipleTouchEnabled = YES;
    _lastFrameTime = CACurrentMediaTime();
    _initialized = false;

    // Slot table: weak pointer keys (no retain on UITouch), strong value NSNumbers
    _touchIDMap = [NSMapTable mapTableWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
                                       valueOptions:NSPointerFunctionsObjectPersonality];

    // Capture content scale now; updated again on orientation change
    g_content_scale = static_cast<f32>([UIScreen mainScreen].scale);

    [self setupHaptics];

    // ── CADisplayLink ────────────────────────────────────────────────────────
    // Target the device's native refresh rate (60 on older, 120 on ProMotion).
    // preferred = max = native rate so the system never throttles us unnecessarily.
    self.displayLink = [CADisplayLink displayLinkWithTarget:self
                                                   selector:@selector(_renderFrame:)];
    f32 maxFPS = static_cast<f32>([UIScreen mainScreen].maximumFramesPerSecond);
    self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(30.0f, maxFPS, maxFPS);
    [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

    return self;
}

- (void)didMoveToWindow {
    [super didMoveToWindow];
    
    if (self.window) {
        // ONE-TIME initialization
        if (!_initialized) {
            _initialized = true;
            
            // ✅ NOW SAFE: window exists, can access screen.scale
            CAMetalLayer* layer = (CAMetalLayer*)self.layer;
            
            // Initialize all engine systems
            g_backend = new MetalBackend();
            g_backend->init((__bridge void*)layer);
            
            g_input_queue = new InputEventQueue();
            g_input_state = new InputState();
            g_input_state->init();
            
            g_audio_system.init();
            
            PoolInit();
            JobSystemInit(0);
            g_event_bus.clear(); // ensure empty at startup
            g_event_bus_ptr = &g_event_bus; // global singleton for user code
            
            // Setup VFS
            NSString* bundlePath = [[NSBundle mainBundle] resourcePath];
            NSArray* docPaths = NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES);
            g_vfs.init([bundlePath UTF8String],
                      docPaths.count > 0 ? [docPaths[0] UTF8String] : nullptr);
            
            // User init callback
            if (g_callbacks.init) {
                g_callbacks.init(g_callbacks.user_data);
            }
        }
        
        // Update viewport (called every time view enters window)
        [self updateViewportDimensions];
        
        // Start rendering
        self.displayLink.paused = NO;
    } else {
        // Cleanup if removed
        self.displayLink.paused = YES;
    }
}

// ─── NEW: Separated viewport update logic ──────────────────────────────────────
- (void)updateViewportDimensions {
    CGFloat scale = self.window ? self.window.screen.scale : [[UIScreen mainScreen] scale];
    g_content_scale = static_cast<f32>(scale);
    
    CGSize size = self.bounds.size;
    auto px_w = static_cast<u32>(size.width * scale);
    auto px_h = static_cast<u32>(size.height * scale);
    
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    SurfaceInfo info{
        .native_handle = (__bridge void*)layer,
        .width         = px_w,
        .height        = px_h,
        .content_scale = g_content_scale > 1.0f ? g_content_scale : 1.0f,
    };
    
    if (g_backend) {
        g_backend->resize(info);
    }
    
    if (g_callbacks.resize) {
        g_callbacks.resize(g_callbacks.user_data,
            static_cast<u32>(size.width),
            static_cast<u32>(size.height));
    }
}

- (void)dealloc {
    [self.displayLink invalidate];
    // Do NOT call [super dealloc] under ARC
}

// ─── Pause / Resume helpers (called from AppDelegate) ─────────────────────────
- (void)pauseRendering {
    self.displayLink.paused = YES;
}

- (void)resumeRendering {
    // Resync timing so first dt after resume is not huge
    _lastFrameTime = CACurrentMediaTime();
    self.displayLink.paused = NO;
}

// ─── Render loop ─────────────────────────────────────────────────────────────
- (void)_renderFrame:(CADisplayLink*)sender {
    @autoreleasepool {
        CFTimeInterval now = CACurrentMediaTime();
        auto dt = static_cast<f32>(now - _lastFrameTime);
        _lastFrameTime = now;
        if (dt > 0.1f) dt = 0.1f;

        if (g_input_state && g_input_queue) {
            g_input_state->process(*g_input_queue, dt);
        }

        g_audio_system.update(dt);

        if (!g_backend) return;

        auto begin_res = g_backend->begin_frame();
        if (!begin_res) {
            MM_ERROR("_renderFrame: begin_frame failed: %d", (int)begin_res.error());
            return;
        }

        if (g_callbacks.frame) {
            g_callbacks.frame(g_callbacks.user_data, dt, *g_input_state);
        }

        auto end_res = g_backend->end_frame();
        if (!end_res) {
            MM_ERROR("_renderFrame: end_frame failed: %d", (int)end_res.error());
        }
    }
}

// ─── Touch ID helpers ────────────────────────────────────────────────────────
// Returns a stable slot index (0..4) for a UITouch pointer.
// Allocates a new slot on first call, reuses existing slot on subsequent calls.
- (u8)_acquireTouchID:(UITouch*)touch {
    NSNumber* existing = [_touchIDMap objectForKey:touch];
    if (existing) return (u8)[existing unsignedIntValue];

    // Find first free slot
    bool used[5] = {};
    for (NSNumber* val in _touchIDMap.objectEnumerator) {
        auto idx = (u8)[val unsignedIntValue];
        if (idx < 5) used[idx] = true;
    }
    for (auto i = 0; i < 5; ++i) {
        if (!used[i]) {
            [_touchIDMap setObject:@(i) forKey:touch];
            return i;
        }
    }
    return 0xFF; // all 5 slots occupied — should never happen
}

- (void)_releaseTouchID:(UITouch*)touch {
    [_touchIDMap removeObjectForKey:touch];
}

// ─── Touch event forwarding ───────────────────────────────────────────────────
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        CGPoint  pt  = [touch locationInView:self];
        u8  tid = [self _acquireTouchID:touch];
        g_input_queue->push(InputEvent::make_touch_down(tid, (f32)pt.x, (f32)pt.y));
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        u8 tid = [self _acquireTouchID:touch];
        if (tid == 0xFF) continue;
        CGPoint pt = [touch locationInView:self];
        g_input_queue->push(InputEvent::make_touch_move(tid, (f32)pt.x, (f32)pt.y));
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        u8 tid = [self _acquireTouchID:touch];
        if (tid == 0xFF) continue;
        g_input_queue->push(InputEvent::make_touch_up(tid));
        [self _releaseTouchID:touch];
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        u8 tid = [self _acquireTouchID:touch];
        if (tid == 0xFF) continue;
        g_input_queue->push(InputEvent::make_touch_cancel(tid));
        [self _releaseTouchID:touch];
    }
}

// ─── Haptic engine setup ────────────────────────────────────────────
- (void)setupHaptics {
    if (@available(iOS 13.0, *)) {
        @try {
            NSError* hapticError = nil;
            CHHapticEngine* engine = [[CHHapticEngine alloc] initAndReturnError:&hapticError];
            
            if (hapticError) {
                MM_ERROR("CHHapticEngine init error: %s", 
                        [hapticError.localizedDescription UTF8String]);
                return;
            }
            
            NSError* startError = nil;
            if (![engine startAndReturnError:&startError]) {
                MM_ERROR("CHHapticEngine start error: %s",
                        [startError.localizedDescription UTF8String]);
                return;
            }
            
            self.hapticEngine = engine;
            
            // Setup handlers
            __weak MetalView* weakSelf = self;
            
            engine.resetHandler = ^{
                NSError* restartError = nil;
                [weakSelf.hapticEngine startAndReturnError:&restartError];
            };
            
            engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
                (void)reason;
                NSError* restartError = nil;
                [weakSelf.hapticEngine startAndReturnError:&restartError];
            };
        }
        @catch (NSException* ex) {
            MM_ERROR("CHHapticEngine exception: %s", [ex.reason UTF8String]);
        }
    }
}

// ─── Haptic feedback ─────────────────────────────────────────────────────────
// The player is captured in a __block variable and released after a short delay
// so it stays alive until playback completes (avoids early-dealloc race).
- (void)triggerHaptic:(f32)intensity {
    if (!self.hapticEngine) return;
    
    // Clamp intensity
    intensity = fmax(0.0f, fmin(1.0f, intensity));
    
    CHHapticEventParameter* param = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticIntensity
                      value:intensity];
    
    CHHapticEvent* ev = [[CHHapticEvent alloc]
        initWithEventType:CHHapticEventTypeHapticTransient
               parameters:@[param]
             relativeTime:0];
    
    NSError* err = nil;
    CHHapticPattern* pattern = [[CHHapticPattern alloc]
        initWithEvents:@[ev] parameters:@[] error:&err];
    
    if (!pattern) {
        MM_ERROR("CHHapticPattern init failed: %s",
                [err.localizedDescription UTF8String]);
        return;
    }

    NSError* createErr = nil;
    id<CHHapticPatternPlayer> player =
        [self.hapticEngine createPlayerWithPattern:pattern error:&createErr];
    
    if (!player) {
        MM_ERROR("CHHapticPatternPlayer create failed: %s",
                [createErr.localizedDescription UTF8String]);
        return;
    }

    NSError* playErr = nil;
    [player startAtTime:0 error:&playErr];
    
    if (playErr) {
        MM_ERROR("CHHapticPatternPlayer start failed: %s",
                [playErr.localizedDescription UTF8String]);
    }

    // Keep player alive until playback completes
    __block id<CHHapticPatternPlayer> blockPlayer = player;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (i64)(0.5 * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
            blockPlayer = nil;  // Release when block exits
        });
}

@end

// ─── AppDelegate ──────────────────────────────────────────────────────────────
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow  *window;
@property (weak,   nonatomic) MetalView *metalView; // weak — owned by view hierarchy
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {

    CGRect  bounds;
    CGFloat scale;
    if (@available(iOS 16.0, *)) {
        UIWindowScene* scene =
            (UIWindowScene*)UIApplication.sharedApplication.connectedScenes.anyObject;
        bounds = scene.screen.bounds;
        scale  = scene.screen.scale;
    } else {
        bounds = [UIScreen mainScreen].bounds;
        scale  = [UIScreen mainScreen].scale;
    }

    // Set global scale early — MetalView init will read it
    g_content_scale = static_cast<f32>(scale);

    self.window = [[UIWindow alloc] initWithFrame:bounds];
    self.window.backgroundColor = [UIColor blackColor];

    MetalView* view = [[MetalView alloc] initWithFrame:bounds];
    view.contentScaleFactor = scale;
    self.metalView = view;

    GameViewController* vc = [[GameViewController alloc] init];
    vc.metalView = view;
    self.window.rootViewController = vc;
    self.window.rootViewController.view = view;
    [self.window makeKeyAndVisible];

    return YES;
}

// ── Pause/resume rendering on foreground transitions ──────────────────────────
// applicationWillResignActive fires on: incoming call, Home button, Control
// Center / Notification Center pull-down, f64 side-button (Siri), or a
// system UIAlert covering the screen.
- (void)applicationWillResignActive:(UIApplication*)application {
    // Clear all touch state — fingers may not send touchesEnded if interrupted
    if (g_input_state) g_input_state->touch.reset();
    if (g_input_queue) g_input_queue->reset();
}

- (void)applicationDidEnterBackground:(UIApplication*)application {
    // Pause the display link so no frames are rendered while backgrounded.
    // This is the reliable place to do it; applicationWillResignActive fires
    // for interruptions (phone calls) that don't actually background the app.
    [self.metalView pauseRendering];
}

- (void)applicationWillEnterForeground:(UIApplication*)application {
    // Nothing needed here — resumeRendering is called in applicationDidBecomeActive
}

- (void)applicationDidBecomeActive:(UIApplication*)application {
    [self.metalView resumeRendering];
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication*)application {
}

- (void)applicationWillTerminate:(UIApplication*)application {
    // NOTE: iOS does NOT guarantee this is called (process can be killed directly).
    // Critical save logic must happen in applicationDidEnterBackground above.
    if (g_callbacks.cleanup) g_callbacks.cleanup(g_callbacks.user_data);
    g_audio_system.shutdown();
    delete g_backend;     g_backend     = nullptr;
    delete g_input_queue; g_input_queue = nullptr;
    delete g_input_state; g_input_state = nullptr;
    g_event_bus.clear();
    JobSystemShutdown();
    PoolShutdown();
}

@end

// ─── Entry point ──────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    @autoreleasepool {
        g_callbacks = markmos_main(argc, argv);
        return UIApplicationMain(argc, argv, nil,
                                 NSStringFromClass([AppDelegate class]));
    }
}

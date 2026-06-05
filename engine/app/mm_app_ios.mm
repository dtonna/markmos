// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// iOS App Entry — Sokol-style: calls user-defined markmos_main() for callbacks
// Platform handles: Metal backend, input queue, audio, CADisplayLink lifecycle

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
MetalBackend*    g_backend       = nullptr;
InputEventQueue* g_input_queue   = nullptr;
InputState*      g_input_state   = nullptr;
float            g_content_scale = 1.0f;

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
    g_content_scale = static_cast<float>(scale);

    // backend expects pixels
    uint32_t px_w = static_cast<uint32_t>(size.width  * scale);
    uint32_t px_h = static_cast<uint32_t>(size.height * scale);
    if (g_backend) g_backend->resize(px_w, px_h);

    // game code works in logical points (consistent with touch coordinates)
    if (g_callbacks.resize) {
        g_callbacks.resize(g_callbacks.user_data,
            static_cast<uint32_t>(size.width),
            static_cast<uint32_t>(size.height));
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

    // Slot table: weak pointer keys (no retain on UITouch), strong value NSNumbers
    _touchIDMap = [NSMapTable mapTableWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
                                       valueOptions:NSPointerFunctionsObjectPersonality];

    // Capture content scale now; updated again on orientation change
    g_content_scale = static_cast<float>([UIScreen mainScreen].scale);

    // ── Haptic engine setup ──────────────────────────────────────────────────
    // CHHapticEngine available iOS 13+, but initAndStartWithError: is not
    // recognized by the iOS 26.5 SDK compiler. Use objc_msgSend to bypass
    // compile-time availability checks.
    Class hapticClass = NSClassFromString(@"CHHapticEngine");
    if (hapticClass) {
        SEL initSel = NSSelectorFromString(@"initAndStartWithError:");
        if (initSel && [hapticClass instancesRespondToSelector:initSel]) {
            NSError* hapticError = nil;
            id (*SendMsg)(id, SEL, NSError**) = (id (*)(id, SEL, NSError**))objc_msgSend;
            id engine = SendMsg([hapticClass alloc], initSel, &hapticError);
            if (engine) {
                self.hapticEngine = engine;

                // Restart engine automatically after interruptions (phone calls, etc.)
                __weak MetalView* weakSelf = self;
                self.hapticEngine.resetHandler = ^{
                    NSError* restartError = nil;
                    [weakSelf.hapticEngine startAndReturnError:&restartError];
                };
                self.hapticEngine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
                    (void)reason;
                    NSError* restartError = nil;
                    [weakSelf.hapticEngine startAndReturnError:&restartError];
                };
            }
        }
    }

    // ── Engine systems ───────────────────────────────────────────────────────
    g_input_queue = new InputEventQueue();
    g_input_state = new InputState();
    g_input_state->init();

    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    g_backend = new MetalBackend();
    g_backend->init((__bridge void*)layer);

    g_audio_system.init();

    // ── Pool allocator, job system ───────────────────────────────────────────
    PoolInit();
    JobSystemInit(0);

    // ── VFS ──────────────────────────────────────────────────────────────────
    NSString* bundlePath = [[NSBundle mainBundle] resourcePath];
    NSArray*  docPaths   = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    g_vfs.init([bundlePath UTF8String],
               docPaths.count > 0 ? [docPaths[0] UTF8String] : nullptr);

    // ── User init callback ───────────────────────────────────────────────────
    // renderer.init(..., 0, 0) — correct dims are set via resize right after
    if (g_callbacks.init) {
        g_callbacks.init(g_callbacks.user_data);
    }

    // ── Resize after init so game_resize can access g_game ─────────────────────
    CGFloat nativeScale = [[UIScreen mainScreen] scale];
    g_content_scale = static_cast<float>(nativeScale);
    CGFloat ptW = self.bounds.size.width;
    CGFloat ptH = self.bounds.size.height;
    if (g_backend) {
        g_backend->resize(static_cast<uint32_t>(ptW * nativeScale),
                          static_cast<uint32_t>(ptH * nativeScale));
    }
    if (g_callbacks.resize) {
        g_callbacks.resize(g_callbacks.user_data,
                           static_cast<uint32_t>(ptW),
                           static_cast<uint32_t>(ptH));
    }

    // ── CADisplayLink ────────────────────────────────────────────────────────
    // Target the device's native refresh rate (60 on older, 120 on ProMotion).
    // preferred = max = native rate so the system never throttles us unnecessarily.
    self.displayLink = [CADisplayLink displayLinkWithTarget:self
                                                   selector:@selector(_renderFrame:)];
    float maxFPS = static_cast<float>([UIScreen mainScreen].maximumFramesPerSecond);
    self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(30.0f, maxFPS, maxFPS);
    [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

    return self;
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
    // Use CACurrentMediaTime for elapsed time (same approach as mm_app_mac.mm).
    // sender.targetTimestamp − sender.timestamp is the frame budget, NOT elapsed time.
    CFTimeInterval now = CACurrentMediaTime();
    float dt = static_cast<float>(now - _lastFrameTime);
    _lastFrameTime = now;
    if (dt > 0.1f) dt = 0.1f; // clamp to prevent spiral-of-death after pause

    if (g_input_state && g_input_queue) {
        g_input_state->process(*g_input_queue, dt);
    }

    g_audio_system.update(dt);

    if (!g_backend) return;

    g_backend->begin_frame();

    if (g_callbacks.frame) {
        g_callbacks.frame(g_callbacks.user_data, dt, *g_input_state);
    }

    g_backend->end_frame();
}

// ─── Touch ID helpers ────────────────────────────────────────────────────────
// Returns a stable slot index (0..4) for a UITouch pointer.
// Allocates a new slot on first call, reuses existing slot on subsequent calls.
- (uint8_t)_acquireTouchID:(UITouch*)touch {
    NSNumber* existing = [_touchIDMap objectForKey:touch];
    if (existing) return (uint8_t)[existing unsignedIntValue];

    // Find first free slot
    bool used[5] = {};
    for (NSNumber* val in _touchIDMap.objectEnumerator) {
        uint8_t idx = (uint8_t)[val unsignedIntValue];
        if (idx < 5) used[idx] = true;
    }
    for (uint8_t i = 0; i < 5; ++i) {
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
        uint8_t  tid = [self _acquireTouchID:touch];
        g_input_queue->push(InputEvent::make_touch_down(tid, (float)pt.x, (float)pt.y));
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        uint8_t tid = [self _acquireTouchID:touch];
        if (tid == 0xFF) continue;
        CGPoint pt = [touch locationInView:self];
        g_input_queue->push(InputEvent::make_touch_move(tid, (float)pt.x, (float)pt.y));
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        uint8_t tid = [self _acquireTouchID:touch];
        if (tid == 0xFF) continue;
        g_input_queue->push(InputEvent::make_touch_up(tid));
        [self _releaseTouchID:touch];
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        uint8_t tid = [self _acquireTouchID:touch];
        if (tid == 0xFF) continue;
        g_input_queue->push(InputEvent::make_touch_cancel(tid));
        [self _releaseTouchID:touch];
    }
}

// ─── Haptic feedback ─────────────────────────────────────────────────────────
// The player is captured in a __block variable and released after a short delay
// so it stays alive until playback completes (avoids early-dealloc race).
- (void)triggerHaptic:(float)intensity {
    if (!self.hapticEngine) return;
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
    if (!pattern) return;

    __block id<CHHapticPatternPlayer> player =
        [self.hapticEngine createPlayerWithPattern:pattern error:&err];
    if (!player) return;

    [player startAtTime:0 error:&err];

    // Release the player retain after the transient event completes (~0.3 s)
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{ player = nil; });
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
    g_content_scale = static_cast<float>(scale);

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

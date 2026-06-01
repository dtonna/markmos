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

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreHaptics/CoreHaptics.h>

// Engine globals (accessible to user code via extern)
MetalBackend*    g_backend       = nullptr;
InputEventQueue* g_input_queue   = nullptr;
InputState*      g_input_state   = nullptr;

// App callbacks (set by main() from user's markmos_main())
static AppCallbacks g_callbacks{};

@class MetalView;

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@property (strong, nonatomic) CADisplayLink *displayLink;
@end

// Custom view controller for rotation handling
@interface GameViewController : UIViewController
@property (strong, nonatomic) MetalView *metalView;
@end

@implementation GameViewController
- (void)viewWillTransitionToSize:(CGSize)size
       withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator {
    [super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];
    CGFloat scale = self.view.window.screen.scale;
    uint32_t w = static_cast<uint32_t>(size.width * scale);
    uint32_t h = static_cast<uint32_t>(size.height * scale);
    if (g_backend) {
        g_backend->resize(w, h);
    }
    if (g_callbacks.resize) {
        g_callbacks.resize(g_callbacks.user_data,
            static_cast<uint32_t>(size.width),
            static_cast<uint32_t>(size.height));
    }
}
@end

@interface MetalView : UIView
@property (strong, nonatomic) CADisplayLink *displayLink;
@property (strong, nonatomic) CHHapticEngine *hapticEngine;
@end

@implementation MetalView
+ (Class)layerClass { return [CAMetalLayer class]; }

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.multipleTouchEnabled = YES;

        NSError* error = nil;
        self.hapticEngine = [[CHHapticEngine alloc] initAndStartWithError:&error];

        // Init engine systems
        g_input_queue = new InputEventQueue();
        g_input_state = new InputState();
        g_input_state->init();
        CAMetalLayer* layer = (CAMetalLayer*)self.layer;
        g_backend = new MetalBackend();
        g_backend->init((__bridge void*)layer);
        g_audio_system.init();

        // Init VFS with bundle resource path and documents directory
        NSString* bundlePath = [[NSBundle mainBundle] resourcePath];
        NSArray* docPaths = NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES);
        g_vfs.init([bundlePath UTF8String],
                   docPaths.count > 0 ? [docPaths[0] UTF8String] : nullptr);

        // User init callback
        if (g_callbacks.init) {
            g_callbacks.init(g_callbacks.user_data);
        }

        // Start display link
        self.displayLink = [CADisplayLink displayLinkWithTarget:self
                                                       selector:@selector(frame:)];
        self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 120);
        [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop]
                               forMode:NSRunLoopCommonModes];
    }
    return self;
}

- (void)dealloc {
    [self.displayLink invalidate];
}

- (void)frame:(CADisplayLink*)sender {
    float dt = sender.targetTimestamp - sender.timestamp;

    // Process input
    if (g_input_state && g_input_queue) {
        g_input_state->process(*g_input_queue, dt);
    }

    g_audio_system.update(dt);

    if (!g_backend) return;

    g_backend->begin_frame();
    PassDesc pass{};
    pass.clear_color[0] = 0.1f; pass.clear_color[1] = 0.1f;
    pass.clear_color[2] = 0.2f; pass.clear_color[3] = 1.0f;
    pass.color_load = LoadOp::Clear;
    g_backend->begin_pass(pass);

    // User frame callback — game logic + draw calls (inside render pass)
    if (g_callbacks.frame) {
        g_callbacks.frame(g_callbacks.user_data, dt, *g_input_state);
    }

    g_backend->end_pass();
    g_backend->end_frame();
}

// Touch → InputEventQueue
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        CGPoint pt = [touch locationInView:self];
        uint8_t tid = (uint8_t)([touch hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_down(tid, pt.x, pt.y));
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        CGPoint pt = [touch locationInView:self];
        uint8_t tid = (uint8_t)([touch hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_move(tid, pt.x, pt.y));
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        uint8_t tid = (uint8_t)([touch hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_up(tid));
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!g_input_queue) return;
    for (UITouch* touch in touches) {
        uint8_t tid = (uint8_t)([touch hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_cancel(tid));
    }
}

// Haptic feedback
- (void)triggerHaptic:(float)intensity {
    if (!self.hapticEngine) return;

    CHHapticEventParameter* param = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticIntensity
                     value:intensity];
    CHHapticEvent* event = [[CHHapticEvent alloc]
        initWithEventType:CHHapticEventTypeHapticTransient
                parameters:@[param]
                  relativeTime:0];
    CHHapticPattern* pattern = [[CHHapticPattern alloc]
        initWithEvents:@[event] parameters:[[NSArray alloc] init] error:nil];
    if (!pattern) return;
    id<CHHapticPatternPlayer> player = [self.hapticEngine
        createPlayerWithPattern:pattern error:nil];
    [player startAtTime:0 error:nil];
}
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    CGRect bounds;
    CGFloat scale;
    if (@available(iOS 16.0, *)) {
        UIWindowScene* scene = (UIWindowScene*)UIApplication.sharedApplication.connectedScenes.anyObject;
        bounds = scene.screen.bounds;
        scale = scene.screen.scale;
    } else {
        bounds = [UIScreen mainScreen].bounds;
        scale = [UIScreen mainScreen].scale;
    }
    self.window = [[UIWindow alloc] initWithFrame:bounds];
    self.window.backgroundColor = [UIColor blackColor];

    MetalView* view = [[MetalView alloc] initWithFrame:bounds];
    view.contentScaleFactor = scale;
    GameViewController* vc = [[GameViewController alloc] init];
    vc.metalView = view;
    self.window.rootViewController = vc;
    self.window.rootViewController.view = view;
    [self.window makeKeyAndVisible];

    return YES;
}

- (void)applicationWillResignActive:(UIApplication*)application {
    if (g_input_state) {
        g_input_state->touch.reset();
        g_input_queue->reset();
    }
}

- (void)applicationDidBecomeActive:(UIApplication*)application {}

- (void)applicationDidReceiveMemoryWarning:(UIApplication*)application {}

- (void)applicationWillTerminate:(UIApplication*)application {
    if (g_callbacks.cleanup) g_callbacks.cleanup(g_callbacks.user_data);
    g_audio_system.shutdown();
    delete g_backend;
    delete g_input_queue;
    delete g_input_state;
    g_backend = nullptr;
    g_input_queue = nullptr;
    g_input_state = nullptr;
}

@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        g_callbacks = markmos_main(argc, argv);
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// macOS App Entry — Sokol-style: calls user-defined markmos_main() for callbacks
// Platform handles: Metal backend, input queue, audio, display link lifecycle
// User handles: game init/update/render via AppCallbacks

#include "mm_app.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include "../rhi/mm_metal_backend.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../audio/mm_audio_system.hpp"
#include "../input/mm_input_event.hpp"
#include "../input/mm_input_state.hpp"
#include "../core/mm_vfs.hpp"

#include "../core/mm_log.hpp"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#include <cstdlib>
#include <unistd.h>

// Engine globals (accessible to user code via extern)
MetalBackend*    g_backend       = nullptr;
InputEventQueue* g_input_queue   = nullptr;
InputState*      g_input_state   = nullptr;
float            g_content_scale = 1.0f;

// App callbacks (set by main() from user's markmos_main())
static AppCallbacks g_callbacks{};

// macOS NSEvent keyCode → engine KeyCode
static KeyCode keycode_from_ns(uint16_t kc) noexcept {
    switch (kc) {
        case 0x00: return KeyCode::A; case 0x0B: return KeyCode::B;
        case 0x08: return KeyCode::C; case 0x02: return KeyCode::D;
        case 0x0E: return KeyCode::E; case 0x03: return KeyCode::F;
        case 0x05: return KeyCode::G; case 0x04: return KeyCode::H;
        case 0x22: return KeyCode::I; case 0x26: return KeyCode::J;
        case 0x28: return KeyCode::K; case 0x25: return KeyCode::L;
        case 0x2E: return KeyCode::M; case 0x2D: return KeyCode::N;
        case 0x1F: return KeyCode::O; case 0x23: return KeyCode::P;
        case 0x0C: return KeyCode::Q; case 0x0F: return KeyCode::R;
        case 0x01: return KeyCode::S; case 0x11: return KeyCode::T;
        case 0x20: return KeyCode::U; case 0x09: return KeyCode::V;
        case 0x0D: return KeyCode::W; case 0x07: return KeyCode::X;
        case 0x10: return KeyCode::Y; case 0x06: return KeyCode::Z;
        case 0x1D: return KeyCode::D0; case 0x12: return KeyCode::D1;
        case 0x13: return KeyCode::D2; case 0x14: return KeyCode::D3;
        case 0x15: return KeyCode::D4; case 0x17: return KeyCode::D5;
        case 0x16: return KeyCode::D6; case 0x1A: return KeyCode::D7;
        case 0x18: return KeyCode::D8; case 0x19: return KeyCode::D9;
        case 0x7B: return KeyCode::Left;  case 0x7C: return KeyCode::Right;
        case 0x7E: return KeyCode::Up;    case 0x7D: return KeyCode::Down;
        case 0x38: return KeyCode::Shift; case 0x3B: return KeyCode::Ctrl;
        case 0x3A: return KeyCode::Alt;
        case 0x31: return KeyCode::Space;   case 0x24: return KeyCode::Enter;
        case 0x35: return KeyCode::Escape;  case 0x33: return KeyCode::Backspace;
        case 0x30: return KeyCode::Tab;
        default:   return KeyCode::Unknown;
    }
}

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (strong) NSWindow* window;
@end
//
//static CVReturn displayCallback(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*,
//                                 CVOptionFlags, CVOptionFlags*, void*);

@interface MetalView : NSView {
    CADisplayLink* _displayLink;
    CFTimeInterval _lastFrameTime;
}
//- (void)tick;
- (void)renderLoopStep:(CADisplayLink *)sender;
@end

@implementation MetalView

- (CALayer *)makeBackingLayer {
    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.device = MTLCreateSystemDefaultDevice();
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    metalLayer.maximumDrawableCount = 3;
    return metalLayer;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        self.allowedTouchTypes = NSTouchTypeMaskDirect;
        _lastFrameTime = CACurrentMediaTime();

        // 1. Setup system input boundaries
        NSTrackingArea* tracking = [[NSTrackingArea alloc]
            initWithRect:self.bounds
                 options:NSTrackingMouseMoved | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                   owner:self
                userInfo:nil];
        [self addTrackingArea:tracking];

        // 2. Initialize graphics systems
        CAMetalLayer* layer = (CAMetalLayer*)self.layer;
        g_backend = new MetalBackend();
        if (!g_backend->init((__bridge void*)layer)) {
            return nil;
        }

        g_input_queue = new InputEventQueue();
        g_input_state = new InputState();
        g_input_state->init();
        g_audio_system.init();

        // 3. Setup VFS and environment paths
        NSString* bundlePath = [[NSBundle mainBundle] resourcePath];
        NSArray* docPaths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        g_vfs.init([bundlePath UTF8String], docPaths.count > 0 ? [docPaths[0] UTF8String] : nullptr);
        chdir([bundlePath UTF8String]);
        
//        char cwd[512];
//        getcwd(cwd, sizeof(cwd));
//        NSLog(@"[app] cwd = %s", cwd);
//        NSLog(@"[app] bundlePath = %@", bundlePath);
//        
//        NSArray* contents = [[NSFileManager defaultManager]
//            contentsOfDirectoryAtPath:bundlePath error:nil];
//        NSLog(@"[app] bundle contents = %@", contents);
        
        if (g_callbacks.init) {
            g_callbacks.init(g_callbacks.user_data);
        }
    }
    return self;
}

// 2. Fixed Retina Scaling Bug: Track when window context becomes valid
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    
    if (self.window) {
        [self updateViewportDimensions];
        
        /// FIX 2: Modern macOS 15+ NSDisplayLink creation
        _displayLink = [self displayLinkWithTarget:self selector:@selector(renderLoopStep:)];
        // 2. FIX: You MUST unpause the link explicitly on macOS to kick off the frame loop!
                
        //_displayLink. = NO;
                
        // 3. Optional but highly recommended: Keep ticking during live window resizing/menu navigation
        [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
                
        // FIX 3: Automatically capture focus so keyboard events register immediately without clicking
        [self.window makeFirstResponder:self];
    } else {
        // Safe tear down if view gets disconnected
        [_displayLink invalidate];
        _displayLink = nil;
    }
}

- (void)renderLoopStep:(CADisplayLink *)sender {
    @autoreleasepool {
        CFTimeInterval currentTime = CACurrentMediaTime();
        float dt = static_cast<float>(currentTime - _lastFrameTime);
        _lastFrameTime = currentTime;
        
        if (dt > 0.1f) dt = 0.1f; // Cap frame hiccups
        
        // Process input
        if (g_input_state && g_input_queue) {
            g_input_state->process(*g_input_queue, dt);
        }
        
        g_audio_system.update(dt);
        
        if (!g_backend) return;
        if (!g_backend->begin_frame()) {
            return;
        }
        if (g_callbacks.frame) {
            g_callbacks.frame(g_callbacks.user_data, dt, *g_input_state);
        }
        g_backend->end_frame();
    }
}

- (void)updateViewportDimensions {
    NSSize size = self.bounds.size;
    CGFloat scale = self.window ? self.window.backingScaleFactor : 1.0f;
    g_content_scale = static_cast<float>(scale);
    
    uint32_t w = static_cast<uint32_t>(size.width * scale);
    uint32_t h = static_cast<uint32_t>(size.height * scale);
    
    // Explicitly update matching backing store dimensions
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    layer.drawableSize = CGSizeMake(w, h);
    layer.contentsScale = scale;

    if (g_backend) {
        g_backend->resize(w, h);
    }
    if (g_callbacks.resize) {
        g_callbacks.resize(g_callbacks.user_data,
            static_cast<uint32_t>(size.width),
            static_cast<uint32_t>(size.height));
    }
}


- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    [self updateViewportDimensions];
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    [self updateViewportDimensions];
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc {
    [super dealloc];
}

// 4. Safe Loop Invalidation: Clean up engine references
- (void)removeFromSuperview {
    // 6. Modern invalidation pass
    if (_displayLink) {
        [_displayLink invalidate];
        _displayLink = nil;
    }
    
    if (g_callbacks.cleanup) g_callbacks.cleanup(g_callbacks.user_data);
        
    g_audio_system.shutdown();
    
//    if (g_backend) {
//        g_backend->shutdown();
//        delete g_backend;
//        
//    }
    delete g_backend;       g_backend = nullptr;
    delete g_input_queue;   g_input_queue = nullptr;
    delete g_input_state;   g_input_state = nullptr;
    
    [super removeFromSuperview];
}
//
//static CVReturn displayCallback(CVDisplayLinkRef displayLink,
//                                const CVTimeStamp* now,
//                                const CVTimeStamp* outputTime,
//                                CVOptionFlags flagsIn,
//                                CVOptionFlags* flagsOut,
//                                void* context) {
//    (void)displayLink; (void)now; (void)outputTime; (void)flagsIn; (void)flagsOut;
//    
//    // 5. Thread Safety Fix: Leap safely back to AppKit main thread loop
//    dispatch_async(dispatch_get_main_queue(), ^{
//        @autoreleasepool {
//            [(__bridge MetalView*)context tick];
//        }
//    });
//    return kCVReturnSuccess;
//}
//
//- (void)tick {
//    // 6. High-Precision Frame Timing (No longer hardcoded 1/60s)
//    CFTimeInterval currentTime = CACurrentMediaTime();
//    float dt = static_cast<float>(currentTime - _lastFrameTime);
//    _lastFrameTime = currentTime;
//    
//    // Smooth over extreme outliers (e.g. system freezes or window drags)
//    if (dt > 0.1f) dt = 0.1f;
//    
//    // Process internal input mutations
//    if (g_input_state && g_input_queue) {
//        g_input_state->process(*g_input_queue, dt);
//    }
//    
//    MM_LOG("tick update audio");
//    g_audio_system.update(dt);
//    
//    if (!g_backend) return;
//    
//    MM_LOG("tick begin frame");
//    auto begin_ret = g_backend->begin_frame();
//    if (!begin_ret) {
//        return;
//    }
//    
//    MM_LOG("tick callbacks frame");
//    // 7. Fixed Truncation: Clean execution flow and terminal frame presentation passes
//    if (g_callbacks.frame) {
//        g_callbacks.frame(g_callbacks.user_data, dt, *g_input_state);
//    }
//    
//    MM_LOG("tick end frame");
//    g_backend->end_frame(); // Signal your RHI to swap buffers and present command encoders
//}

// Mouse → InputEventQueue (finger 0)
// macOS origin is bottom-left; engine origin is top-left → flip Y
- (NSPoint)flipY:(NSPoint)pt {
    pt.y = self.bounds.size.height - pt.y;
    return pt;
}

- (void)mouseDown:(NSEvent*)event {
    if (!g_input_queue) return;
    NSPoint pt = [self flipY:[self convertPoint:event.locationInWindow fromView:nil]];
    g_input_queue->push(InputEvent::make_touch_down(0, (float)pt.x, (float)pt.y));
}

- (void)mouseDragged:(NSEvent*)event {
    if (!g_input_queue) return;
    NSPoint pt = [self flipY:[self convertPoint:event.locationInWindow fromView:nil]];
    g_input_queue->push(InputEvent::make_touch_move(0, (float)pt.x, (float)pt.y));
}

- (void)mouseUp:(NSEvent*)event {
    if (!g_input_queue) return;
    g_input_queue->push(InputEvent::make_touch_up(0));
}

- (void)mouseMoved:(NSEvent*)event {
    if (!g_input_queue) return;
    NSPoint pt = [self flipY:[self convertPoint:event.locationInWindow fromView:nil]];
    g_input_queue->push(InputEvent::make_mouse_move((float)pt.x, (float)pt.y));
}

- (void)rightMouseDown:(NSEvent*)event {}

- (void)scrollWheel:(NSEvent*)event {
    if (!g_input_queue) return;
    g_input_queue->push(InputEvent::make_mouse_scroll((float)event.scrollingDeltaX,
                                                       (float)event.scrollingDeltaY));
}

// Keyboard → InputEventQueue
- (void)keyDown:(NSEvent*)event {
    if (!g_input_queue) return;
    KeyCode kc = keycode_from_ns(event.keyCode);
    if (kc != KeyCode::Unknown) {
        g_input_queue->push(InputEvent::make_key_down(kc));
    }
    NSString* chars = event.characters;
    if (chars.length > 0) {
        unichar c = [chars characterAtIndex:0];
        if (c >= 32 && c != 127) { // printable, not Delete
            g_input_queue->push(InputEvent::make_text_input((char)c));
        }
    }
}

- (void)keyUp:(NSEvent*)event {
    if (!g_input_queue) return;
    KeyCode kc = keycode_from_ns(event.keyCode);
    if (kc != KeyCode::Unknown) {
        g_input_queue->push(InputEvent::make_key_up(kc));
    }
}

// Trackpad → InputEventQueue (finger 1+)
- (void)touchesBeganWithEvent:(NSEvent*)event {
    if (!g_input_queue) return;
    NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseBegan inView:self];
    for (NSTouch* touch in touches) {
        NSPoint pt = touch.normalizedPosition;
        uint8_t tid = (uint8_t)([touch.identity hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_down(
            tid, (float)(pt.x * self.bounds.size.width), (float)(pt.y * self.bounds.size.height)));
    }
}

- (void)touchesMovedWithEvent:(NSEvent*)event {
    if (!g_input_queue) return;
    NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseMoved inView:self];
    for (NSTouch* touch in touches) {
        NSPoint pt = touch.normalizedPosition;
        uint8_t tid = (uint8_t)([touch.identity hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_move(
            tid, (float)(pt.x * self.bounds.size.width), (float)(pt.y * self.bounds.size.height)));
    }
}

- (void)touchesEndedWithEvent:(NSEvent*)event {
    if (!g_input_queue) return;
    NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseEnded inView:self];
    for (NSTouch* touch in touches) {
        uint8_t tid = (uint8_t)([touch.identity hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_up(tid));
    }
}

- (void)touchesCancelledWithEvent:(NSEvent*)event {
    if (!g_input_queue) return;
    NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseCancelled inView:self];
    for (NSTouch* touch in touches) {
        uint8_t tid = (uint8_t)([touch.identity hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_cancel(tid));
    }
}
@end

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    NSRect frame = NSMakeRect(100, 200, 900, 640);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled |
                                                       NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskMiniaturizable |
                                                       NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    [self.window setTitle:@"Markmos"];
    MetalView* view = [[MetalView alloc] initWithFrame:frame];
    if (!view) {
        return;
    }
    [self.window setContentView:view];
    [self.window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
//    MetalView* view = (MetalView*)self.window.contentView;
//    if ([view isKindOfClass:[MetalView class]]) {
//        [view stopDisplayLink];
//    }
    if (g_callbacks.cleanup) g_callbacks.cleanup(g_callbacks.user_data);
    g_audio_system.shutdown();
    if (g_backend) {
        g_backend->shutdown();
        delete g_backend;
    }
    delete g_input_queue;
    delete g_input_state;
}
@end

void app_quit() noexcept {
    exit(0);
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        g_callbacks = markmos_main(argc, const_cast<char**>(argv));
        [NSApplication sharedApplication];
        [NSApp setDelegate:[[AppDelegate alloc] init]];
        [NSApp run];
    }
    return 0;
}

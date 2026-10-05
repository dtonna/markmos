// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// macOS App Entry — Sokol-style: calls user-defined markmos_main() for callbacks
// Platform handles: Metal backend, input queue, audio, display link lifecycle
// User handles: game init/update/render via AppCallbacks

#include "mm_app.hpp"
#include "core/mm_types.h"
#include "../rhi/mm_rhi_concept.hpp"
#include "../rhi/mm_metal_backend.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../audio/mm_audio_system.hpp"
#include "../input/mm_input_event.hpp"
#include "../input/mm_input_state.hpp"
#include "../core/mm_vfs.hpp"
#include "../core/mm_pool.hpp"
#include "../core/mm_job_system.hpp"
#include "../game/mm_event_bus.hpp"

#include "../core/mm_log.hpp"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#include <cstdlib>
#include <unistd.h>

// Engine globals (accessible to user code via extern)
// Note: g_event_bus is defined once in engine/game/mm_event_bus.cpp,
// declared extern via ../game/mm_event_bus.hpp — do NOT redefine here.
MetalBackend*    g_backend       = nullptr;
InputEventQueue* g_input_queue   = nullptr;
InputState*      g_input_state   = nullptr;
f32            g_content_scale = 1.0f;
EventBus*        g_event_bus_ptr = nullptr;

// App callbacks (set by main() from user's markmos_main())
static AppCallbacks g_callbacks{};

// macOS NSEvent keyCode → engine KeyCode
static KeyCode keycode_from_ns(u16 kc) noexcept {
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
        case 0x1C: return KeyCode::D8; case 0x19: return KeyCode::D9; // 0x1C='8' (0x18 is '=' — must stay Unknown so typed +/- reaches text_input)
        case 0x7B: return KeyCode::Left;  case 0x7C: return KeyCode::Right;
        case 0x7E: return KeyCode::Up;    case 0x7D: return KeyCode::Down;
        case 0x38: return KeyCode::Shift; case 0x3B: return KeyCode::Ctrl;
        case 0x3A: return KeyCode::Alt;
        case 0x31: return KeyCode::Space;   case 0x24: return KeyCode::Enter;
        case 0x35: return KeyCode::Escape;  case 0x33: return KeyCode::Backspace;
        case 0x30: return KeyCode::Tab;
        case 0x75: return KeyCode::Delete;  // kVK_ForwardDelete (0x7F is kVK_Delete = Backspace)
        case 0x73: return KeyCode::Home;    case 0x77: return KeyCode::End;
        // Values from the SDK's HIToolbox/Events.h, not guessed:
        //   kVK_PageUp = 0x74, kVK_F4 = 0x76, kVK_PageDown = 0x79
        // PageUp/PageDown have their OWN keycodes and do NOT collide with the
        // arrows (kVK_Down = 0x7D). A Mac laptop keyboard sends PageUp/PageDown
        // for Fn+Up / Fn+Down, and the OS delivers 0x74 / 0x79 - so mapping them
        // is safe and there is no "Fn+Down is just Down" trap to document around.
        case 0x74: return KeyCode::PageUp;  case 0x79: return KeyCode::PageDown;
        case 0x76: return KeyCode::F4;
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
    bool _initialized;
    bool _init_done;
    // Modifier state, mirrored from flagsChanged: (see that method for why it
    // cannot be read off the keyboard events). One flag per KeyCode we map.
    bool _mod_shift;
    bool _mod_ctrl;
    bool _mod_alt;
}
//- (void)tick;
- (void)renderLoopStep:(CADisplayLink *)sender;
@end

@implementation MetalView

- (CALayer *)makeBackingLayer {
    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.device = MTLCreateSystemDefaultDevice();
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    // NOTE: maximumDrawableCount left at the system default (triple buffering).
    return metalLayer;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        self.allowedTouchTypes = NSTouchTypeMaskDirect;
        _lastFrameTime = CACurrentMediaTime();
        _initialized = false;
        _init_done = false;

        // 1. Setup system input boundaries
        NSTrackingArea* tracking = [[NSTrackingArea alloc]
            initWithRect:self.bounds
                 options:NSTrackingMouseMoved | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                   owner:self
                userInfo:nil];
        [self addTrackingArea:tracking];

        // NOTE: viewport updates are driven directly via setFrameSize: /
        // viewDidEndLiveResize / viewDidMoveToWindow — no NSNotification
        // observer needed (avoids dangling-observer crash on teardown).

        // NOTE: g_callbacks.init is NOT called here — backend/systems do not
        // exist yet. One-time init happens in viewDidMoveToWindow below.
    }
    return self;
}

// 2. Fixed Retina Scaling Bug: Track when window context becomes valid
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    
    if (self.window) {
        // One-time initialization
        if (!_initialized) {
            _initialized = true;
            
            CAMetalLayer* layer = (CAMetalLayer*)self.layer;
            g_backend = new MetalBackend();
            if (!g_backend->init((__bridge void*)layer)) {
                return;
            }
            
            g_input_queue = new InputEventQueue();
            g_input_state = new InputState();
            g_input_state->init();
            g_audio_system.init();
            
            PoolInit();
            JobSystemInit(0);
            g_event_bus.clear();
            g_event_bus_ptr = &g_event_bus;
            
            NSString* bundlePath = [[NSBundle mainBundle] resourcePath];
            NSArray* docPaths = NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES);
            g_vfs.init([bundlePath UTF8String],
                      docPaths.count > 0 ? [docPaths[0] UTF8String] : nullptr);
            chdir([bundlePath UTF8String]);
            
            if (g_callbacks.init) {
                g_callbacks.init(g_callbacks.user_data);
            }
            _init_done = true;
        }
        
        // Update viewport (called every time)
        [self updateViewportDimensions];
        
        // Start display link
        if (!_displayLink) {
            _displayLink = [self displayLinkWithTarget:self selector:@selector(renderLoopStep:)];
            [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        }
        
        [self.window makeFirstResponder:self];
    } else {
        [_displayLink invalidate];
        _displayLink = nil;
    }
}
- (void)renderLoopStep:(CADisplayLink *)sender {
    // One OR per frame; the message send only happens while a modifier is held.
    if ((_mod_shift || _mod_ctrl || _mod_alt) && self.window && ![self.window isKeyWindow]) {
        [self release_held_modifiers];
    }
    @autoreleasepool {
        CFTimeInterval currentTime = CACurrentMediaTime();
        f32 dt = static_cast<f32>(currentTime - _lastFrameTime);
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
    g_content_scale = static_cast<f32>(scale);
    
    u32 w = static_cast<u32>(size.width * scale);
    u32 h = static_cast<u32>(size.height * scale);
    
    // Explicitly update matching backing store dimensions
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    layer.drawableSize = CGSizeMake(w, h);
    layer.contentsScale = scale;

    SurfaceInfo info{
        .native_handle = (__bridge void*)layer,
        .width         = w,
        .height        = h,
        .content_scale = static_cast<f32>(scale > 1.0f ? scale : 1.0f),
    };
    if (g_backend) {
        g_backend->resize(info);
    }
    // setContentView: resizes the view, so setFrameSize: -> here fires while
    // the app is still uninitialized (g_callbacks.init has not run yet, so a
    // per-app resize handler may dereference state that does not exist yet).
    // viewDidMoveToWindow calls us again right after init, which is where the
    // app learns its real size.
    if (g_callbacks.resize && _init_done) {
        g_callbacks.resize(g_callbacks.user_data,
            static_cast<u32>(size.width),
            static_cast<u32>(size.height));
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

    delete g_backend;       g_backend = nullptr;
    delete g_input_queue;   g_input_queue = nullptr;
    delete g_input_state;   g_input_state = nullptr;
    g_event_bus.clear();
    JobSystemShutdown();
    
    [super removeFromSuperview];
}

// Mouse → InputEventQueue (finger 0)
// macOS origin is bottom-left; engine origin is top-left → flip Y
- (NSPoint)flipY:(NSPoint)pt {
    pt.y = self.bounds.size.height - pt.y;
    return pt;
}

- (void)mouseDown:(NSEvent*)event {
    if (!g_input_queue) return;
    NSPoint pt = [self flipY:[self convertPoint:event.locationInWindow fromView:nil]];
    g_input_queue->push(InputEvent::make_touch_down(0, (f32)pt.x, (f32)pt.y));
}

- (void)mouseDragged:(NSEvent*)event {
    if (!g_input_queue) return;
    NSPoint pt = [self flipY:[self convertPoint:event.locationInWindow fromView:nil]];
    g_input_queue->push(InputEvent::make_touch_move(0, (f32)pt.x, (f32)pt.y));
}

- (void)mouseUp:(NSEvent*)event {
    if (!g_input_queue) return;
    g_input_queue->push(InputEvent::make_touch_up(0));
}

- (void)mouseMoved:(NSEvent*)event {
    if (!g_input_queue) return;
    NSPoint pt = [self flipY:[self convertPoint:event.locationInWindow fromView:nil]];
    g_input_queue->push(InputEvent::make_mouse_move((f32)pt.x, (f32)pt.y));
}

- (void)rightMouseDown:(NSEvent*)event {}

- (void)scrollWheel:(NSEvent*)event {
    if (!g_input_queue) return;
    g_input_queue->push(InputEvent::make_mouse_scroll((f32)event.scrollingDeltaX,
                                                       (f32)event.scrollingDeltaY));
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

// Modifier keys arrive as flagsChanged:, NOT keyDown:.
//
// This is the AppKit contract and getting it wrong is silent: keyDown:/keyUp: are
// never sent for Shift / Control / Option / Command, so with only those two
// implemented `keys_down[Shift]` stayed false forever and every Shift+Tab was
// delivered as FocusNext - the FocusPrev half of the focus contract did not exist
// on the only desktop platform, and nothing failed, it just walked forwards. The
// same hole swallowed Alt+Down.
//
// Two details that are load-bearing:
// - Compare against STORED state and push only on a change. One event can flip
//   several flags at once (Shift+Option), and flagsChanged: fires again for every
//   later key event's flag set, so re-pushing on every event would re-arm a
//   modifier that is merely being HELD.
// - Only the three modifiers that exist as KeyCodes are mirrored. Command has no
//   KeyCode, so Cmd-shortcuts are still invisible to the engine (unchanged).

- (void)flagsChanged:(NSEvent*)event {
    if (!g_input_queue) return;
    const NSEventModifierFlags f = event.modifierFlags;
    bool* slot[3] = {&_mod_shift, &_mod_ctrl, &_mod_alt};
    const NSEventModifierFlags mask[3] = {NSEventModifierFlagShift, NSEventModifierFlagControl, NSEventModifierFlagOption};
    const KeyCode kc[3] = {KeyCode::Shift, KeyCode::Ctrl, KeyCode::Alt};
    for (int i = 0; i < 3; ++i) {
        const bool down = (f & mask[i]) != 0;
        if (down == *slot[i]) {
            continue;
        }
        *slot[i] = down;
        g_input_queue->push(down ? InputEvent::make_key_down(kc[i]) : InputEvent::make_key_up(kc[i]));
    }
}

// Release every modifier still marked down. Called when the window is not the key
// window: macOS does NOT guarantee a final flagsChanged with cleared flags when
// focus is lost, and a stuck Shift means every later Tab is FocusPrev, a stuck
// Option means every later Down is MenuToggle. Self-limiting - the guard in the
// caller makes this a no-op once the flags are cleared.
- (void)release_held_modifiers {
    if (!g_input_queue) return;
    if (_mod_shift) {
        _mod_shift = false;
        g_input_queue->push(InputEvent::make_key_up(KeyCode::Shift));
    }
    if (_mod_ctrl) {
        _mod_ctrl = false;
        g_input_queue->push(InputEvent::make_key_up(KeyCode::Ctrl));
    }
    if (_mod_alt) {
        _mod_alt = false;
        g_input_queue->push(InputEvent::make_key_up(KeyCode::Alt));
    }
}

// Trackpad → InputEventQueue (finger 1+)
- (void)touchesBeganWithEvent:(NSEvent*)event {
    if (!g_input_queue) return;
    NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseBegan inView:self];
    for (NSTouch* touch in touches) {
        NSPoint pt = touch.normalizedPosition;
        u8 tid = (u8)([touch.identity hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_down(
            tid, (f32)(pt.x * self.bounds.size.width), (f32)(pt.y * self.bounds.size.height)));
    }
}

- (void)touchesMovedWithEvent:(NSEvent*)event {
    if (!g_input_queue) return;
    NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseMoved inView:self];
    for (NSTouch* touch in touches) {
        NSPoint pt = touch.normalizedPosition;
        u8 tid = (u8)([touch.identity hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_move(
            tid, (f32)(pt.x * self.bounds.size.width), (f32)(pt.y * self.bounds.size.height)));
    }
}

- (void)touchesEndedWithEvent:(NSEvent*)event {
    if (!g_input_queue) return;
    NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseEnded inView:self];
    for (NSTouch* touch in touches) {
        u8 tid = (u8)([touch.identity hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_up(tid));
    }
}

- (void)touchesCancelledWithEvent:(NSEvent*)event {
    if (!g_input_queue) return;
    NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseCancelled inView:self];
    for (NSTouch* touch in touches) {
        u8 tid = (u8)([touch.identity hash] & 0xFF);
        g_input_queue->push(InputEvent::make_touch_cancel(tid));
    }
}
@end

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // Size comes from the app (AppCallbacks::width/height). 0 in either
    // field keeps the historical 900x640 default.
    const CGFloat w = g_callbacks.width  ? static_cast<CGFloat>(g_callbacks.width)  : 900.0;
    const CGFloat h = g_callbacks.height ? static_cast<CGFloat>(g_callbacks.height) : 640.0;
    NSRect        frame = NSMakeRect(100, 200, w, h);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled |
                                                       NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskMiniaturizable |
                                                       NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    // Title comes from the app (AppCallbacks::title). nullptr - or a string
    // that is not valid UTF-8 - falls back to the historical "Markmos", so
    // the examples and any app that does not set it behave exactly as before.
    // A minimum, because the window is resizable and the UI is laid out in
    // absolute numbers. Panels are placed with things like `view_h - 230`, so
    // below roughly that the height goes NEGATIVE - and a negative rect cast to
    // the uint16 scissor args wraps to ~65000, i.e. a scissor covering the whole
    // window. The engine now clamps degenerate frames too (mm_ui render), but
    // the honest fix is not to offer a size that breaks the layout.
    [self.window setContentMinSize:NSMakeSize(360.0, 280.0)];
    NSString *title = nil;
    if (g_callbacks.title) {
        title = [NSString stringWithUTF8String:g_callbacks.title];
    }
    [self.window setTitle:(title.length ? title : @"Markmos")];
    MetalView* view = [[MetalView alloc] initWithFrame:frame];
    if (!view) {
        return;
    }
    [self.window setContentView:view];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
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

    g_event_bus.clear();
    JobSystemShutdown();
    PoolShutdown();
}
@end

void app_quit() noexcept {
    exit(0);
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        g_callbacks = markmos_main(argc, const_cast<char**>(argv));
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp setDelegate:[[AppDelegate alloc] init]];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}

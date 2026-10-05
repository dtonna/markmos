// Button widget tests — factory defaults, the optional tail params (shape /
// on_draw / user), click + keyboard paths, effective-disabled, backfill, and
// the label fit rule's INPUTS (the clamp itself needs a Renderer).
// Plain main() + assert(), no framework.
// Headless: handle() and update() are pure CPU. The label is drawn in Pass 3 and
// the fill in Pass 1, both covered by the mm_07 D2 screenshot.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

static int  g_fire_count = 0;
static int  g_draw_count = 0;
static void *g_draw_user  = nullptr;

static void FireCb(u16, void *) noexcept {
    ++g_fire_count;
}

static void DrawCb(u16, Renderer &, SpriteBatch &, f32, f32, f32, void *user) noexcept {
    ++g_draw_count;
    g_draw_user = user;
}

static InputState FreshInput() noexcept {
    InputState in;
    in.init();
    return in;
}

static void Tap(InputState &in, f32 x, f32 y) noexcept {
    in.action_count = 1;
    in.actions[0]  = InputAction::Select;
    in.action_x    = x;
    in.action_y    = y;
}

int main() {
    // ─── Factory defaults ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        const u16 b = m.button(10.0f, 10.0f, 120.0f, 32.0f, "press", 0xFF3A3A3A, 0xFFFFFFFF, FireCb);
        assert(b != UINT16_MAX);
        assert(m.pool[b].type == (u8)ui::widget_type::BUTTON);
        assert(m.pool[b].frame.w == 120.0f && m.pool[b].frame.h == 32.0f);
        assert(std::strcmp(m.pool[b].text, "press") == 0);
        assert(m.pool[b].bg_color == 0xFF3A3A3A);
        assert(m.pool[b].state == 0);
        assert(m.pool[b].press_scale == 1.0f && m.pool[b].press_scale_target == 1.0f);
        // The PANEL fade-in must not touch a button. anim_t starts at 0 and only
        // advance()s for PANEL, so a button never fades - which is what this
        // checks after the frames, since the start value alone proves nothing.
        assert(m.pool[b].anim_t == 0.0f);
        for (int i = 0; i < 200; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.pool[b].anim_t == 0.0f); // still 0: only a PANEL eases this
        // Buttons ARE focusable. They were not until faaf2c1: with Confirm acting
        // only on focus_id and Tab walking WF_FOCUSABLE widgets, the toolkit had
        // NO keyboard path to any button - and mm_07's own "Focus nav" page
        // claimed otherwise. Pinned so nobody re-derives the ring and drops them.
        assert(m.pool[b].flags & ui::WF_FOCUSABLE);
        assert(m.pool[b].flags & ui::WF_VISIBLE);
        assert(m.pool[b].flags & ui::WF_ENABLED);
    }

    // ─── Tap fires the callback with its owner ───
    {
        ui::Manager m;
        m.init();
        int         owner_marker = 7;
        g_fire_count             = 0;
        m.button(10.0f, 10.0f, 120.0f, 32.0f, "go", 0xFF3A3A3A, 0xFFFFFFFF, FireCb, UINT16_MAX, 1.0f, 0, 0, nullptr, &owner_marker);
        InputState in = FreshInput();
        Tap(in, 70.0f, 26.0f);
        m.handle(in);
        assert(g_fire_count == 1);
        // The user pointer rides click_user[], which is what let every freecell
        // callback reach its Game* without a global.
        const u16 b = 0;
        assert(m.click_user[b] == &owner_marker);
    }

    // ─── A tap outside fires nothing ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        m.button(10.0f, 10.0f, 120.0f, 32.0f, "go", 0xFF3A3A3A, 0xFFFFFFFF, FireCb);
        InputState in = FreshInput();
        Tap(in, 400.0f, 400.0f);
        m.handle(in);
        assert(g_fire_count == 0);
    }

    // ─── Optional tail params: shape, on_draw, user ───
    // These used to require poking pool[] after creation, which is why the
    // factory grew them. All defaulted, so every existing caller is untouched.
    {
        ui::Manager m;
        m.init();
        g_draw_count = 0;
        g_draw_user  = nullptr;
        int          marker = 42;
        // shape != 0 (CUSTOM) means Pass 1 skips the widget entirely and on_draw
        // owns every pixel.
        const u16 custom = m.button(10.0f, 10.0f, 120.0f, 32.0f, "art", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX, 1.0f, 0,
                                         (u8)ui::shape_type::CUSTOM, DrawCb, &marker);
        assert(m.pool[custom].shape == (u8)ui::shape_type::CUSTOM);
        assert(m.pool[custom].on_draw == DrawCb);
        assert(m.draw_user[custom] == &marker);
        // `user` fills an owner array ONLY alongside a callback of that kind, so
        // there is no way to hand over a pointer nothing reads. Both defaults off.
        const u16 plain = m.button(10.0f, 60.0f, 120.0f, 32.0f, "plain", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX, 1.0f, 0, 0,
                                        nullptr, &marker);
        assert(m.pool[plain].on_draw == nullptr);
        assert(m.draw_user[plain] == nullptr);
        assert(m.click_user[plain] == nullptr);
        assert(m.pool[plain].shape == 0); // 0 = "use the style's shape", not CUSTOM
        // shape is INDEPENDENT of on_draw: a non-CUSTOM shape still draws the
        // style fill in Pass 1 and on_draw on top, which is the layered case.
        const u16 layered = m.button(10.0f, 110.0f, 120.0f, 32.0f, "layer", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX, 1.0f, 0,
                                          (u8)ui::shape_type::ROUNDED_RECT, DrawCb, &marker);
        assert(m.pool[layered].shape == (u8)ui::shape_type::ROUNDED_RECT);
        assert(m.pool[layered].on_draw == DrawCb);
        assert(m.draw_user[layered] == &marker);
        // Click callback and draw callback get SEPARATE owners.
        g_fire_count = 0;
        const u16 both = m.button(10.0f, 160.0f, 120.0f, 32.0f, "both", 0xFF3A3A3A, 0xFFFFFFFF, FireCb, UINT16_MAX, 1.0f, 0,
                                       (u8)ui::shape_type::CUSTOM, DrawCb, &marker);
        assert(m.click_user[both] == &marker);
        assert(m.draw_user[both] == &marker);
    }

    // ─── Keyboard: Tab reaches the button, then Confirm fires it ───
    {
        ui::Manager m;
        m.init();
        g_fire_count = 0;
        const u16 b = m.button(10.0f, 10.0f, 120.0f, 32.0f, "go", 0xFF3A3A3A, 0xFFFFFFFF, FireCb);
        InputState     in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::Confirm; // nothing focused yet -> nothing fires
        m.handle(in);
        assert(g_fire_count == 0);
        assert(m.focus_id == UINT16_MAX);

        in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::FocusNext; // focus nav
        m.handle(in);
        assert(m.focus_id == b);
        // Focus NAVIGATION is deliberately not "consumed" for the host, unlike a
        // ListBox consuming Up/Dn for its own selection - otherwise the first Tab
        // would freeze any host binding.
        assert(!m.nav_consumed_this_frame());

        in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::Confirm;
        m.handle(in);
        assert(g_fire_count == 1);
    }

    // ─── A DISABLED control is inert from the keyboard too ───
    // This is the one that mattered. The pointer path always respected the
    // effective state - pick() refuses a disabled widget, so a tap could never
    // reach it - but the keyboard Confirm branch had no enabled check at all: it
    // set clicked = focus_id and called on_click unconditionally. A greyed-out
    // "Delete" fired on Enter/Space, and a greyed-out toggle flipped. The
    // focus ring could not even get there any more (find_next_focus skips a
    // disabled widget), so this state needed the app to disable a control while
    // it was focused - a "saving..." button, a "not your turn" control, a
    // one-shot that latches off. The one that latches off is the one a player
    // mashes Enter on, and the mashing is the test: 20 presses, 0 effects.
    {
        ui::Manager  m;
        m.init();
        g_fire_count = 0;
        const u16 panel = m.panel(0.0f, 0.0f, 300.0f, 200.0f, 0xFF222222);
        const u16 b     = m.button(10.0f, 10.0f, 120.0f, 32.0f, "delete", 0xFF3A3A3A, 0xFFFFFFFF, FireCb, panel);
        const u16 t     = m.toggle(10.0f, 60.0f, 56.0f, 28.0f, "on", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, nullptr, panel);
        const u16 after = m.button(220.0f, 10.0f, 60.0f, 32.0f, "after", 0xFF3A3A3A, 0xFFFFFFFF, nullptr);

        // Focus the button while it still works, then disable the PANEL it lives
        // in. Disabling the panel is the case that matters, because is_enabled is
        // the effective state - and it is what an app actually does.
        InputState nav = FreshInput();
        nav.action_count = 1;
        nav.actions[0]   = InputAction::FocusNext;
        m.handle(nav);
        assert(m.focus_id == b);
        g_fire_count = 1;
        InputState live = FreshInput();
        live.action_count = 1;
        live.actions[0]   = InputAction::Confirm;
        m.handle(live);
        assert(g_fire_count == 2); // fires while enabled - the control above is not vacuous

        m.set_enabled(panel, false);
        assert(m.pool[b].flags & ui::WF_ENABLED); // own flag untouched...
        assert(!m.is_enabled(b));                 // ...but effectively dead
        assert(m.pool[t].flags & ui::WF_ENABLED);

        g_fire_count = 0;
        InputState drop = FreshInput();
        drop.action_count = 1;
        drop.actions[0]   = InputAction::Confirm;
        m.handle(drop);

        // Focus was DROPPED, not parked on a dead widget - and the position was
        // remembered. Not UINT16_MAX-then-first-on-page, because that would
        // teleport the player across the screen for a control that merely
        // greyed out; the anchor resumes the ring where it was interrupted.
        assert(m.focus_id == UINT16_MAX);
        assert(m.focus_anchor == b);

        for (int i = 0; i < 20; ++i) {
            InputState in = FreshInput();
            in.action_count = 1;
            in.actions[0]   = InputAction::Confirm;
            m.handle(in);
            assert(!m.pool[t].state);            // a disabled toggle never flips
            assert(m.focus_id == UINT16_MAX);    // and nothing steals focus either
        }
        assert(g_fire_count == 0); // ...and a disabled button never fires

        // Tab resumes from the anchor: the panel's toggle is skipped as disabled,
        // so the next stop is the button AFTER the panel.
        InputState resume = FreshInput();
        resume.action_count = 1;
        resume.actions[0]   = InputAction::FocusNext;
        m.handle(resume);
        assert(m.focus_id == after);
        assert(m.focus_anchor == UINT16_MAX); // the anchor is consumed, not sticky
    }

    // ─── A HIDDEN control releases focus too, and remembers the spot ───
    // The mirror of the above, for set_visible(false). It has to release: a hidden
    // widget is already skipped by focus navigation, so holding a ring on one is
    // inconsistent by construction. But nothing may open a popup on it either -
    // combobox_tests covers that half.
    {
        ui::Manager  m;
        m.init();
        static const char *const kTabs[] = {"A", "B"};
        const u16 b0 = m.button(10.0f, 10.0f, 80.0f, 32.0f, "first", 0xFF3A3A3A, 0xFFFFFFFF, nullptr);
        const u16 tb = m.tabbar(10.0f, 60.0f, 240.0f, 32.0f, kTabs, 2, UINT16_MAX, 0, nullptr);
        const u16 b1 = m.button(10.0f, 110.0f, 80.0f, 32.0f, "third", 0xFF3A3A3A, 0xFFFFFFFF, nullptr);

        InputState nav = FreshInput();
        nav.action_count = 1;
        nav.actions[0]   = InputAction::FocusNext;
        m.handle(nav);
        assert(m.focus_id == b0);
        InputState nav2 = FreshInput();
        nav2.action_count = 1;
        nav2.actions[0]   = InputAction::FocusNext;
        m.handle(nav2);
        assert(m.focus_id == tb);

        m.set_visible(tb, false);
        // The setter does NOT clear focus. That is deliberate: there is ONE choke
        // point (the top of handle()), because disabling a PANEL disables its
        // children through the effective state, so a setter would have to search
        // the subtree - and it would still miss begin_modal's root and remove().
        // The cost of the single choke point is that the ring survives until the
        // next frame, which is invisible in practice: every host runs handle()
        // before render() in the same frame, so it leaves the same frame's pixels.
        assert(m.focus_id == tb);
        assert(m.focus_anchor == UINT16_MAX);

        InputState sweep = FreshInput();
        m.handle(sweep);                       // one frame with no actions at all
        assert(m.focus_id == UINT16_MAX);      // released, not parked
        assert(m.focus_anchor == tb);          // ...and the spot was remembered

        // Tab resumes from the hidden widget's position, so it lands on b1 - the
        // NEXT control along - rather than restarting at b0.
        InputState resume = FreshInput();
        resume.action_count = 1;
        resume.actions[0]   = InputAction::FocusNext;
        m.handle(resume);
        assert(m.focus_id == b1);
        assert(m.focus_anchor == UINT16_MAX);
    }

    // ─── Press animation, and a disabled button does not chase it ───
    {
        ui::Manager m;
        m.init();
        const u16 b = m.button(10.0f, 10.0f, 120.0f, 32.0f, "go", 0xFF3A3A3A, 0xFFFFFFFF, nullptr);
        m.hot         = b;
        m.active      = b;
        m.pool[b].state = (u8)ui::btn_state::PRESSED;
        m.update(1.0f / 60.0f);
        assert(m.pool[b].press_scale < 1.0f && m.pool[b].press_scale > 0.8f);
        m.update(5.0f);
        assert(__builtin_fabsf(m.pool[b].press_scale - 0.85f) < 0.01f); // the tuned target
        // Disabled: the target snaps back to 1.0 whatever the state says.
        m.set_enabled(b, false);
        m.pool[b].state = (u8)ui::btn_state::PRESSED;
        m.update(5.0f);
        assert(__builtin_fabsf(m.pool[b].press_scale - 1.0f) < 0.01f);
    }

    // ─── A disabled button in a disabled PANEL is not clickable ───
    // The render side of this is the `is_enabled(i)` read in Pass 1 / 1.5; the
    // input side is `is_enabled` too. One test, both halves.
    {
        ui::Manager m;
        m.init();
        const u16 panel = m.panel(0.0f, 0.0f, 300.0f, 200.0f, 0xFF222222);
        g_fire_count         = 0;
        m.button(10.0f, 10.0f, 120.0f, 32.0f, "go", 0xFF3A3A3A, 0xFFFFFFFF, FireCb, panel);
        InputState in = FreshInput();
        Tap(in, 70.0f, 26.0f);
        m.handle(in);
        assert(g_fire_count == 1); // enabled: works

        m.set_enabled(panel, false);
        g_fire_count = 0;
        InputState in2 = FreshInput();
        Tap(in2, 70.0f, 26.0f);
        m.handle(in2);
        assert(g_fire_count == 0); // the PANEL disabled it, and the child never knew
        assert(!m.is_enabled(0));
        m.set_enabled(panel, true);
        assert(m.is_enabled(0));
    }

    // ─── backfill_callback_owners: the safety net for a missing owner ───
    // Every widget carrying a callback with no owner gets one. Without it a tap
    // reaches an app callback with a null context, which is a crash waiting for
    // the next screen that forgets a parameter.
    {
        ui::Manager m;
        m.init();
        int owner = 5;
        // Build one with a callback but NO owner, by poking the callback after
        // the factory (which is exactly the straggler case the net exists for).
        const u16 b = m.button(0.0f, 0.0f, 40.0f, 20.0f, "x", 0xFF111111, 0xFFFFFFFF, nullptr);
        m.pool[b].on_click = FireCb;
        assert(m.click_user[b] == nullptr);
        m.backfill_callback_owners(&owner);
        assert(m.click_user[b] == &owner);
        // Idempotent: an owner that is already set is left alone.
        int other = 9;
        m.backfill_callback_owners(&other);
        assert(m.click_user[b] == &owner);
    }

    // ─── A recycled slot does not inherit a button's style ───
    {
        ui::Manager m;
        m.init();
        const u16 a = m.button(0.0f, 0.0f, 40.0f, 20.0f, "a", 0xFF111111, 0xFFFFFFFF, nullptr);
        const u8  fancy = m.register_style(ui::gradient_style(TextureHandle::invalid(), 0.2f));
        m.set_style(a, fancy);
        assert(m.pool[a].style_id == fancy);
        m.clear();
        const u16 b = m.button(0.0f, 0.0f, 40.0f, 20.0f, "b", 0xFF222222, 0xFFFFFFFF, nullptr);
        assert(b == a);
        // clear() resets the STYLE pool too, so a stale id would dangle.
        assert(m.pool[b].style_id == 0);
        assert(m.style_count == 1); // the default style, re-registered
    }

    printf("[button] all tests passed\n");
    return 0;
}
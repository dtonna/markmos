// TabBar + Separator tests. Both live in mm_ui_wtab.hpp, so both are here.
//
// TabBar had 2 calls in one file and no test of its own - which is how its
// Pass 1.9 f64-draw bug survived: the strip was flushed without resetting the
// batch, so the next pass submitted its two quads again through its own pipeline.
// render() needs a Renderer and cannot be tested headless, but every INPUT rule
// around the strip can be, and those are what this file pins.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

static int  g_fire_count = 0;
static int  g_last_cell  = -1;

static void ChangeCb(u16, f32 v) noexcept {
    ++g_fire_count;
    g_last_cell = static_cast<int>(v);
}

static void NoopCb(u16, void *) noexcept {}

static InputState FreshInput() noexcept {
    InputState in;
    in.init();
    return in;
}

// A tap is Select at a point. hit_test() is not needed: handle() picks by rect.
static void Tap(InputState &in, f32 x, f32 y) noexcept {
    in.action_count = 1;
    in.actions[0]  = InputAction::Select;
    in.action_x    = x;
    in.action_y    = y;
}

static void Key(InputState &in, InputAction a) noexcept {
    in.action_count = 1;
    in.actions[0]  = a;
}

int main() {
    // ─── Factory: equal cells, item pointers, clamped initial ───
    {
        ui::Manager m;
        m.init();
        static const char *const kTabs[] = {"Deals", "Stats", "Help"};
        g_fire_count                     = 0;
        const u16 tb = m.tabbar(20.0f, 30.0f, 240.0f, 32.0f, kTabs, 3, UINT16_MAX, 0, ChangeCb);
        assert(tb != UINT16_MAX);
        assert(m.pool[tb].type == (u8)ui::widget_type::TABBAR);
        assert(m.tabbar_items[tb] == kTabs); // the app owns the strings
        assert(m.tabbar_count_[tb] == 3);
        assert(m.get_tab(tb) == 0);
        assert(m.pool[tb].flags & ui::WF_FOCUSABLE); // a strip IS in the Tab order
        // ...but its frame is the rule on BOTH axes: cell_w is frame.w / count and
        // the label scale is derived from the cell HEIGHT, so neither can be auto.
        assert(!(m.pool[tb].flags & (ui::WF_AUTO_W | ui::WF_AUTO_H)));
        // No own label: the labels come from the app's array. Pass 3 allowlists
        // TABBAR for exactly that reason, and this is why.
        assert(m.pool[tb].text[0] == '\0');
        // on_change is the tab callback (ChangeCallback, not ClickCallback).
        assert(m.on_change[tb] == ChangeCb);
        // The initial index is CLAMPED into range, so a stale app value cannot
        // point at a cell that does not exist.
        const u16 hi = m.tabbar(0.0f, 0.0f, 90.0f, 30.0f, kTabs, 3, UINT16_MAX, 9, nullptr);
        assert(m.get_tab(hi) == 2);
        const u16 lo = m.tabbar(0.0f, 0.0f, 90.0f, 30.0f, kTabs, 3, UINT16_MAX, -4, nullptr);
        assert(m.get_tab(lo) == 0);
    }

    // ─── tabbar_cell_at: the ONE cell geometry both paths use ───
    // The tap handler and the tests call this; the render path never inverts it
    // separately. Two boundary rules worth naming: x == frame_w belongs to the
    // LAST cell (not out of range), and anything left of the frame is -1.
    {
        assert(ui::Manager::tabbar_cell_at(240.0f, 3, 0.0f) == 0);
        assert(ui::Manager::tabbar_cell_at(240.0f, 3, 79.0f) == 0);
        assert(ui::Manager::tabbar_cell_at(240.0f, 3, 80.0f) == 1);
        assert(ui::Manager::tabbar_cell_at(240.0f, 3, 159.0f) == 1);
        assert(ui::Manager::tabbar_cell_at(240.0f, 3, 160.0f) == 2);
        assert(ui::Manager::tabbar_cell_at(240.0f, 3, 240.0f) == 2); // == width, still a cell
        assert(ui::Manager::tabbar_cell_at(240.0f, 3, -0.5f) == -1);
        assert(ui::Manager::tabbar_cell_at(240.0f, 3, 240.5f) == -1);
        assert(ui::Manager::tabbar_cell_at(240.0f, 0, 10.0f) == -1); // no cells
        assert(ui::Manager::tabbar_cell_at(0.0f, 3, 0.0f) == -1);   // no width
    }

    // ─── Tap switches tab; the active cell is a SILENT no-op ───
    {
        ui::Manager m;
        m.init();
        static const char *const kTabs[] = {"Deals", "Stats", "Help"};
        g_fire_count                     = 0;
        g_last_cell                      = -1;
        const u16 tb = m.tabbar(0.0f, 0.0f, 240.0f, 32.0f, kTabs, 3, UINT16_MAX, 0, ChangeCb);
        assert(m.get_tab(tb) == 0);

        InputState in = FreshInput();
        Tap(in, 200.0f, 16.0f); // cell 2
        m.handle(in);
        assert(m.get_tab(tb) == 2);
        assert(g_fire_count == 1);
        assert(g_last_cell == 2);
        // Change carries the cell index, like every other Change event.
        bool change = false;
        f32 v     = -1.0f;
        for (int i = 0; i < 8; ++i) {
            ui::UiEvent e;
            if (!m.poll_event(e)) {
                break;
            }
            if (e.type == ui::ui_event_type::CHANGE && e.id == tb) {
                change = true;
                v      = e.value;
            }
        }
        assert(change);
        assert(v == 2.0f);

        // Re-tapping the ACTIVE cell must not fire: the app's page swap is
        // idempotent and a second fire is noise (the radiobox rule).
        g_fire_count = 0;
        InputState in2 = FreshInput();
        Tap(in2, 200.0f, 16.0f);
        m.handle(in2);
        assert(m.get_tab(tb) == 2);
        assert(g_fire_count == 0);

        // A tap BELOW the strip is not the strip's.
        g_fire_count = 0;
        InputState in3 = FreshInput();
        Tap(in3, 120.0f, 200.0f);
        m.handle(in3);
        assert(m.get_tab(tb) == 2);
        assert(g_fire_count == 0);
    }

    // ─── Up/Dn while focused switches AND is consumed - and CLAMPS, not wraps ───
    // Consumed matters: a host binding Up/Dn to something else (mm_07's font
    // tuner) fired on every press while the strip moved too. Clamped, not
    // wrapped, because a strip that wraps hides the ends of its range.
    {
        ui::Manager m;
        m.init();
        static const char *const kTabs[] = {"Deals", "Stats", "Help"};
        g_fire_count                     = 0;
        const u16 tb = m.tabbar(0.0f, 0.0f, 240.0f, 32.0f, kTabs, 3, UINT16_MAX, 0, ChangeCb);

        InputState in = FreshInput();
        Key(in, InputAction::FocusNext); // nothing focused yet: the HOST keeps this key
        m.handle(in);
        assert(m.focus_id == tb);
        assert(!m.nav_consumed_this_frame());

        g_fire_count = 0;
        InputState in2 = FreshInput();
        Key(in2, InputAction::MenuDown); // now the strip owns it
        m.handle(in2);
        assert(m.nav_consumed_this_frame());
        assert(m.get_tab(tb) == 1);
        assert(g_fire_count == 1);
        // Focus does NOT move: the strip switched, it did not hand the key on.
        assert(m.focus_id == tb);

        InputState in3 = FreshInput();
        Key(in3, InputAction::MenuUp);
        m.handle(in3);
        assert(m.get_tab(tb) == 0);

        // BOTH ends clamp rather than wrap - a strip that wraps hides the ends
        // of its range. Each half needs its own frame, because the first one
        // that hits a bound is the only frame that can prove it.
        InputState up = FreshInput();
        Key(up, InputAction::MenuUp); // 0 -> -1
        m.handle(up);
        assert(m.get_tab(tb) == 0);
        g_fire_count = 0;
        InputState up2 = FreshInput();
        Key(up2, InputAction::MenuUp); // already at 0: silent
        m.handle(up2);
        assert(m.get_tab(tb) == 0);
        assert(g_fire_count == 0);

        // Up to the top...
        for (int i = 0; i < 2; ++i) {
            InputState d = FreshInput();
            Key(d, InputAction::MenuDown);
            m.handle(d);
        }
        assert(m.get_tab(tb) == 2);
        // ...and one past it: clamped, not wrapped back to 0, and silent.
        g_fire_count = 0;
        InputState past = FreshInput();
        Key(past, InputAction::MenuDown); // 2 -> 3
        m.handle(past);
        assert(m.get_tab(tb) == 2);
        assert(g_fire_count == 0);

        // Tab must still walk focus OFF a strip that is sitting on its last
        // cell. This is the trap the clamp above sets up: when Tab arrived as
        // MenuDown it took this branch, the strip clamped at cell 2 and
        // `continue`d, so a player who paged to the end could never Tab away
        // again. Clamping the arrows is right; swallowing Tab is not.
        const u16 after = m.button(300.0f, 0.0f, 80.0f, 40.0f, "next", 0xFF3A3A3A, 0xFFFFFFFF, nullptr);
        InputState       leave = FreshInput();
        Key(leave, InputAction::FocusNext);
        m.handle(leave);
        assert(m.focus_id == after);          // walked off the strip
        assert(m.get_tab(tb) == 2);           // the active cell is unchanged
        assert(!m.nav_consumed_this_frame()); // and the host kept the key

        // Shift-Tab returns to the strip without switching anything either.
        g_fire_count     = 0;
        InputState back  = FreshInput();
        Key(back, InputAction::FocusPrev);
        m.handle(back);
        assert(m.focus_id == tb);
        assert(m.get_tab(tb) == 2);
        assert(g_fire_count == 0);
    }

    // ─── set_tab: bounds-checked, no clamping (that is the factory's job) ───
    {
        ui::Manager m;
        m.init();
        static const char *const kTabs[] = {"A", "B", "C"};
        const u16        tb   = m.tabbar(0.0f, 0.0f, 90.0f, 30.0f, kTabs, 3, UINT16_MAX, 0, nullptr);
        m.set_tab(tb, 2);
        assert(m.get_tab(tb) == 2);
        m.set_tab(tb, 3); // out of range: refused, not clamped
        assert(m.get_tab(tb) == 2);
        m.set_tab(tb, -1);
        assert(m.get_tab(tb) == 2);
        m.set_tab(9999, 1); // invalid widget: no crash
        assert(m.get_tab(9999) == -1);
    }

    // ─── Separator: the frame IS the rule, and it stays out of every path ───
    {
        ui::Manager m;
        m.init();
        const u16 s = m.separator(10.0f, 20.0f, 200.0f, UINT16_MAX);
        assert(m.pool[s].type == (u8)ui::widget_type::SEPARATOR);
        assert(m.pool[s].frame.w == 200.0f);
        assert(m.pool[s].frame.h == m.theme.separator_thickness); // default thickness
        // NO auto size: an auto-sized rule would size itself from an EMPTY label,
        // which is zero - the frame is the rule, so auto is meaningless here.
        assert(!(m.pool[s].flags & (ui::WF_AUTO_W | ui::WF_AUTO_H)));
        assert(!(m.pool[s].flags & ui::WF_FOCUSABLE));
        assert(m.pool[s].text[0] == '\0');
        assert(m.pool[s].on_click == nullptr);
        // Pass 1 draws any visible widget with a bg_color, so that IS its render
        // code. With no colour it uses the theme's.
        assert(m.pool[s].bg_color == m.theme.separator_color);

        const u16 custom = m.separator(0.0f, 0.0f, 100.0f, UINT16_MAX, 0xFF00FF00, 6.0f);
        assert(m.pool[custom].frame.h == 6.0f);
        assert(m.pool[custom].bg_color == 0xFF00FF00);

        // Vertical is a transposed horizontal: x/y become x/thickness and the
        // extent becomes the HEIGHT.
        const u16 v = m.separator_v(50.0f, 60.0f, 120.0f, UINT16_MAX);
        assert(m.pool[v].type == (u8)ui::widget_type::SEPARATOR);
        assert(m.pool[v].frame.w == m.theme.separator_thickness);
        assert(m.pool[v].frame.h == 120.0f);
    }

    // ─── A separator is INERT: not pickable, not focusable, no container ───
    // It is a container ONLY if it has children or a layout, which is what makes
    // skipping it a type check rather than a flag check.
    {
        ui::Manager m;
        m.init();
        const u16 s = m.separator(10.0f, 20.0f, 200.0f, UINT16_MAX);
        assert(!m.is_container(s));
        InputState in = FreshInput();
        Tap(in, 100.0f, 20.0f);
        m.handle(in);
        assert(m.clicked != s);
        assert(m.focus_id != s);
        // And no callback could fire even if something pointed at it.
        assert(m.pool[s].on_click == nullptr);
    }

    // ─── Recycled slots do not inherit a tab strip or a rule ───
    // mm_07/freecell rebuild their whole UI every frame, so "slot N held a
    // tabbar last frame" is the common case, not an edge one.
    {
        ui::Manager m;
        m.init();
        static const char *const kTabs[] = {"A", "B", "C"};
        const u16        tb   = m.tabbar(0.0f, 0.0f, 90.0f, 30.0f, kTabs, 3, UINT16_MAX, 2, nullptr);
        m.set_tab(tb, 1);
        m.clear();
        const u16 plain = m.separator(0.0f, 0.0f, 90.0f, UINT16_MAX);
        assert(plain == tb); // the same slot came back
        assert(m.pool[plain].type == (u8)ui::widget_type::SEPARATOR);
        // NOTE: tabbar_items / tabbar_count_ / on_change are NOT reset in
        // alloc(), and do not need to be - the tabbar FACTORY writes all three,
        // and a separator can never read them because every reader is
        // type-guarded (tabbar::compute and render_text both return early on
        // pool[i].type != TABBAR). That makes a stale pointer dormant rather
        // than wrong, which is the same category as slider_value. The arrays
        // that DID have to move into alloc() are the ones a SETTER writes.
        //
        // What must be true, and is: a tap on the recycled slot changes no tab,
        // and it cannot reach the previous strip's callback.
        InputState in = FreshInput();
        Tap(in, 45.0f, 1.0f);
        m.handle(in);
        assert(m.pool[plain].type == (u8)ui::widget_type::SEPARATOR); // pick() skips it
        assert(m.get_tab(plain) == 1); // the stale active index is unreachable
        assert(m.on_change[plain] == nullptr); // and this strip had no callback anyway
    }

    // ─── Home / End jump to the first / last cell ───
    // Same fold-into-the-arrow-body as the ListBox: pick a cell, then fire. A
    // second copy of that is free to forget the callback, and the callback is the
    // part an app actually consumes.
    {
        ui::Manager  m;
        m.init();
        static const char *const kTabs[] = {"Deals", "Stats", "Help"};
        g_fire_count = 0;
        const u16 tb = m.tabbar(0.0f, 0.0f, 240.0f, 32.0f, kTabs, 3, UINT16_MAX, 0, ChangeCb);
        m.focus_id = tb;

        auto key = [&m](InputAction a) {
            InputState in = FreshInput();
            Key(in, a);
            m.handle(in);
        };

        key(InputAction::MenuLast);
        assert(m.get_tab(tb) == 2);
        assert(g_fire_count == 1);
        assert(m.nav_consumed_this_frame());
        assert(m.focus_id == tb);

        key(InputAction::MenuFirst);
        assert(m.get_tab(tb) == 0);
        assert(g_fire_count == 2);

        // Already at the bound: silent, no event, no wrap. The strip clamps rather
        // than wraps, and Home/End must obey the same rule.
        g_fire_count = 0;
        key(InputAction::MenuFirst);
        assert(m.get_tab(tb) == 0);
        assert(g_fire_count == 0);
        key(InputAction::MenuLast);
        key(InputAction::MenuLast);
        assert(m.get_tab(tb) == 2);
        assert(g_fire_count == 1); // the second one was a no-op
    }

    // ─── Confirm on a TabBar is a true NO-OP - and emits no event ───
    // It used to emit a CLICK and do nothing else. A TabBar's factory takes only
    // a ChangeCallback, so `on_click` was null and no app callback could ever
    // fire - while `emit_event(CLICK)` ran unconditionally. An app listening for
    // CLICK was told the strip had been clicked when no cell had switched.
    //
    // A no-op is the right answer rather than "switch the cell": the arrows
    // already move the active cell, so there is no separate highlighted-vs-active
    // state for Enter to act on. Re-confirming the current value is already a
    // silent no-op for a tap on the active cell and for a radiobox.
    {
        ui::Manager  m;
        m.init();
        static const char *const kTabs[] = {"A", "B", "C"};
        g_fire_count = 0;
        const u16 tb = m.tabbar(0.0f, 0.0f, 240.0f, 32.0f, kTabs, 3, UINT16_MAX, 1, ChangeCb);
        m.focus_id = tb;

        // Arrows DO reach the app - that is the contrast that makes the no-op a
        // decision rather than an absence.
        InputState dn = FreshInput();
        Key(dn, InputAction::MenuDown);
        m.handle(dn);
        assert(m.get_tab(tb) == 2);
        assert(g_fire_count == 1);

        g_fire_count = 0;
        InputState en = FreshInput();
        Key(en, InputAction::Confirm);
        m.handle(en);
        assert(m.get_tab(tb) == 2);                  // no cell switched
        assert(g_fire_count == 0);                   // no callback
        assert(!m.nav_consumed_this_frame());        // nothing consumed the key
        ui::UiEvent ev{};
        while (m.poll_event(ev)) {
            assert(ev.type != ui::ui_event_type::CLICK); // and NO phantom event
        }
    }

    // ─── Left / Right switch cells: a strip is a HORIZONTAL row ───
    // Left/Right emitted no action at all until the keymap gained MenuLeft /
    // MenuRight, which left a tab strip reachable only by Up/Down or a tap. Both
    // axes are accepted now; the clamping is shared with the arrows (a strip that
    // wraps hides the ends of the range).
    {
        ui::Manager m;
        m.init();
        static const char *const kTabs[] = {"Deals", "Stats", "Help"};
        g_fire_count                     = 0;
        const u16 tb = m.tabbar(0.0f, 0.0f, 240.0f, 32.0f, kTabs, 3, UINT16_MAX, 0, ChangeCb);
        m.focus_id = tb;

        InputState rt = FreshInput();
        Key(rt, InputAction::MenuRight);
        m.handle(rt);
        assert(m.get_tab(tb) == 1);
        assert(g_fire_count == 1);
        InputState rt2 = FreshInput();
        Key(rt2, InputAction::MenuRight);
        m.handle(rt2);
        assert(m.get_tab(tb) == 2);
        InputState lf = FreshInput();
        Key(lf, InputAction::MenuLeft);
        m.handle(lf);
        assert(m.get_tab(tb) == 1);

        // Clamped at both ends, NOT wrapped, and a clamped press is the same
        // silent no-op as re-confirming the active cell (no callback, no event).
        g_fire_count = 0;
        InputState lf2 = FreshInput();
        Key(lf2, InputAction::MenuLeft);
        m.handle(lf2);
        assert(m.get_tab(tb) == 0);
        InputState lf3 = FreshInput();
        Key(lf3, InputAction::MenuLeft);
        m.handle(lf3);
        assert(m.get_tab(tb) == 0);
        assert(g_fire_count == 1); // only the two real moves fired

        // And the horizontal keys are FLAGGED, or a host binding Left/Right for
        // its own nav would also page the strip.
        InputState probe = FreshInput();
        Key(probe, InputAction::MenuRight);
        m.handle(probe);
        assert(m.nav_consumed_this_frame());
    }

    // ─── PageUp / PageDown are a deliberate NO-OP on a strip ───
    // "A page" is a viewport, and a tab strip is one line with no viewport and no
    // scrolling - there is nothing to page. Not consuming the key is the point: a
    // host may still bind it, exactly like Esc on a ListBox. Pinned so a future
    // "make Page work everywhere" pass has to decide this on purpose.
    {
        ui::Manager m;
        m.init();
        static const char *const kTabs[] = {"Deals", "Stats", "Help"};
        g_fire_count                     = 0;
        const u16 tb = m.tabbar(0.0f, 0.0f, 240.0f, 32.0f, kTabs, 3, UINT16_MAX, 0, ChangeCb);
        m.focus_id = tb;

        for (int i = 0; i < 3; ++i) {
            InputState in = FreshInput();
            Key(in, InputAction::MenuPageDown);
            m.handle(in);
        }
        assert(m.get_tab(tb) == 0);
        assert(g_fire_count == 0);
        assert(!m.nav_consumed_this_frame());
        InputState up = FreshInput();
        Key(up, InputAction::MenuPageUp);
        m.handle(up);
        assert(m.get_tab(tb) == 0);
        assert(!m.nav_consumed_this_frame());
    }

    printf("[tabbar] all tests passed\n");
    return 0;
}
// Manager::update() tests — time-driven widget state advances in update(),
// never in render() (Plan B: render is pure draw, takes no dt).
// Plain main() + assert(), no framework. Headless: update() is pure CPU.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cstring>
#include <cmath>

static void NoopCb(u16, void *) noexcept {}

// Modal tests: ClickCallback is a plain function pointer, so the counters are
// file-scope (same pattern as radiobox_tests).
static int g_modal_dismiss = 0;
static int g_modal_inner   = 0;
static void ModalDismissCb(u16, void *) noexcept { ++g_modal_dismiss; }
static void ModalInnerCb(u16, void *) noexcept { ++g_modal_inner; }

static bool Near(f32 a, f32 b) noexcept {
    return __builtin_fabsf(a - b) < 0.001f;
}

int main() {
    // ─── Panel fade converges 0 → 1 via update ───
    {
        ui::Manager m;
        m.init();
        u16 p = m.panel(10.0f, 10.0f, 200.0f, 100.0f, 0xFF222222);
        assert(Near(m.pool[p].anim_t, 0.0f));
        m.update(0.05f); // anim_speed=10 → +0.5
        assert(Near(m.pool[p].anim_t, 0.5f));
        m.update(0.05f);
        assert(Near(m.pool[p].anim_t, 1.0f));
        m.update(1.0f); // capped, never exceeds 1
        assert(Near(m.pool[p].anim_t, 1.0f));
    }

    // ─── cursor_timer advances only via update ───
    {
        ui::Manager m;
        m.init();
        assert(Near(m.cursor_timer, 0.0f));
        m.update(0.25f);
        assert(Near(m.cursor_timer, 0.25f));
    }

    // ─── Toggle thumb slides toward ON state ───
    {
        ui::Manager m;
        m.init();
        u16 t = m.toggle(100.0f, 100.0f, 140.0f, 40.0f, "T", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, true, NoopCb);
        assert(Near(m.pool[t].thumb_pos, 0.0f));
        m.update(1.0f); // t_lerp = 1 - e^-10 ≈ 1
        assert(m.pool[t].thumb_pos > 0.99f);
    }

    // ─── Button press scale converges toward PRESSED target ───
    {
        ui::Manager m;
        m.init();
        u16 b = m.button(100.0f, 100.0f, 80.0f, 30.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);
        m.pool[b].state = (u8)ui::btn_state::PRESSED;
        m.update(1.0f);
        assert(__builtin_fabsf(m.pool[b].press_scale - 0.85f) < 0.01f);
        m.pool[b].state = (u8)ui::btn_state::NORMAL;
        m.update(1.0f);
        assert(__builtin_fabsf(m.pool[b].press_scale - 1.0f) < 0.01f);
    }

    // ─── Button hover factor converges ───
    {
        ui::Manager m;
        m.init();
        u16 b = m.button(100.0f, 100.0f, 80.0f, 30.0f, "B", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);
        m.pool[b].state = (u8)ui::btn_state::HOVER;
        m.update(1.0f);
        assert(m.pool[b].hover_factor > 0.99f);
    }

    // ─── update(0) is a bit-identical no-op ───
    // (purity guard: with no time passing, no anim state may move —
    // render() itself takes no dt, so time cannot leak through draw).
    {
        ui::Manager m;
        m.init();
        u16 p = m.panel(10.0f, 10.0f, 200.0f, 100.0f, 0xFF222222);
        m.update(0.03f);
        f32 anim = m.pool[p].anim_t;
        f32 cur  = m.cursor_timer;
        assert(anim > 0.0f && cur > 0.0f);
        m.update(0.0f);
        assert(m.pool[p].anim_t == anim);
        assert(m.cursor_timer == cur);
    }

    // ─── Modal: input routing, focus trap, dismiss ───
    {
        ui::Manager m;
        m.init();
        // Scene under the dialog, then the dialog itself.
        u16 scene_btn = m.button(10.0f, 10.0f, 100.0f, 30.0f, "scene", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb, UINT16_MAX);
        u16 root      = m.panel(20.0f, 20.0f, 200.0f, 120.0f, 0xFF2D2D2D, UINT16_MAX);
        u16 ok        = m.button(0.0f, 0.0f, 80.0f, 30.0f, "ok", 0xFF5CB85C, 0xFFFFFFFF, NoopCb, root);
        u16 cancel    = m.button(0.0f, 40.0f, 80.0f, 30.0f, "no", 0xFF5CB85C, 0xFFFFFFFF, NoopCb, root);
        (void)cancel;

        assert(!m.is_modal());
        // Without a modal the scene button is reachable.
        assert(m.hit_test(50.0f, 12.0f) == scene_btn);

        m.begin_modal(root);
        assert(m.is_modal());
        assert(m.in_modal(root) && m.in_modal(ok));
        assert(!m.in_modal(scene_btn));
        // pick() is scoped: the scene button is gone, the dialog's widget is not.
        assert(m.hit_test(50.0f, 12.0f) != scene_btn);
        assert(m.hit_test(60.0f, 35.0f) == ok);

        // Focus trap: focus cycles INSIDE the dialog, never onto the scene.
        // Toggles because they are the cheapest focusable type, so the ring has
        // room to wrap without the buttons' own taps muddying the count.
        u16 scene_tg = m.toggle(10.0f, 200.0f, 80.0f, 30.0f, "scene", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, UINT16_MAX);
        u16 dlg_tg1  = m.toggle(0.0f, 80.0f, 80.0f, 30.0f, "a", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, root);
        u16 dlg_tg2  = m.toggle(0.0f, 115.0f, 80.0f, 30.0f, "b", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, root);
        m.hit_test(0.0f, 0.0f); // build the abs cache (no-op query)
        bool escaped = false;
        for (int i = 0; i < 10; ++i) {
            InputState in;
            in.init();
            in.action_count = 1;
            in.actions[0]    = InputAction::FocusNext; // focus-next
            m.handle(in);
            if (m.focus_id == scene_tg) {
                escaped = true;
            }
        }
        assert(!escaped);
        assert(m.in_modal(m.focus_id));
        (void)dlg_tg1;
        (void)dlg_tg2;
    }

    // ─── Focus trap: BACKWARD (Shift-Tab) wraps inside the modal too ───
    // find_prev_focus()'s wrap-around loop was left checking only the OWN
    // flags and missing in_modal() entirely, so Shift+Tab from the first
    // focusable widget in the dialog landed on the scene.
    //
    // Pool order is the whole point of this test. Tab/Shift-Tab walk the pool
    // by INDEX, and the backward wrap loop walks DOWN from count-1, so the
    // only widget it can hand back is one with a HIGHER index than the current
    // focus. Build the dialog first and the scene widget last, or the wrap loop
    // is never reached and the bug hides - which is what the first version of
    // this test did (it passed against the broken code).
    {
        ui::Manager m;
        m.init();
        // Dialog first: root=0, its toggles=1,2. Scene widget LAST: 3.
        u16 root = m.panel(20.0f, 20.0f, 200.0f, 120.0f, 0xFF2D2D2D, UINT16_MAX);
        u16 dlg_a = m.toggle(0.0f, 0.0f, 80.0f, 30.0f, "a", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, root);
        u16 dlg_b = m.toggle(0.0f, 40.0f, 80.0f, 30.0f, "b", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, root);
        u16 scene_tg = m.toggle(10.0f, 10.0f, 80.0f, 30.0f, "scene", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, UINT16_MAX);
        assert(root == 0 && dlg_a == 1 && dlg_b == 2 && scene_tg == 3);
        m.begin_modal(root);
        m.hit_test(0.0f, 0.0f); // build the abs cache

        // Park focus on the LOWEST focusable widget in the dialog, so the
        // backward scan finds nothing above it and must wrap.
        m.focus_id = dlg_a;
        bool escaped = false;
        for (int i = 0; i < 10; ++i) {
            InputState in;
            in.init();
            in.action_count = 1;
            in.actions[0]    = InputAction::FocusPrev; // focus-prev (Shift-Tab)
            m.handle(in);
            if (m.focus_id == scene_tg || !m.in_modal(m.focus_id)) {
                escaped = true;
            }
        }
        assert(!escaped);
        assert(m.focus_id == dlg_a || m.focus_id == dlg_b);

        // Same wrap, but the scene widget is INVISIBLE rather than out of
        // scope: the backward loop read own-flags, so it could also land on
        // (or step over) a widget whose panel was disabled.
        u16 hidden = m.toggle(0.0f, 80.0f, 80.0f, 30.0f, "hid", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, root);
        m.set_visible(hidden, false);
        m.focus_id = dlg_a;
        for (int i = 0; i < 10; ++i) {
            InputState in;
            in.init();
            in.action_count = 1;
            in.actions[0]    = InputAction::FocusPrev;
            m.handle(in);
            assert(m.focus_id != hidden);
            assert(m.focus_id != scene_tg);
        }
    }

    // ─── Shift-Tab with NOTHING focused used to read 8.4MB past the pool ───
    // `from` is UINT16_MAX when no widget holds focus, and find_prev_focus()
    // walked down from it: pool[from - 1] = pool[65534], on a 128-entry array.
    // SIGBUS on the ScrollView page. The three predicates in the loop all guard
    // on `id < MAX`, which made it look safe - the unguarded read was the
    // `pool[id].flags` AFTER them, so the guards protected nothing.
    {
        ui::Manager m;
        m.init();
        // Three focusable widgets and one focus-less button (buttons are
        // focusable NOW - faaf2c1 - so use a panel as the non-focusable one).
        u16 t0 = m.toggle(10.0f, 10.0f, 80.0f, 30.0f, "a", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, UINT16_MAX);
        u16 bg = m.panel(10.0f, 50.0f, 80.0f, 30.0f, 0xFF2D2D2D, UINT16_MAX);
        u16 t1 = m.toggle(10.0f, 90.0f, 80.0f, 30.0f, "b", 0xFF5CB85C, 0xFF444444, 0xFFFFFFFF, false, NoopCb, UINT16_MAX);
        (void)bg;
        assert(t0 == 0 && t1 == 2);
        m.hit_test(0.0f, 0.0f);

        assert(m.focus_id == UINT16_MAX); // nothing focused

        // This is the crash: MenuUp with nothing focused reached
        // find_prev_focus(UINT16_MAX), which read pool[65534].
        InputState in;
        in.init();
        in.action_count = 1;
        in.actions[0]    = InputAction::FocusPrev; // Shift-Tab from nothing
        m.handle(in);
        assert(m.focus_id == t1); // lands on the LAST focusable widget
        m.handle(in);
        assert(m.focus_id == t0); // ...and from there it walks backwards
        m.handle(in);
        assert(m.focus_id == t1); // wraps
    }

    // ─── Modal: tap inside acts normally, tap outside dismisses + no leak ───
    {
        ui::Manager m;
        m.init();
        g_modal_dismiss = 0;
        g_modal_inner   = 0;
        u16 root = m.panel(100.0f, 100.0f, 200.0f, 120.0f, 0xFF2D2D2D, UINT16_MAX);
        u16 ok   = m.button(0.0f, 0.0f, 80.0f, 30.0f, "ok", 0xFF5CB85C, 0xFFFFFFFF, ModalInnerCb, root);
        (void)ok;
        u16 scene_btn = m.button(10.0f, 10.0f, 100.0f, 30.0f, "scene", 0xFF3A3A3A, 0xFFFFFFFF, ModalInnerCb, UINT16_MAX);
        m.begin_modal(root, ModalDismissCb, nullptr);

        // Tap INSIDE: the dialog's button fires, the modal survives.
        InputState in;
        in.init();
        in.action_count = 1;
        in.actions[0]    = InputAction::Select;
        in.action_x      = 140.0f;
        in.action_y      = 115.0f;
        m.handle(in);
        assert(g_modal_inner == 1);
        assert(g_modal_dismiss == 0 && m.is_modal());

        // Tap OUTSIDE: dismiss fires, the scope ends, and the click does NOT
        // fall through to the widget underneath.
        InputState out;
        out.init();
        out.action_count = 1;
        out.actions[0]    = InputAction::Select;
        out.action_x      = 50.0f;
        out.action_y      = 12.0f;
        const int before  = g_modal_inner;
        m.handle(out);
        assert(g_modal_dismiss == 1);
        assert(!m.is_modal());
        assert(g_modal_inner == before);
        // ...and the scene button is reachable again.
        assert(m.hit_test(50.0f, 12.0f) == scene_btn);
    }

    // ─── Modal: Back/Escape closes it, once ───
    {
        ui::Manager m;
        m.init();
        g_modal_dismiss = 0;
        u16 root    = m.panel(10.0f, 10.0f, 200.0f, 120.0f, 0xFF2D2D2D, UINT16_MAX);
        m.begin_modal(root, ModalDismissCb, nullptr);
        InputState esc;
        esc.init();
        esc.action_count = 1;
        esc.actions[0]    = InputAction::Back;
        m.handle(esc);
        assert(g_modal_dismiss == 1 && !m.is_modal());
        m.handle(esc); // no scope any more
        assert(g_modal_dismiss == 1);
    }

    // ─── Modal: end_modal() without a callback is safe + idempotent ───
    {
        ui::Manager m;
        m.init();
        u16 root = m.panel(10.0f, 10.0f, 200.0f, 120.0f, 0xFF2D2D2D, UINT16_MAX);
        m.begin_modal(root);
        assert(m.is_modal());
        m.end_modal();
        assert(!m.is_modal());
        m.end_modal();
    }

    // ─── begin_modal rejects a bad root instead of scoping to garbage ───
    {
        ui::Manager m;
        m.init();
        m.begin_modal(9999);
        assert(!m.is_modal());
    }

    // ─── set_text: explicit about the inline buffer instead of truncating ───
    {
        ui::Manager m;
        m.init();
        u16 lbl = m.label(10.0f, 10.0f, "short", 0xFFFFFFFF, 0.40f);
        assert(std::strcmp(m.text_of(lbl), "short") == 0);

        // Fits: copied into the inline buffer, ext cleared.
        assert(m.set_text(lbl, "a bit longer but still under 48"));
        assert(std::strcmp(m.text_of(lbl), "a bit longer but still under 48") == 0);
        assert(m.text_ext[lbl] == nullptr);

        // Does NOT fit: refused, and the old text is untouched. This is the
        // whole point - the factories used to strncpy and truncate mid-word.
        char big[80];
        std::memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        assert(!m.set_text(lbl, big));
        assert(std::strcmp(m.text_of(lbl), "a bit longer but still under 48") == 0);

        // Long text goes through the caller-owned override instead.
        static const char *kLong = "a string well past the forty-eight byte inline buffer, kept by the caller";
        m.set_text_ext(lbl, kLong);
        assert(std::strcmp(m.text_of(lbl), kLong) == 0);
        m.set_text_ext(lbl, nullptr);
        assert(std::strcmp(m.text_of(lbl), "a bit longer but still under 48") == 0);
        assert(!m.set_text(9999, "x"));
    }

    // ─── Tooltip: hover accumulates, dt == 0 does not, leaving resets ───
    {
        ui::Manager m;
        m.init();
        static const char *kTip = "line one\nline two";
        u16 host = m.button(10.0f, 10.0f, 120.0f, 30.0f, "hover me", 0xFF3A3A3A, 0xFFFFFFFF, NoopCb);
        assert(m.tooltip_str[host] == nullptr);

        m.tooltip(host, kTip, 0.5f);
        assert(m.tooltip_str[host] == kTip);

        // No pointer over it: nothing accumulates.
        for (int i = 0; i < 30; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.tooltip_for == UINT16_MAX);

        // dt == 0 must not advance the timer either (Plan B no-op guard).
        m.hot = host;
        m.update(0.0f);
        assert(m.tooltip_t == 0.0f);
        for (int i = 0; i < 20; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.tooltip_for == host);
        assert(m.tooltip_t > 0.0f && m.tooltip_t < 0.5f);

        // Reaches the delay.
        for (int i = 0; i < 30; ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.tooltip_t >= 0.5f);

        // Moving the pointer away resets it.
        m.hot = UINT16_MAX;
        m.update(1.0f / 60.0f);
        assert(m.tooltip_for == UINT16_MAX);

        // hide() clears the string and any pending timer.
        m.hot = host;
        m.update(1.0f / 60.0f);
        assert(m.tooltip_for == host);
        m.tooltip_hide(host);
        assert(m.tooltip_str[host] == nullptr);
        assert(m.tooltip_for == UINT16_MAX);
    }

    {
        // A gradient reaches the UI as a STYLE TEXTURE, so this is the whole
        // contract: bg_tex set, rounded shape, optional border. The rounded
        // SDF path samples it as the fill, which is what keeps the corner
        // radius, the per-side borders and the edge AA working.
        const TextureHandle t = TextureHandle::invalid(); // value only
        ui::WidgetStyle     s = ui::gradient_style(t, 0.12f, 0xFF88AAFF, 0.04f);
        assert(s.bg_tex.handle.id == t.handle.id);
        assert(s.shape == ui::shape_type::ROUNDED_RECT);
        assert(Near(s.corner_r, 0.12f));
        assert(s.border_color == 0xFF88AAFF && Near(s.border_width, 0.04f));
        // Defaults: no border (a bare gradient).
        ui::WidgetStyle bare = ui::gradient_style(t, 0.12f);
        assert(bare.border_color == 0 && Near(bare.border_width, 0.0f));
        assert(sizeof(ui::WidgetStyle) == 104); // fill_tex + stripe_pitch + stripe_color
        // fill_tex rides the same skinning field as thumb_tex/bg_tex. The
        // "no texture" test MUST be is_valid(), not handle.id != 0: a
        // zero-init WidgetStyle leaves the handle at SlotHandle::invalid()
        // (0xFFFFFFFF), so an `!= 0` test puts EVERY bar on the textured path
        // and the flat fill silently turns white. This asserts both halves.
        ui::WidgetStyle f0{};
        assert(!f0.fill_tex.is_valid());
        assert(f0.fill_tex.handle.id != 0); // the trap the check above avoids
        assert(!ui::gradient_style(t, 0.1f).fill_tex.is_valid()); // not a fill texture

        // set_style swaps at runtime and re-dirties measure() (content insets
        // come from the style, so the next frame's geometry must follow).
        ui::Manager m;
        m.init();
        u16 b = m.button(0.0f, 0.0f, 40.0f, 20.0f, "x", 0xFF111111, 0xFFFFFFFF, nullptr, UINT16_MAX);
        u8  st = m.register_style(ui::gradient_style(t, 0.2f));
        m.measure_dirty = false;
        m.set_style(b, st);
        assert(m.pool[b].style_id == st);
        assert(m.measure_dirty);
        // Out-of-range is refused rather than clamped into a valid style slot.
        m.pool[b].style_id = 0;
        m.set_style(b, 200);
        assert(m.pool[b].style_id == 0);
        m.set_style(9999, st); // invalid widget: no crash
    }

    // ─── Recycled slots do not inherit a material, an override string or a tooltip ──
    // One block for three arrays, because the rule that puts all three here is
    // the same: each is written by a SETTER that nothing else clears, and
    // clear() only moves `count`. Every array below is invisible in isolation
    // and loud in the app - mm_07 and freecell rebuild their whole UI every
    // frame, so "slot N had a texture last frame" is the common case.
    {
        ui::Manager m;
        m.init();

        // 1. widget_material[]: a stale pipeline changes which SHADER a widget
        //    draws through, in Pass 1 AND Pass 1.5. This had zero demo and zero
        //    test uses before this block, which is why nothing surfaced it.
        {
            Material mat{};
            mat.pipeline.handle.id = 7; // any valid id; no Renderer exists here
            const u16 img = m.image(0.0f, 0.0f, 40.0f, 20.0f);
            m.set_material(img, mat);
            assert(m.widget_material[img].is_valid());
            m.clear();
            const u16 plain = m.button(0.0f, 0.0f, 40.0f, 20.0f, "b", 0xFF111111, 0xFFFFFFFF, nullptr, UINT16_MAX);
            assert(plain == img);
            assert(!m.widget_material[plain].is_valid());
        }

        // 2. text_ext[]: when set it IS the display string (Widget::text is a
        //    48-byte inline buffer), so a stale one prints the previous owner's
        //    string on top of the new label. No factory clears it.
        static const char *const kLong = "a caller-owned string far longer than 48 bytes of inline buffer";
        m.clear(); // every sub-case starts from slot 0, so `== first` really means "same slot"
        {
            const u16 lbl = m.label(0.0f, 0.0f, "short", 0xFFFFFFFF, 0.3f);
            m.set_text_ext(lbl, kLong);
            assert(m.text_of(lbl) == kLong);
            m.clear();
            const u16 lbl2 = m.label(0.0f, 0.0f, "short", 0xFFFFFFFF, 0.3f);
            assert(lbl2 == lbl);
            assert(m.text_ext[lbl2] == nullptr);
            assert(std::strcmp(m.text_of(lbl2), "short") == 0);
        }

        // 3. tooltip_str[] / tooltip_delay[]: a stale string pops a tooltip the
        //    new widget never asked for. The delay was worse - it was never
        //    initialised ANYWHERE, so before alloc() grew a reset for it a
        //    default-constructed Manager held an indeterminate f32.
        m.clear();
        {
            const u16 host = m.button(0.0f, 0.0f, 40.0f, 20.0f, "h", 0xFF111111, 0xFFFFFFFF, NoopCb);
            m.tooltip(host, kLong, 9.0f);
            assert(m.tooltip_str[host] == kLong);
            assert(m.tooltip_delay[host] == 9.0f);
            m.clear();
            const u16 host2 = m.button(0.0f, 0.0f, 40.0f, 20.0f, "h", 0xFF111111, 0xFFFFFFFF, NoopCb);
            assert(host2 == host);
            assert(m.tooltip_str[host2] == nullptr);
            assert(m.tooltip_delay[host2] == ui::Manager::K_TOOLTIP_DEFAULT_DELAY);
        }

        // And init() must zero them too, for a Manager that never runs a frame:
        // `= Manager{}` leaves the two f32 arrays indeterminate. The values are
        // POISONED first on purpose - reading a fresh Manager's array and asserting
        // it is 0 proves nothing (it passed against a build where init() did not
        // write the field, because the stack happened to be zero), and it would be
        // flaky besides.
        {
            ui::Manager fresh;
            fresh.progressbar_stripe_t[0]            = 3.5f;
            fresh.tooltip_delay[0]                   = 9.0f;
            fresh.widget_material[0].pipeline.handle.id = 7;
            fresh.layout_type[0]                     = 1;
            fresh.text_ext[0]                        = "stale";
            fresh.tooltip_str[0]                     = "stale";
            fresh.combobox_anim_mode[0]              = ui::popup_anim_mode::GARAGE;
            fresh.init();
            assert(fresh.progressbar_stripe_t[0] == 0.0f);
            assert(fresh.tooltip_delay[0] == ui::Manager::K_TOOLTIP_DEFAULT_DELAY);
            assert(!fresh.widget_material[0].is_valid());
            assert(fresh.layout_type[0] == 0);
            assert(fresh.text_ext[0] == nullptr);
            assert(fresh.tooltip_str[0] == nullptr);
            assert(fresh.combobox_anim_mode[0] == ui::popup_anim_mode::SCALE_FADE);
        }
    }

    // ─── Renderer::sweep_t — the ring angle convention ───────────
    {
        // The ring shader sweeps atan(x, -y) clockwise from 12 o'clock, and
        // make_sweep_gradient_texture() lays its pixels out with THIS function.
        // Two files agreeing is not evidence, so pin the convention: local
        // units are -0.5..+0.5 with y growing DOWNWARD.
        const f32 top = Renderer::sweep_t(0.0f, -0.5f);   // 12 o'clock
        const f32 rgt = Renderer::sweep_t(0.5f, 0.0f);    // 3 o'clock
        const f32 bot = Renderer::sweep_t(0.0f, 0.5f);    // 6 o'clock
        const f32 lft = Renderer::sweep_t(-0.5f, 0.0f);   // 9 o'clock
        assert(Near(0.0f, top));
        assert(Near(0.25f, rgt));
        assert(Near(0.5f, bot));
        assert(Near(0.75f, lft));
        // The centre is degenerate (any angle) but must stay IN RANGE: a
        // negative value would wrap the texture's colour lookup.
        assert(Renderer::sweep_t(0.0f, 0.0f) >= 0.0f);
        assert(Renderer::sweep_t(0.0f, 0.0f) <= 1.0f);
        // Clockwise, not counter-clockwise: a quarter turn down-right is
        // between 3 and 6 o'clock, i.e. > 0.25.
        assert(Renderer::sweep_t(0.35f, 0.35f) > 0.25f);
    }

    // ─── striped fill: the phase walks in update(), render only reads it ──
    {
        ui::Manager m;
        m.init();
        ui::WidgetStyle st{};
        st.stripe_pitch = 14.0f;
        st.stripe_color = 0x30FFFFFF;
        u8  sid = m.register_style(st);
        u16 bar = m.progressbar(0.0f, 0.0f, 100.0f, 20.0f, 0.0f, UINT16_MAX, 1.0f, sid);
        assert(m.progressbar_stripe_t[bar] == 0.0f);
        // The walk is time-driven, not value-driven: the value never moves
        // here, yet the bands must drift.
        m.update(0.5f);
        const f32 after_1 = m.progressbar_stripe_t[bar];
        assert(after_1 > 0.0f && after_1 < st.stripe_pitch);
        // It wraps at the pitch, so the phase is NOT monotone - what must hold
        // is that it MOVED and stayed in range. (A bar left open for an hour
        // must not lose f32 precision, which is why it wraps instead of
        // accumulating.)
        m.update(0.5f);
        assert(m.progressbar_stripe_t[bar] != after_1);
        for (int k = 0; k < 400; ++k) {
            m.update(1.0f);
            assert(m.progressbar_stripe_t[bar] >= 0.0f && m.progressbar_stripe_t[bar] < st.stripe_pitch);
        }
        // dt == 0 advances nothing (the same noop guard the value uses).
        const f32 t0 = m.progressbar_stripe_t[bar];
        m.update(0.0f);
        assert(m.progressbar_stripe_t[bar] == t0);
        // pitch == 0 is the off switch: no stripe state is touched at all.
        ui::WidgetStyle plain{};
        u8        pid   = m.register_style(plain);
        u16       plain_bar = m.progressbar(0.0f, 0.0f, 100.0f, 20.0f, 0.0f, UINT16_MAX, 1.0f, pid);
        m.update(1.0f);
        assert(m.progressbar_stripe_t[plain_bar] == 0.0f);
        // The style grew by exactly the two new fields.
        assert(sizeof(ui::WidgetStyle) == 104);
        assert(plain.stripe_pitch == 0.0f && plain.stripe_color == 0); // off by default
    }

    // ─── A recycled slot does not inherit a stripe phase ─────────────
    // The phase is animation CONTINUITY, so this one is not a "is a model
    // attached?" predicate: it is animation state whose only writer is
    // update() - no factory, no setter, and (before this) not even init().
    // A striped bar landing on a live slot resumed the previous bar's phase, so
    // the bands visibly jumped on its first frame.
    {
        ui::Manager m;
        m.init();
        ui::WidgetStyle st{};
        st.stripe_pitch  = 14.0f;
        st.stripe_color = 0x30FFFFFF;
        const u8   sid = m.register_style(st);
        const u16  bar = m.progressbar(0.0f, 0.0f, 100.0f, 20.0f, 0.0f, UINT16_MAX, 1.0f, sid);
        m.update(0.37f);
        assert(m.progressbar_stripe_t[bar] > 0.0f);
        const f32 walked = m.progressbar_stripe_t[bar];
        m.clear();
        // Same slot, striped again: it starts at 0 rather than resuming `walked`.
        const u8  sid2 = m.register_style(st);
        const u16 bar2 = m.progressbar(0.0f, 0.0f, 100.0f, 20.0f, 0.0f, UINT16_MAX, 1.0f, sid2);
        assert(bar2 == bar);
        assert(m.progressbar_stripe_t[bar2] == 0.0f);
        assert(walked != 0.0f); // the first bar really had moved
    }

    // ─── Accordion: the height easing is pure ───
    {
        // dt == 0 must be a no-op (update(dt)'s contract), and the step has to
        // converge rather than creep.
        assert(ui::accordion_step(50.0f, 100.0f, 0.0f) == 50.0f);
        const f32 one = ui::accordion_step(0.0f, 100.0f, 1.0f / 60.0f);
        assert(one > 0.0f && one < 100.0f);
        f32 h = 0.0f;
        for (int i = 0; i < 600; ++i) {
            h = ui::accordion_step(h, 100.0f, 1.0f / 60.0f);
        }
        assert(h == 100.0f); // snaps on arrival
        f32 c = 100.0f;
        for (int i = 0; i < 600; ++i) {
            c = ui::accordion_step(c, 0.0f, 1.0f / 60.0f);
        }
        assert(c == 0.0f);
        // Never negative, even with an overshooting dt.
        assert(ui::accordion_step(10.0f, 0.0f, 100.0f) >= 0.0f);
    }

    // ─── Accordion: attach lands settled, and the target drives the animation ───
    {
        ui::Manager m;
        m.init();
        u16 head = m.button(10.0f, 10.0f, 200.0f, 30.0f, "Section", 0xFF3A3A3A, 0xFFFFFFFF, nullptr);
        u16 body = m.panel(10.0f, 200.0f, 200.0f, 120.0f, 0xFF444444, UINT16_MAX);

        m.accordion_attach(head, body, 120.0f, true);
        assert(m.is_accordion(head));
        // Attach must NOT start at 0: a section that animates open on the frame
        // it is built reads as a glitch.
        assert(m.accordion_is_settled(head));
        assert(m.accordion_drawn_h(head) == 120.0f);
        assert(m.accordion_is_open(head));
        // The content is now a CHILD of the header and clips its own children -
        // that is what makes the collapse reach everything inside it.
        assert(m.pool[body].parent == head);
        assert((m.pool[body].flags & ui::widget_flag::WF_CLIP) != 0);
        assert(m.accordion_parent_of(body) == head);
        assert(m.accordion_parent_of(head) == UINT16_MAX);

        // Closing animates, and only hides the content once there is nothing
        // left of it to see.
        m.accordion_set_open(head, false);
        assert(!m.accordion_is_open(head));
        assert(!m.accordion_is_settled(head));
        assert(m.accordion_drawn_h(head) == 120.0f); // still full this frame
        assert(m.is_visible(body));
        for (int i = 0; i < 400 && !m.accordion_is_settled(head); ++i) {
            m.update(1.0f / 60.0f);
        }
        assert(m.accordion_is_settled(head));
        assert(m.accordion_drawn_h(head) == 0.0f);
        assert(!m.is_visible(body));

        // Toggling a leaf-ish section with no content is refused, not a crash.
        u16 plain = m.button(10.0f, 60.0f, 200.0f, 30.0f, "plain", 0xFF3A3A3A, 0xFFFFFFFF, nullptr);
        assert(!m.is_accordion(plain));
        m.accordion_toggle(plain);
        assert(!m.is_accordion(plain));
    }

    // ─── Recycled slots do not inherit an accordion section ────────
// The same rule as the tree/grid models, with a sharper edge: `is_accordion`
// reads the CONTENT ID, so a plain button that lands on a former section's slot
// would clip its children to the section band AND toggle itself open on every
// tap - a page rebuild turns one button into two bugs at once.
{
    ui::Manager m;
    const u16 head = m.button(0, 0, 200, 30, "Section", 0, 0, nullptr, UINT16_MAX);
    const u16 body = m.panel(0, 30, 200, 120, 0, UINT16_MAX);
    m.accordion_attach(head, body, 120.0f, true);
    assert(m.is_accordion(head));
    m.clear();
    const u16 fresh_btn = m.button(0, 0, 200, 30, "Plain", 0, 0, nullptr, UINT16_MAX);
    const u16 fresh_body = m.panel(0, 30, 200, 120, 0, UINT16_MAX);
    assert(fresh_btn == head);
    assert(fresh_body == body);
    assert(!m.is_accordion(fresh_btn));
    assert(m.accordion_parent_of(fresh_body) == UINT16_MAX);
    assert(m.accordion_drawn_h(fresh_btn) == 0.0f);
}

// ─── Tap on a header TOGGLES its section ────────────────────────
// A section is a button, and a button is the one widget the user is expected
// to press: if the tap does not toggle, the page is a dead row of headers.
// Injecting the tap is one Select action with a position (the same shape
// listbox_tests uses), so this needs no Renderer and no window.
{
    ui::Manager m;
    m.init();
    const u16 head = m.button(0, 0, 300, 30, "first section (open)", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX);
    const u16 body = m.panel(0, 30, 300, 120, 0xFF1D1D1D, UINT16_MAX);
    m.accordion_attach(head, body, 120.0f, true);
    assert(m.accordion_is_open(head));

    InputState in;
    in.init();
    in.action_count = 1;
    in.actions[0]  = InputAction::Select;
    in.action_x    = 150.0f; // center of the header
    in.action_y    = 15.0f;
    m.handle(in);
    assert(!m.accordion_is_open(head));

    InputState in2;
    in2.init();
    in2.action_count = 1;
    in2.actions[0]  = InputAction::Select;
    in2.action_x    = 150.0f;
    in2.action_y    = 15.0f;
    m.handle(in2);
    assert(m.accordion_is_open(head));

    // A tap on the CONTENT is not a toggle: the content is a child of the
    // header, so a sloppy hit test would close the section when the user meant
    // to press a row inside it.
    InputState in3;
    in3.init();
    in3.action_count = 1;
    in3.actions[0]  = InputAction::Select;
    in3.action_x    = 150.0f;
    in3.action_y    = 60.0f;
    m.handle(in3);
    assert(m.accordion_is_open(head));

    // And it REOPENS. update() clears the content's visibility once a section
    // settles closed; if the reopen path does not set it back, the height
    // animates with nothing inside it - "closes fine, will not open again".
    m.accordion_set_open(head, false);
    for (int i = 0; i < 400 && !m.accordion_is_settled(head); ++i) {
        m.update(1.0f / 60.0f);
    }
    assert(!m.is_visible(body));
    m.accordion_set_open(head, true);
    assert(m.is_visible(body));
    for (int i = 0; i < 400 && !m.accordion_is_settled(head); ++i) {
        m.update(1.0f / 60.0f);
    }
    assert(m.accordion_is_settled(head));
    assert(m.is_visible(body));
    assert(m.accordion_drawn_h(head) == 120.0f);
}

// ─── A clip is the INTERSECTION of every clipping ancestor ──────
// get_clip() returning the FIRST clipping ancestor looks fine until two
// containers clip. A ScrollView inside an Accordion section is itself a clipping
// container, so a row inside it resolved the ScrollView's full viewport and never
// saw the section's band: collapsing the section left the rows standing, which is
// exactly what the screenshot showed.
{
    ui::Manager m;
    m.init();
    // header (clips) > content (clips) > scroll view (clips) > row
    const u16 head = m.button(0, 0, 300, 30, "section", 0xFF3A3A3A, 0xFFFFFFFF, nullptr, UINT16_MAX);
    const u16 body = m.panel(0, 30, 300, 120, 0xFF1D1D1D, UINT16_MAX);
    m.accordion_attach(head, body, 120.0f, true);
    const u16 sv   = m.scrollview(10, 10, 280, 100, body);
    const u16 row  = m.button(0, 0, 260, 40, "row", 0xFF333333, 0xFFFFFFFF, nullptr, sv);
    m.hit_test(0.0f, 0.0f); // rebuild the abs cache the clip reads

    i16  cx = 0, cy = 0;
    u16 cw = 0, ch = 0;
    assert(m.get_clip(row, cx, cy, cw, ch));
    // Open: the row's clip is the ScrollView viewport INSIDE the section band.
    const f32 open_h = static_cast<f32>(ch);
    assert(open_h > 0.0f && open_h <= 100.0f);

    m.accordion_set_open(head, false);
    for (int i = 0; i < 400 && !m.accordion_is_settled(head); ++i) {
        m.update(1.0f / 60.0f);
    }
    assert(m.accordion_is_settled(head));
    m.hit_test(0.0f, 0.0f);
    assert(m.get_clip(row, cx, cy, cw, ch));
    // Collapsed: the intersection is empty. "Empty clip" must not degrade into
    // "no clip" - that would paint a collapsed section's rows at full height.
    assert(ch == 0);
    // And the subtree is EFFECTIVELY invisible, which is what the render passes
    // test. An empty clip is not enough on its own: a pass that draws its own
    // primitive (the ScrollView's scrollbar) outlives the rect it was clipped to,
    // so a collapsed section used to leave its bar floating on the page.
    assert(!m.is_visible(body));
    assert(!m.is_visible(sv));
    assert(!m.is_visible(row));
    assert(m.is_visible(head));
}

return 0;
}

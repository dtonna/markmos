// Image widget tests — factory defaults, set_material / set_style, tap is
// inert (no callback, no state), Click event still emitted (same as buttons).
// Plain main() + assert(), no framework.
// Headless: factory + handle() are pure CPU. The TEXTURED draw needs a
// Material + Renderer and is covered by the mm_07 `I` (Images) page - which
// used to be cited as "the D6 screenshot", and D6 has never created an image.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>

static InputState FreshInput() noexcept {
    InputState in;
    in.init();
    return in;
}

static void Tap(InputState& in, f32 x, f32 y) noexcept {
    in.action_count = 1;
    in.actions[0] = InputAction::Select;
    in.action_x = x;
    in.action_y = y;
}

int main() {
    // ─── Factory defaults ───
    {
        ui::Manager m;
        m.init();
        u16 img = m.image(10.0f, 20.0f, 96.0f, 64.0f);
        assert(img != UINT16_MAX);
        assert(m.pool[img].type == (u8)ui::widget_type::IMAGE);
        assert(m.pool[img].frame.w == 96.0f && m.pool[img].frame.h == 64.0f);
        assert(m.pool[img].bg_color == 0xFFFFFFFF); // white tint = texture as-is
        assert(m.pool[img].flags & ui::WF_VISIBLE);
        assert(m.pool[img].on_click == nullptr);
    }

    // ─── Tap is inert: no state change, no crash, Click emitted ───
    {
        ui::Manager m;
        m.init();
        u16 img = m.image(10.0f, 20.0f, 96.0f, 64.0f);
        InputState pre = FreshInput();
        pre.mouse_x = 58.0f;
        pre.has_pointer = true; // a simulated pointer must declare itself
        pre.mouse_y = 52.0f;
        m.handle(pre);
        ui::UiEvent drain;
        while (m.poll_event(drain)) {
        }
        InputState t = FreshInput();
        t.mouse_x = 58.0f;
        t.has_pointer = true; // a simulated pointer must declare itself
        t.mouse_y = 52.0f;
        Tap(t, 58.0f, 52.0f);
        m.handle(t);
        assert(m.was_clicked(img));
        assert(m.event_pending() == 1);
        assert(m.poll_event(drain) && drain.type == ui::ui_event_type::CLICK && drain.id == img);
        assert(m.event_pending() == 0);
    }

    // ─── set_material swaps the PIPELINE, not just the tint ───
    // This array had ZERO uses outside a unit test for the life of the repo,
    // which is why widget_material[] went un-reset in alloc() through three
    // "recycled slot" fixes: nothing drew a material widget, so nothing could
    // surface a bug in the path. The mm_07 `I` page is what changed that.
    {
        ui::Manager m;
        m.init();
        u16 img = m.image(0.0f, 0.0f, 64.0f, 64.0f);
        // No material yet: the "no texture" test MUST be is_valid(), not
        // handle.id != 0, because invalid() is 0xFFFFFFFF - a zero-init array
        // leaves id != 0 TRUE and puts the widget on the textured path with a
        // bogus handle.
        assert(!m.widget_material[img].is_valid());
        assert(m.widget_material[img].pipeline.handle.id == 0xFFFFFFFFu);

        Material mat{};
        mat.pipeline.handle.id = 7; // any valid id; no Renderer exists here
        m.set_material(img, mat);
        assert(m.widget_material[img].is_valid());
        assert(m.widget_material[img].pipeline.handle.id == 7);
        // The tint is untouched: a material is a pipeline, not a colour.
        assert(m.pool[img].bg_color == 0xFFFFFFFF);

        // set_material on an invalid widget must not write out of bounds.
        m.set_material(9999, mat);
        m.set_material(UINT16_MAX, mat);
    }

    // ─── set_style: content insets come from the style, so measure() re-runs ───
    {
        ui::Manager m;
        m.init();
        u16 img = m.image(0.0f, 0.0f, 64.0f, 64.0f, UINT16_MAX, 0);
        assert(m.pool[img].style_id == 0);
        m.measure_dirty = false;
        u8 st = m.register_style(ui::gradient_style(TextureHandle::invalid(), 0.2f));
        m.set_style(img, st);
        assert(m.pool[img].style_id == st);
        assert(m.measure_dirty); // the next frame's geometry must follow
        // Out-of-range is REFUSED, not clamped into a valid style slot: a
        // clamped id would silently keep the old style and look like a no-op.
        m.pool[img].style_id = 0;
        m.set_style(img, 200);
        assert(m.pool[img].style_id == 0);
        m.set_style(9999, st); // invalid widget: no crash
    }

    // ─── Recycled slots: neither the material nor the tint survives ───
    {
        ui::Manager m;
        m.init();
        Material    mat{};
        mat.pipeline.handle.id = 7;
        u16    img = m.image(0.0f, 0.0f, 64.0f, 64.0f);
        m.set_material(img, mat);
        m.set_style(img, 1);
        assert(m.widget_material[img].is_valid());
        m.clear();
        u16 next = m.image(0.0f, 0.0f, 64.0f, 64.0f);
        assert(next == img); // the same slot came back
        // The stale pipeline is the worst of the leaks: it changes which SHADER
        // the widget draws through, in Pass 1 AND Pass 1.5.
        assert(!m.widget_material[next].is_valid());
        // The factory's white tint is its own again (a stale tint would show a
        // recycled image as whatever colour the previous owner asked for).
        assert(m.pool[next].bg_color == 0xFFFFFFFF);
        assert(m.pool[next].style_id == 0);
    }

    printf("[image] all tests passed\n");
    return 0;
}

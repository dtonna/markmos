// ListBox widget tests — metric derivation, selection, wheel/thumb/body
// scroll, keyboard nav. Plain main() + assert(), no framework.
// Headless: handle() is pure CPU; metrics are seeded via the same pure
// compute_listbox_metrics() that render() uses (line_height 48 = baked
// font representative — a test input, not production magic).
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cmath>

static const char* kItems[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
};
static constexpr u8 kCount = 16;
static constexpr f32 kLineH = 48.0f;

static void NoopLb(u16, void *) noexcept {}

// Counts on_click, so the keyboard assertions can prove a row CHANGE fired the
// app and not just moved the stored index.
static int g_lb_fires = 0;
static void FireLb(u16, void *) noexcept {
    ++g_lb_fires;
}

// Row-renderer probe: records what the engine handed the callback.
// RowCallback is a plain function pointer, so the counters are file-scope.
static int    g_row_calls = 0;
static int    g_row_last  = -1;
static f32  g_row_w     = 0.0f;
static void RowProbe(u16, Renderer &, SpriteBatch &, f32, f32, f32 row_w, f32 row_h, int item, void *user) noexcept {
    ++g_row_calls;
    g_row_last = item;
    g_row_w    = row_w;
    if (user != reinterpret_cast<void *>(0x1234)) {
        g_row_calls = -999; // the user pointer must survive the round trip
    }
    (void)row_h;
}

static bool Near(f32 a, f32 b) noexcept {
    return __builtin_fabsf(a - b) < 0.001f;
}

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

// Mirror of render(): seed the header cache from the pure derivation.
static ui::ListboxMetrics Seed(ui::Manager& m, u16 lb, f32 h) noexcept {
    ui::ListboxMetrics mt = ui::Manager::compute_listbox_metrics(h, kLineH, 1.0f);
    m.listbox_metrics[lb] = mt;
    return mt;
}

static void NoopCb(u16, void *) noexcept {}

static void Settle(ui::Manager &m) noexcept {
    for (int i = 0; i < 120; ++i) m.update(0.016f);
}

static void NoopGrid(u16, void *) noexcept {}
static const ui::GridColumn kCols[] = {
    {"Move", 0.0f, ui::text_align::LEFT, true},
    {"Time", 80.0f, ui::text_align::RIGHT, true},
    {"Score", 0.0f, ui::text_align::RIGHT, false},
    {"Tag", 60.0f, ui::text_align::LEFT, false},
};
static u16 MakeGrid(ui::Manager &m, f32 w = 300.0f) {
    const u16 id = m.listbox(10, 10, w, 200, nullptr, 0, NoopGrid, UINT16_MAX);
    m.grid_attach(id, kCols, 4);
    return id;
}

int main() {
    // ─── Pure derivation chain ───
    {
        ui::ListboxMetrics mt = ui::Manager::compute_listbox_metrics(280.0f, 50.0f, 1.0f);
        assert(Near(mt.text_scale, 0.45f));
        assert(Near(mt.text_h, 22.5f));
        assert(Near(mt.row_h, 30.5f)); // 22.5 + 2*4
        assert(mt.visible == 9);       // 280 / 30.5
        assert(Near(mt.scrollbar_w, 30.5f * 0.36f));
        assert(Near(mt.indent, 30.5f * 0.28f));
        // The ratio is PUBLIC: a RowCallback gets row_h but not scrollbar_w, so
        // right-aligned row content insets by K_BAR_RATIO * row_h itself. If the
        // constant stops matching the derivation this is where it shows.
        assert(Near(mt.scrollbar_w, ui::ListboxMetrics::K_BAR_RATIO * mt.row_h));
        // ui_scale flows through: 2x window scale doubles everything
        ui::ListboxMetrics mt2 = ui::Manager::compute_listbox_metrics(560.0f, 50.0f, 2.0f);
        assert(Near(mt2.row_h, 61.0f));
        assert(mt2.visible == 9);
        // invalid inputs → zero (guarded downstream)
        ui::ListboxMetrics bad = ui::Manager::compute_listbox_metrics(0.0f, 0.0f, 0.0f);
        assert(bad.row_h == 0.0f);
    }

    // ─── Factory defaults ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
        assert(lb != UINT16_MAX);
        assert(m.get_listbox_selected(lb) == -1);
        assert(m.listbox_scroll[lb] == 0.0f);
        assert(m.listbox_count[lb] == kCount);
        assert(m.pool[lb].flags & ui::WF_CLIP);
        assert(m.pool[lb].flags & ui::WF_FOCUSABLE);
        assert(m.pool[lb].type == (u8)ui::widget_type::LISTBOX);
    }

    // ─── Tap selects row (geometry from metrics) ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
        ui::ListboxMetrics mt = Seed(m, lb, 280.0f);
        InputState in = FreshInput();
        Tap(in, 100.0f, 2.0f * mt.row_h + mt.row_h * 0.5f); // row 2 center
        m.handle(in);
        assert(m.get_listbox_selected(lb) == 2);
        assert(m.was_clicked(lb));
    }

    // ─── Tap with scroll offset ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 140.0f, kItems, kCount, NoopLb);
        ui::ListboxMetrics mt = Seed(m, lb, 140.0f);
        m.listbox_scroll[lb] = 6.0f;
        InputState in = FreshInput();
        Tap(in, 100.0f, 4.0f * mt.row_h + mt.row_h * 0.5f); // 4th visible row
        m.handle(in);
        assert(m.get_listbox_selected(lb) == 10); // 6 + 4
    }

    // ─── Wheel scroll + clamp ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
        ui::ListboxMetrics mt = Seed(m, lb, 280.0f);
        f32 maxsc = static_cast<f32>(kCount - mt.visible);
        InputState in = FreshInput();
        in.mouse_x = 100.0f;
        in.has_pointer = true; // a simulated pointer must declare itself
        in.mouse_y = 100.0f;
        in.mouse_scroll_dy = -1.0f; // wheel down → later rows
        m.handle(in);
        assert(Near(m.listbox_scroll[lb], 3.0f));
        InputState in2 = FreshInput();
        in2.mouse_x = 100.0f;
        in2.has_pointer = true; // a simulated pointer must declare itself
        in2.mouse_y = 100.0f;
        in2.mouse_scroll_dy = -10.0f; // clamp at maxscroll
        m.handle(in2);
        assert(Near(m.listbox_scroll[lb], maxsc));
        InputState in3 = FreshInput();
        in3.mouse_x = 100.0f;
        in3.has_pointer = true; // a simulated pointer must declare itself
        in3.mouse_y = 100.0f;
        in3.mouse_scroll_dy = 10.0f; // wheel up past 0
        m.handle(in3);
        assert(Near(m.listbox_scroll[lb], 0.0f));
    }

    // ─── Wheel outside box ignored ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
        Seed(m, lb, 280.0f);
        InputState in = FreshInput();
        in.mouse_x = 500.0f;
        in.has_pointer = true; // a simulated pointer must declare itself
        in.mouse_y = 500.0f;
        in.mouse_scroll_dy = -1.0f;
        m.handle(in);
        assert(m.listbox_scroll[lb] == 0.0f);
    }

    // ─── Keyboard nav + autoscroll ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
        ui::ListboxMetrics mt = Seed(m, lb, 280.0f);
        m.focus_id = lb;
        InputState in = FreshInput();
        in.action_count = 1;
        in.actions[0] = InputAction::MenuDown;
        m.handle(in);
        assert(m.get_listbox_selected(lb) == 0); // from -1 → first
        assert(m.focus_id == lb);                // focus stays (consumed)
        for (int k = 0; k < 12; ++k) {
            InputState ik = FreshInput();
            ik.action_count = 1;
            ik.actions[0] = InputAction::MenuDown;
            m.handle(ik);
        }
        assert(m.get_listbox_selected(lb) == 12);
        // autoscroll: 12 - visible + 1
        assert(Near(m.listbox_scroll[lb], static_cast<f32>(12 - mt.visible + 1)));
        InputState up = FreshInput();
        up.action_count = 1;
        up.actions[0] = InputAction::MenuUp;
        m.handle(up);
        assert(m.get_listbox_selected(lb) == 11);
    }

    // ─── PageDown / PageUp move a VIEWPORT, and autoscroll with it ───
    // A page is ListboxMetrics::visible - 1: one screenful minus the row you land
    // on, so the selection is still on screen without the autoscroll having to
    // rescue it. Derived from the seeded metrics rather than hardcoded, so a
    // change to the row-height derivation moves the expectation with it.
    // Nothing selected yet: the page anchors at the far end and jumps, so the
    // FIRST PageDown does something (an arrow from -1 lands on row 0, which is
    // right for a step and wrong for a page).
    {
        ui::Manager m;
        m.init();
        // A SHORT viewport on purpose: with 16 rows and a tall list the first two
        // pages would already hit the end (page 8 -> 16 > 15), so the test could
        // not tell "jumped a page" from "clamped". 140pt gives visible 4, page 3,
        // so several real pages fit before the clamp does.
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 140.0f, kItems, kCount, NoopLb);
        ui::ListboxMetrics mt = Seed(m, lb, 140.0f);
        m.focus_id = lb;
        const int page = mt.visible - 1;
        assert(page == 3);

        auto key = [&m](InputAction a) {
            InputState in = FreshInput();
            in.action_count = 1;
            in.actions[0]   = a;
            m.handle(in);
        };

        key(InputAction::MenuPageDown);
        assert(m.get_listbox_selected(lb) == page);
        assert(m.focus_id == lb); // consumed, focus did not walk

        key(InputAction::MenuPageDown);
        assert(m.get_listbox_selected(lb) == page * 2);

        key(InputAction::MenuPageDown);
        assert(m.get_listbox_selected(lb) == page * 3);
        // The jump must be ON SCREEN or it is a lie - the same requirement the
        // arrow autoscroll has.
        assert(Near(m.listbox_scroll[lb], static_cast<f32>(page * 3 - mt.visible + 1)));

        key(InputAction::MenuPageUp);
        assert(m.get_listbox_selected(lb) == page * 2);

        // Clamped at both ends, never wrapped: 3 pages down is 9, five more would
        // be 24.
        for (int i = 0; i < 5; ++i) {
            key(InputAction::MenuPageDown);
        }
        assert(m.get_listbox_selected(lb) == static_cast<int>(kCount) - 1);
        assert(Near(m.listbox_scroll[lb], static_cast<f32>(kCount - mt.visible)));
        for (int i = 0; i < 9; ++i) {
            key(InputAction::MenuPageUp);
        }
        assert(m.get_listbox_selected(lb) == 0);
        assert(Near(m.listbox_scroll[lb], 0.0f));

        // The key is FLAGGED, or a host with its own PageDown binding double-fires.
        InputState probe = FreshInput();
        probe.action_count = 1;
        probe.actions[0]   = InputAction::MenuPageDown;
        m.handle(probe);
        assert(m.nav_consumed_this_frame());
    }

    // ─── Thumb drag ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
        ui::ListboxMetrics mt = Seed(m, lb, 280.0f);
        f32 maxsc = static_cast<f32>(kCount - mt.visible);
        // thumb at scroll 0 spans y 0..thumb_h, x w-bar..w
        f32 th = 280.0f * mt.visible / kCount;
        if (th < mt.row_h) th = mt.row_h;
        InputState in = FreshInput();
        in.touch.on_touch_down(0, 240.0f - mt.scrollbar_w * 0.5f, th * 0.5f);
        in.touch.fingers[0].type = GestureType::Drag; // white-box: skip swipe classification
        m.handle(in); // grab (thumb mode)
        f32 target_y = 280.0f * 0.75f;
        in.touch.on_touch_move(0, 240.0f - mt.scrollbar_w * 0.5f, target_y);
        m.handle(in); // drag
        f32 want = (target_y - th * 0.5f) / (280.0f - th) * maxsc;
        if (want > maxsc) want = maxsc;
        assert(Near(m.listbox_scroll[lb], want));
        in.touch.on_touch_up(0);
        m.handle(in); // release
        assert(Near(m.listbox_scroll[lb], want));
    }

    // ─── Body drag ───
    {
        ui::Manager m;
        m.init();
        u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
        ui::ListboxMetrics mt = Seed(m, lb, 280.0f);
        InputState in = FreshInput();
        in.touch.on_touch_down(0, 100.0f, 140.0f); // center, not on thumb
        in.touch.fingers[0].type = GestureType::Drag; // white-box: skip swipe classification
        m.handle(in); // grab (body mode)
        f32 dy = -2.0f * mt.row_h; // drag up 2 rows
        in.touch.on_touch_move(0, 100.0f, 140.0f + dy);
        m.handle(in);
        assert(Near(m.listbox_scroll[lb], 2.0f));
    }

    // ─── clear() resets the style pool (no leak → no MAX_STYLES abort) ───
    {
        ui::Manager m;
        m.init();
        for (int round = 0; round < 70; ++round) {
            // Simulate a page rebuild: clear + re-register gallery styles.
            m.clear();
            ui::WidgetStyle s{};
            s.bg_tex = TextureHandle::invalid();
            s.shape = ui::shape_type::ROUNDED_RECT;
            m.register_style(s); // must never abort
            assert(m.style_count == 2); // default 0 + gallery 1, stable
            u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
            assert(lb != UINT16_MAX);
        }
    }

    // ─── TreeView: the flatten is the whole new idea, and it is pure ───
    // A tree is a LISTBOX plus a node model: scroll, selection, keyboard and the
    // row callback all come from the listbox. Only the flatten is new - deciding
    // which nodes are visible given which ancestors are open.
    {
        using ui::Manager;
        using ui::TreeNode;
        //   0 root (children 1,2,3)   2 -> child 4   5 second root
        static const TreeNode kTree[] = {
            {-1, 1, 3, ui::TREE_HAS_CHILDREN}, // 0
            {0, 0, 0, 0},                     // 1
            {0, 4, 1, ui::TREE_HAS_CHILDREN},  // 2
            {0, 0, 0, 0},                     // 3
            {2, 0, 0, 0},                     // 4 (grandchild)
            {-1, 0, 0, 0},                    // 5
        };
        constexpr u32 kN = sizeof(kTree) / sizeof(kTree[0]);
        u64            bits[4] = {0, 0, 0, 0};

        // Closed: only the two roots.
        assert(ui::tree_visible_count(kTree, kN, bits) == 2);
        u32 rows[64];
        assert(ui::tree_flatten(kTree, kN, bits, rows, 64) == 2);
        assert(rows[0] == 0 && rows[1] == 5);

        // Open root 0: its three children join; node 4 stays hidden under node 2.
        bits[0] |= 1ull;
        assert(ui::tree_visible_count(kTree, kN, bits) == 5);
        assert(ui::tree_flatten(kTree, kN, bits, rows, 64) == 5);
        assert(rows[0] == 0 && rows[1] == 1 && rows[2] == 2 && rows[3] == 3 && rows[4] == 5);
        // Node 4 is under a CLOSED node, so it has no row at all. This mapping is
        // what a row callback gets wrong when it indexes the node array directly.
        assert(!ui::tree_node_visible(kTree, kN, bits, 4));

        // Open node 2: the grandchild appears. Order is INDEX order, not a
        // depth-first walk - siblings are stored as a contiguous index range, so
        // node 4 (a child of 2) cannot sit between 2 and its sibling 3. An app
        // therefore lays its array out in the order it wants to SEE rows.
        bits[0] |= 1ull << 2;
        assert(ui::tree_visible_count(kTree, kN, bits) == 6);
        assert(ui::tree_flatten(kTree, kN, bits, rows, 64) == 6);
        for (u32 r = 0; r < 6; ++r) {
            assert(rows[r] == r);
        }

        // Closing root 0 hides its whole subtree, grandchild included.
        bits[0] &= ~(1ull | (1ull << 2));
        assert(ui::tree_visible_count(kTree, kN, bits) == 2);

        // Bad input is refused, not trusted: an out-of-range node and a null model
        // are both invisible, and a cyclic parent chain terminates instead of
        // hanging the frame.
        assert(!ui::tree_node_visible(kTree, kN, bits, 99));
        assert(!ui::tree_node_visible(nullptr, kN, bits, 0));
        assert(ui::tree_visible_count(nullptr, kN, bits) == 0);
        assert(ui::tree_visible_count(kTree, kN, nullptr) == 0);
        TreeNode cyc[2] = {{-1, 0, 1, ui::TREE_HAS_CHILDREN}, {0, 0, 1, ui::TREE_HAS_CHILDREN}};
        (void)ui::tree_node_visible(cyc, 2, bits, 1); // bounded: returns, no hang

        // Expander strip: a leaf has no affordance, and a deeper row's strip
        // starts further right so a tap at a shallower row's x misses it.
        assert(Manager::tree_hit_expander(0, true, 4.0f, 20.0f));
        assert(Manager::tree_hit_expander(0, true, 4.0f + Manager::K_TREE_EXPANDER_W, 20.0f));
        assert(!Manager::tree_hit_expander(0, true, 3.0f, 20.0f));
        assert(!Manager::tree_hit_expander(0, false, 4.0f, 20.0f));
        const f32 d0 = Manager::tree_expander_x(0);
        const f32 d1 = Manager::tree_expander_x(1);
        assert(d1 > d0 + Manager::K_TREE_EXPANDER_W);
        assert(!Manager::tree_hit_expander(1, true, d0, 20.0f));
        assert(Manager::tree_hit_expander(1, true, d1, 20.0f));
    }

    // ─── set_row_renderer: app draws the row, engine keeps the plumbing ───
    {
        ui::Manager m;
        m.init();
        static const char *items[] = {"one", "two", "three"};
        u16 lb = m.listbox(10.0f, 10.0f, 120.0f, 90.0f, items, 3, nullptr, UINT16_MAX, 0);
        // Default: no row callback.
        assert(m.listbox_row_cb[lb] == nullptr);
        // Setter round-trips, and an invalid id is a no-op rather than a crash.
        m.set_row_renderer(lb, RowProbe, reinterpret_cast<void *>(0x1234));
        assert(m.listbox_row_cb[lb] == RowProbe);
        assert(m.listbox_row_user[lb] == reinterpret_cast<void *>(0x1234));
        // Invalid id: the guard makes this a no-op (reading the array back
        // would itself be the out-of-bounds bug this guards against).
        m.set_row_renderer(9999, RowProbe, nullptr);
        // Clearing restores the default row rendering.
        m.set_row_renderer(lb, nullptr, nullptr);
        assert(m.listbox_row_cb[lb] == nullptr);
    }

    // ─── Recycled slots do not inherit a model ─────────────────────
// clear() moves `count` and touches no per-widget array, so every slot comes
// back holding the PREVIOUS widget's model. mm_07/freecell rebuild their UI
// every frame, so "slot N had a tree last frame" is the COMMON case. Without the
// reset in alloc(), this listbox answers "is there a tree?" with yes: its rows
// indent, and a tap in the left margin toggles a branch that does not exist.
{
    ui::Manager m;
    using ui::TreeNode;
    static const TreeNode nodes[] = {{-1, 1, 1, ui::TREE_HAS_CHILDREN}, {0, 0, 0, 0}};
    const u16 tree = m.listbox(0, 0, 200, 200, kItems, kCount, NoopLb, UINT16_MAX);
    m.set_tree(tree, nodes, 2);
    assert(m.tree_count[tree] == 2);
    m.clear();
    const u16 plain = m.listbox(0, 0, 200, 200, kItems, kCount, NoopLb, UINT16_MAX);
    assert(plain == tree); // same slot came back
    assert(m.tree_nodes[plain] == nullptr);
    assert(m.tree_count[plain] == 0);
    assert(!m.tree_tap_is_expander(plain, 4.0f, 20.0f));
    // And the expansion bitset, which is 4KB of stale state per slot.
    for (u32 b = 0; b < 4; ++b) {
        assert(m.tree_expanded[plain][b] == 0);
    }
}


// ─── A recycled slot does not inherit a row renderer ──────────────────
// The bug: Treeviews sets listbox_row_cb via set_row_renderer(); clear() only
// resets the pool bookkeeping (count/freelist), not per-widget arrays. When
// alloc() recycles the same slot for a plain listbox, the stale row callback
// would fire instead of the default one-string row, making item text invisible
// while selection/scrollbar still worked.
{
    ui::Manager m;
    m.init();
    static const char *items[] = {"a", "b", "c"};
    const u16 tree = m.listbox(0, 0, 200, 200, items, 3, NoopLb, UINT16_MAX);
    m.set_row_renderer(tree, RowProbe, reinterpret_cast<void *>(0x1234));
    assert(m.listbox_row_cb[tree] == RowProbe);
    assert(m.listbox_row_user[tree] == reinterpret_cast<void *>(0x1234));
    m.clear();
    const u16 plain = m.listbox(0, 0, 200, 200, items, 3, NoopLb, UINT16_MAX);
    assert(plain == tree); // same slot recycled
    assert(m.listbox_row_cb[plain] == nullptr);  // must not inherit the tree's renderer
    assert(m.listbox_row_user[plain] == nullptr);
}

// ─── Combobox factory sets all interaction state (stale open/filter cleared) ───
// A recycled combobox slot must not remember a previous popup's open/filter/highlight.
{
    ui::Manager m;
    m.init();
    u16 cb = m.combobox(0, 0, 240, 40, kItems, kCount, true, NoopCb);
    Seed(m, cb, 40.0f);
    // Drive it to a non-default state.
    InputState open = FreshInput();
    Tap(open, 30.0f, 40.0f);
    m.handle(open);
    Settle(m);
    assert(m.is_combobox_open(cb));
    // Now recycle: clear + rebuild.
    m.clear();
    u16 cb2 = m.combobox(0, 0, 240, 40, kItems, kCount, false, NoopCb);
    Seed(m, cb2, 40.0f);
    assert(cb2 == cb); // same slot
    assert(!m.is_combobox_open(cb2));
    assert(!m.combobox_filtering[cb2]);
    assert(m.combobox_hl[cb2] == -1);
    assert(m.combobox_selected[cb2] == -1);
    assert(m.combobox_scroll[cb2] == 0.0f);
}

// ─── Grid model is fully cleared on recycle ─────────────────────────
// Column model + sort state must not leak to an unrelated widget.
{
    ui::Manager m;
    const u16 g = MakeGrid(m);
    m.grid_toggle_sort(g, 0);
    m.grid_header_h[g] = 20.0f;
    m.clear();
    const u16 fresh = m.listbox(0, 0, 200, 100, nullptr, 0, nullptr, UINT16_MAX);
    assert(fresh == g);
    assert(!m.is_grid(fresh));
    assert(m.grid_header_h[fresh] == 0.0f);
    assert(m.grid_sorted_col(fresh) == UINT8_MAX);
    assert(m.grid_row_source(fresh, 0) == 0);
    assert(m.row_area_top(fresh, 50.0f) == 50.0f);
}

// ─── Accordion model is fully cleared on recycle ────────────────────
// A plain widget must not remember a previous section's content/target height.
{
    ui::Manager m;
    m.init();
    u16 head = m.button(0, 0, 200, 30, "x", 0, 0, nullptr);
    u16 body = m.panel(0, 30, 200, 100, 0, UINT16_MAX);
    m.accordion_attach(head, body, 100.0f, true);
    assert(m.acc_content[head] == body);
    assert(m.acc_target_h[head] == 100.0f);
    m.clear();
    u16 plain = m.button(0, 0, 200, 30, "y", 0, 0, nullptr);
    assert(plain == head); // same slot
    assert(m.acc_content[plain] == UINT16_MAX);
    assert(m.acc_target_h[plain] == 0.0f);
    assert(m.acc_drawn_h[plain] == 0.0f);
    assert(m.acc_content_h[plain] == 0.0f);
}

// ─── A focused listbox CLAIMS Up/Dn (and says so) ───────────────
// A host binding Up/Down to something else - a live tuner, a zoom - used to also
// fire on every press, because the engine consumed the key silently. mm_07 hit
// exactly that: the listbox page's own hint ("Up/Dn moves selection") also
// shrank the global font 0.1 per press, and the shrunken text survived every page
// switch, which reads as "the listbox lost its text". The flag is the only way a
// host can know.
//
// Note what is NOT flagged: FOCUS NAVIGATION (nothing focused yet) shares the
// same action, and the host must still get its key there - otherwise the first
// Tab would freeze the tuner forever.
{
    ui::Manager m;
    m.init();
    u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, NoopLb);
    Seed(m, lb, 280.0f);

    {
        InputState in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::FocusNext;
        m.handle(in);
        assert(m.focus_id == lb);          // focus moved onto the listbox
        assert(!m.nav_consumed_this_frame()); // ... and the host still owns the key
    }
    {
        InputState in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::MenuDown;
        m.handle(in);
        assert(m.nav_consumed_this_frame()); // now the WIDGET owns the ARROW
        assert(m.get_listbox_selected(lb) == 0);
        assert(m.focus_id == lb);            // and focus did not move away
    }
    // ...but Tab must still walk focus OFF it. This is the half that was missing:
    // the listbox branch used to match Tab too (it arrived as MenuDown), it moved
    // the selection, it CLAMPED at the last row and `continue`d - so the list was
    // a one-way door exactly like the ComboBox. Two actions, two owners: the
    // arrows belong to the widget, Tab belongs to the focus ring.
    {
        u16 after = m.button(300.0f, 0.0f, 80.0f, 40.0f, "next", 0xFF3A3A3A, 0xFFFFFFFF, NoopLb);
        InputState in = FreshInput();
        in.action_count = 1;
        in.actions[0]  = InputAction::FocusNext;
        m.handle(in);
        assert(m.focus_id == after);                    // walked off the list
        assert(m.get_listbox_selected(lb) == 0);        // selection untouched
        assert(!m.nav_consumed_this_frame());           // and the host kept the key
        // Shift-Tab comes back, still without disturbing the selection.
        InputState back = FreshInput();
        back.action_count = 1;
        back.actions[0]  = InputAction::FocusPrev;
        m.handle(back);
        assert(m.focus_id == lb);
        assert(m.get_listbox_selected(lb) == 0);
    }
}
    // Printed LAST, and only here. It used to sit ~165 lines earlier - before the
    // recycled-slot blocks - so reading stdout told you the file passed while a
    // later block was still about to abort.
    // ─── Home / End jump to the first / last row ───
    // Folded into the arrow branch's body rather than duplicated, so the events
    // and the autoscroll cannot drift apart: "pick a row, then fire and scroll"
    // is one contract, and a second copy of it is free to forget half.
    {
        ui::Manager m;
        m.init();
        g_lb_fires = 0;
        const u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, FireLb);
        Seed(m, lb, 280.0f);
        m.focus_id = lb;

        auto key = [&m](InputAction a) {
            InputState in = FreshInput();
            in.action_count = 1;
            in.actions[0]   = a;
            m.handle(in);
        };

        key(InputAction::MenuLast);
        assert(m.get_listbox_selected(lb) == static_cast<int>(kCount) - 1);
        assert(g_lb_fires == 1); // on_click fired, exactly like an arrow
        assert(m.nav_consumed_this_frame());
        assert(m.focus_id == lb);

        key(InputAction::MenuFirst);
        assert(m.get_listbox_selected(lb) == 0);
        assert(g_lb_fires == 2);
        assert(m.focus_id == lb); // focus never moved

        // Autoscroll follows the jump, not just the arrows: End has to bring the
        // last row into view, or the selection moves off-screen and the player
        // cannot see what they selected.
        assert(m.get_listbox_selected(lb) == 0); // back at the top
        key(InputAction::MenuLast);
        const f32 scrolled = m.listbox_scroll[lb];
        assert(scrolled > 0.0f);
        {
            const int                   vis = m.listbox_metrics[lb].visible > 0 ? m.listbox_metrics[lb].visible : 1;
            const ui::ListboxMetrics &lm  = m.listbox_metrics[lb];
            const int                   top = static_cast<int>(scrolled);
            assert(static_cast<int>(kCount) - 1 >= top && static_cast<int>(kCount) - 1 < top + vis);
            (void)lm;
        }
        key(InputAction::MenuFirst); // back to the top: silent, and scroll follows
        assert(m.get_listbox_selected(lb) == 0);
        assert(g_lb_fires == 4);
        assert(Near(m.listbox_scroll[lb], 0.0f));
    }

    // ─── Confirm activates the selection, and IS reported to the host ──
    // Enter (and Space - they share one action) means "act on the current
    // selection". It also has to set nav_consumed: the widget has taken the key,
    // so a host with its own Space/Enter binding must not also fire. That flag was
    // only ever set by the four arrow branches, which is the same bug the mm_07
    // font tuner hit with Up/Down.
    {
        ui::Manager m;
        m.init();
        g_lb_fires = 0;
        const u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, FireLb);
        Seed(m, lb, 280.0f);
        m.focus_id = lb;

        // Put the selection somewhere first, so "activate" has something to act on.
        InputState dn = FreshInput();
        dn.action_count = 1;
        dn.actions[0]   = InputAction::MenuDown;
        m.handle(dn);
        assert(m.get_listbox_selected(lb) == 0);
        g_lb_fires      = 0;

        InputState en = FreshInput();
        en.action_count = 1;
        en.actions[0]   = InputAction::Confirm;
        m.handle(en);
        assert(g_lb_fires == 1);              // on_click fired
        assert(m.get_listbox_selected(lb) == 0); // ...without moving the selection
        assert(m.nav_consumed_this_frame()); // ...and the host was told
        assert(m.focus_id == lb);
    }

    // ─── Escape on a ListBox does nothing ──
    // Recorded rather than implemented: clearing the selection on Escape is a
    // defensible reading, but it is a decision about what Escape MEANS to a list,
    // and no toolkit agrees on it. Better written down than guessed.
    {
        ui::Manager m;
        m.init();
        g_lb_fires = 0;
        const u16 lb = m.listbox(0.0f, 0.0f, 240.0f, 280.0f, kItems, kCount, FireLb);
        Seed(m, lb, 280.0f);
        m.focus_id = lb;
        InputState dn = FreshInput();
        dn.action_count = 1;
        dn.actions[0]   = InputAction::MenuDown;
        m.handle(dn);
        assert(m.get_listbox_selected(lb) == 0);
        g_lb_fires = 0; // the arrow fired on_click; only Escape is under test here

        InputState esc = FreshInput();
        esc.action_count = 1;
        esc.actions[0]   = InputAction::Pause;
        m.handle(esc);
        assert(m.get_listbox_selected(lb) == 0); // selection intact
        assert(g_lb_fires == 0);
        assert(!m.nav_consumed_this_frame());
    }

    printf("[listbox] all tests passed\n");
return 0;
}

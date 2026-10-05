// DataGrid tests — column geometry, the comparator, the sort order, and the
// Manager state (header model, sort state, visible→model mapping, row-area
// inset). Plain main() + assert(), no framework.
// Headless: everything here is pure CPU. The sort order and the column widths
// never need a Renderer, which is the point of keeping them out of the draw
// pass — a grid is a listbox, so its scroll/selection are already covered by
// listbox_tests.
#include "../ui/mm_ui.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cstring>

using namespace ui;

static const GridColumn kCols[] = {
    {"Move", 0.0f, text_align::LEFT, true},     // flex
    {"Time", 80.0f, text_align::RIGHT, true},   // fixed
    {"Score", 0.0f, text_align::RIGHT, false},  // flex, not sortable
    {"Tag", 60.0f, text_align::LEFT, false},    // fixed
};

static const GridRow kRows[] = {
    {{"12", "0:31", "204", "aa"}},
    {{"9", "1:02", "180", "bb"}},
    {{"100", "0:09", "12", "cc"}},
    {{"9", "0:44", "7", "dd"}},
    {{"31", "2:15", "999", "ee"}},
};
static constexpr u32 kRowCount = 5;

static void NoopGrid(u16, void *) noexcept {}

static u16 MakeGrid(ui::Manager &m, f32 w = 300.0f) {
    const u16 id = m.listbox(10, 10, w, 200, nullptr, 0, NoopGrid, UINT16_MAX);
    m.grid_attach(id, kCols, 4);
    return id;
}

int main() {
// ─── Column geometry ────────────────────────────────────────────
{
    // Fixed + flex: the two flexible columns share what the fixed ones leave.
    const GridMetrics m = compute_grid_metrics(kCols, 4, 300.0f);
    assert(m.columns == 4);
    assert(m.x[0] == 0.0f);
    assert(m.x[1] == 80.0f);   // after the flex column, not after 300
    assert(m.x[2] == 160.0f);
    assert(m.x[3] == 240.0f);
    assert(m.w[0] == 80.0f);   // (300 - 80 - 60) / 2 flexible columns
    assert(m.w[1] == 80.0f);
    assert(m.w[2] == 80.0f);
    assert(m.w[3] == 60.0f);   // fixed wins over the share
    assert(m.total_w == 300.0f);
}

// ─── Column geometry, degenerate inputs ─────────────────────────
{
    // No columns: a grid attached to nothing still has a usable width.
    const GridMetrics m0 = compute_grid_metrics(nullptr, 0, 200.0f);
    assert(m0.columns == 0);
    assert(m0.total_w == 200.0f);

    // Fixed widths wider than the grid: the flexible share clamps to 0 rather
    // than going negative (a negative width is a mirrored quad).
    const GridColumn wide[] = {{"A", 300.0f, text_align::LEFT, true}, {"B", 0.0f, text_align::LEFT, true}};
    const GridMetrics mw = compute_grid_metrics(wide, 2, 200.0f);
    assert(mw.w[0] == 300.0f);
    assert(mw.w[1] == 0.0f);

    // Over the column cap: truncated, never written past the arrays.
    const GridColumn many[10] = {};
    const GridMetrics mm = compute_grid_metrics(many, 10, 100.0f);
    assert(mm.columns == MAX_GRID_COLS);
}

// ─── Comparator ────────────────────────────────────────────────
{
    // Numbers compare as numbers: "9" < "12", which a string compare gets
    // backwards and makes a move-count column look broken.
    assert(grid_cell_cmp("9", "12") < 0);
    assert(grid_cell_cmp("100", "9") > 0);
    assert(grid_cell_cmp("42", "42") == 0);
    // Non-numeric falls back to text.
    assert(grid_cell_cmp("apple", "banana") < 0);
    assert(grid_cell_cmp("Banana", "apple") < 0); // case-sensitive, documented
    // A numeric column and a text column: text compare, never a parse of "".
    assert(grid_cell_cmp("", "0:31") < 0);
    // null is an empty cell, not a crash.
    assert(grid_cell_cmp(nullptr, nullptr) == 0);
    assert(grid_cell_cmp(nullptr, "a") < 0);
    assert(grid_cell_cmp("a", nullptr) > 0);
}

// ─── Sort order ────────────────────────────────────────────────
{
    u8 order[kRowCount] = {};
    // Move column ascending: 9, 9, 12, 31, 100.
    assert(grid_sort_order(kRows, kRowCount, 0, grid_sort::ASC, order, kRowCount) == kRowCount);
    assert(order[0] == 1);
    assert(order[1] == 3); // the second "9" keeps its model order (stable)
    assert(order[2] == 0);
    assert(order[3] == 4);
    assert(order[4] == 2);

    // Descending reverses the VALUES, not the tie order: rows 1 and 3 are both
    // "9", and a stable sort keeps 1 before 3 here too - flipping ties on every
    // tap is what makes a table unreadable.
    grid_sort_order(kRows, kRowCount, 0, grid_sort::DESC, order, kRowCount);
    assert(order[0] == 2);
    assert(order[1] == 4);
    assert(order[2] == 0);
    assert(order[3] == 1);
    assert(order[4] == 3);

    // NONE is identity — not "everything equal", which would scramble the rows.
    grid_sort_order(kRows, kRowCount, 0, grid_sort::NONE, order, kRowCount);
    for (u8 i = 0; i < kRowCount; ++i) {
        assert(order[i] == i);
    }

    // Score is a real numeric sort even with a text-ish neighbour column.
    grid_sort_order(kRows, kRowCount, 2, grid_sort::ASC, order, kRowCount);
    assert(order[0] == 3); // 7
    assert(order[1] == 2); // 12
    assert(order[4] == 4); // 999

    // Defensive: null model / null destination write nothing.
    assert(grid_sort_order(nullptr, kRowCount, 0, grid_sort::ASC, order, kRowCount) == 0);
    assert(grid_sort_order(kRows, kRowCount, 0, grid_sort::ASC, nullptr, kRowCount) == 0);
    // Cap respected.
    u8 small[2] = {0xFF, 0xFF};
    assert(grid_sort_order(kRows, kRowCount, 0, grid_sort::ASC, small, 2) == 2);
}

// ─── Sort cycle ────────────────────────────────────────────────
{
    // First tap arms the column ascending; after that it toggles. There is no
    // "unsorted" third state: a tap that sorts by nothing is never intended.
    assert(grid_tap_cycle(grid_sort::NONE) == grid_sort::ASC);
    assert(grid_tap_cycle(grid_sort::ASC) == grid_sort::DESC);
    assert(grid_tap_cycle(grid_sort::DESC) == grid_sort::ASC);
}

// ─── Attach ────────────────────────────────────────────────────
{
    ui::Manager m;
    const u16 plain = m.listbox(0, 0, 200, 100, nullptr, 0, nullptr, UINT16_MAX);
    assert(!m.is_grid(plain));
    // A plain listbox's rows start at the content top — the whole reason the
    // row inset is one accessor and not a subtraction at three sites.
    assert(m.row_area_top(plain, 40.0f) == 40.0f);

    const u16 g = MakeGrid(m);
    assert(m.is_grid(g));
    assert(m.grid_col_count[g] == 4);
    assert(m.grid_sorted_col(g) == UINT8_MAX); // unsorted until tapped
    assert(m.grid_sort_of(g, 0) == grid_sort::NONE);

    // Out-of-range ids are inert, not a crash.
    assert(!m.is_grid(9999));
    assert(m.grid_sorted_col(9999) == UINT8_MAX);
    assert(m.row_area_top(9999, 10.0f) == 10.0f);
}

// ─── Column hit test ───────────────────────────────────────────
{
    ui::Manager m;
    const u16 g = MakeGrid(m);
    // attach() seeds the geometry, so the header is hittable before the first
    // render (a widget must not need a frame drawn before it can be tested).
    u8 col = 0xFF;
    assert(m.grid_header_at(g, 0.0f, col) && col == 0);
    assert(m.grid_header_at(g, 79.0f, col) && col == 0);
    assert(m.grid_header_at(g, 80.0f, col) && col == 1);
    assert(m.grid_header_at(g, 159.0f, col) && col == 1);
    assert(m.grid_header_at(g, 160.0f, col) && col == 2);
    assert(m.grid_header_at(g, 239.0f, col) && col == 2);
    assert(m.grid_header_at(g, 240.0f, col) && col == 3);
    assert(m.grid_header_at(g, 299.0f, col) && col == 3);
    assert(!m.grid_header_at(g, 300.0f, col)); // past the last column
    assert(!m.grid_header_at(g, -1.0f, col));
    // A plain listbox has no header to hit.
    const u16 plain = m.listbox(0, 300, 200, 100, nullptr, 0, nullptr, UINT16_MAX);
    assert(!m.grid_header_at(plain, 10.0f, col));
}

// ─── Sort state ────────────────────────────────────────────────
{
    ui::Manager m;
    const u16 g = MakeGrid(m);
    m.grid_toggle_sort(g, 0);
    assert(m.grid_sort_of(g, 0) == grid_sort::ASC);
    assert(m.grid_sorted_col(g) == 0);
    m.grid_toggle_sort(g, 0);
    assert(m.grid_sort_of(g, 0) == grid_sort::DESC);
    // Moving to another column arms it ascending; the old one is no longer the
    // sorted column (the header highlight follows).
    m.grid_toggle_sort(g, 1);
    assert(m.grid_sort_of(g, 1) == grid_sort::ASC);
    assert(m.grid_sort_of(g, 0) == grid_sort::NONE);
    assert(m.grid_sorted_col(g) == 1);
    // A non-sortable column is inert — a tap must not silently do nothing with
    // no feedback, but it also must not claim the sort.
    m.grid_toggle_sort(g, 2);
    assert(m.grid_sort_of(g, 2) == grid_sort::NONE);
    assert(m.grid_sorted_col(g) == 1);
    m.grid_toggle_sort(g, 3);
    assert(m.grid_sorted_col(g) == 1);
    // Out of range / not a grid.
    m.grid_toggle_sort(g, 9);
    assert(m.grid_sorted_col(g) == 1);
    const u16 plain = m.listbox(0, 300, 200, 100, nullptr, 0, nullptr, UINT16_MAX);
    m.grid_toggle_sort(plain, 0);
    assert(m.grid_sorted_col(plain) == UINT8_MAX);
}

// ─── Visible row → model row ───────────────────────────────────
{
    ui::Manager m;
    const u16 g = MakeGrid(m);
    assert(m.grid_row_source(g, 0) == 0);
    assert(m.grid_row_source(g, 4) == 4);
    u8 order[kRowCount] = {2, 4, 0, 1, 3};
    m.set_grid_order(g, order);
    assert(m.grid_row_source(g, 0) == 2);
    assert(m.grid_row_source(g, 4) == 3);
    m.set_grid_order(g, nullptr);
    assert(m.grid_row_source(g, 0) == 0);
    assert(m.grid_row_source(9999, 3) == 3);
}

// ─── Row area inset ────────────────────────────────────────────
{
    ui::Manager m;
    const u16 g = MakeGrid(m);
    // The header height is derived in the metrics pass (one row); the accessor
    // is what Pass 1.8, Pass 3 and the tap path all call, so this is the single
    // place the offset exists.
    m.grid_header_h[g] = 20.0f;
    assert(m.row_area_top(g, 100.0f) == 120.0f);
    m.grid_header_h[g] = 0.0f;
    assert(m.row_area_top(g, 100.0f) == 100.0f);
}

// ─── Recycled slots do not inherit a grid ──────────────────────
{
    ui::Manager m;
    const u16 g = MakeGrid(m);
    m.grid_toggle_sort(g, 0);
    m.grid_header_h[g] = 20.0f;
    m.clear();
    // alloc() resets nothing, so every grid field has to be reset there: a
    // recycled slot that kept the column model would render a header over an
    // unrelated widget.
    const u16 fresh = m.listbox(0, 0, 200, 100, nullptr, 0, nullptr, UINT16_MAX);
    assert(fresh == g); // the same slot came back
    assert(!m.is_grid(fresh));
    assert(m.grid_header_h[fresh] == 0.0f);
    assert(m.grid_sorted_col(fresh) == UINT8_MAX);
    assert(m.grid_row_source(fresh, 0) == 0);
    assert(m.row_area_top(fresh, 50.0f) == 50.0f);
}
return 0;
}

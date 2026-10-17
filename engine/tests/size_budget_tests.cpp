// size_budget_tests.cpp — size limits on the four structs that dominate the
// engine's working set.
//
// These are not "nice numbers". Every one of them is here because it grew, or
// because growing it broke something:
//
//   sizeof(Renderer)    1,054,848 -> 2,103,168. Nobody decided to double it;
//                        temp_storage went 1 MB -> 2 MB when the sprite staging
//                        fix landed (a 16384-sprite batch needs ~1.7 MB at
//                        108 B/sprite, and the old 1 MB cap silently dropped
//                        every batch over ~9700 sprites). 2.01 MB is the
//                        CONQUERED number, not the target.
//
//   sizeof(ui::Manager) grew with every widget type that added a parallel
//                        array — and its widgets are what doubled inside it.
//
//   sizeof(SpriteBatch) is the per-frame vertex buffer. Its cap is deliberate:
//                        MAX_SPRITES is 16384 and the pool rejects overflow with
//                        a logged drop rather than flushing implicitly.
//
//   sizeof(ui::Widget)  104 -> 120 -> 128, twice. Every field has been moved in
//                        and out of the parallel arrays to buy back tail padding.
//
// The stack hazard is the reason this file exists at all. sizeof(Renderer) is
// ~2 MB, so a function declaring THREE of them as locals has a ~6 MB frame and
// a ~14 MB frame declared seven — against macOS's 8 MB main-thread stack.
// layout_tests did that and died in __chkstk_darwin's PROLOGUE, before its
// first statement, twice, and both times it was recorded as "a test bug" and
// worked around rather than pinned. The assert below is that workaround, made
// into the check that would have caught it.
//
// Plain main() + assert, no framework — same as every other test in this repo.

#include "../render/mm_renderer.hpp"
#include "../render/mm_sprite_batch.hpp"
#include "../ui/mm_ui.hpp"
#include "../input/mm_touch_gesture.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>

namespace {

constexpr std::size_t K_MB = 1024u * 1024u;

// ─── The four limits ─────────────────────────────────────────────────────────
// Headroom is deliberate and small. These are "a doubling is a bug" limits, not
// "use up the space" limits: each one survived a growth event that broke
// something, so the number is set just above the conquered value.
constexpr std::size_t K_MAX_RENDERER   = 2304 * 1024;   // 2.25 MB (actual 2.01)
constexpr std::size_t K_MAX_UI_MANAGER = 96 * 1024;     // 96 KB   (actual 85.2)
constexpr std::size_t K_MAX_SPRITE_B   = 900 * 1024;    // 900 KB  (actual 768)
constexpr std::size_t K_MAX_WIDGET     = 128;           // locked, not a budget

// The safety property, stated as the thing it protects rather than as a magic
// number: everything ONE FRAME touches at once — Renderer, ui::Manager and the
// sprite batch — must fit in half the 8 MB main-thread default, because a test
// or a host function will hold all three as locals.
//
// Written the other way round first and it was wrong: "two Renderers plus a
// Manager" is 4.09 MB, which already busts a 4 MB line. The number was chosen to
// look conservative and was not derived from anything. This one is: it is the
// realistic worst case, and it currently sits 1.16 MB under the limit.
constexpr std::size_t K_FRAME_SET = 4 * K_MB;

} // namespace

int main() {
    const std::size_t renderer  = sizeof(Renderer);
    const std::size_t manager   = sizeof(ui::Manager);
    const std::size_t sprite    = sizeof(SpriteBatch);
    const std::size_t widget    = sizeof(ui::Widget);
    const std::size_t frame_all = renderer + manager + sprite;

    printf("[size_budget]\n");
    printf("  Renderer        %10zu  (%6.2f MB)  limit %.2f MB\n", renderer,
           double(renderer) / K_MB, double(K_MAX_RENDERER) / K_MB);
    printf("  ui::Manager     %10zu  (%6.1f KB)  limit %.0f KB\n", manager,
           double(manager) / 1024.0, double(K_MAX_UI_MANAGER) / 1024.0);
    printf("  SpriteBatch     %10zu  (%6.1f KB)  limit %.0f KB\n", sprite,
           double(sprite) / 1024.0, double(K_MAX_SPRITE_B) / 1024.0);
    printf("  ui::Widget      %10zu             limit %zu\n", widget, K_MAX_WIDGET);
    printf("  frame working set%10zu  (%6.2f MB)\n\n", frame_all,
           double(frame_all) / K_MB);

    // ─── The limits ──────────────────────────────────────────────────────────
    assert(renderer <= K_MAX_RENDERER && "Renderer grew past its budget — see the header comment");
    assert(manager <= K_MAX_UI_MANAGER && "ui::Manager grew past its budget");
    assert(sprite <= K_MAX_SPRITE_B && "SpriteBatch grew past its budget");
    // Widget is a LOCK, not a ceiling: the whole parallel-array design is a
    // search for tail padding, so "it got bigger again" is the failure to report.
    assert(widget == K_MAX_WIDGET && "ui::Widget changed size — find the padding first");

    // ─── The stack property ──────────────────────────────────────────────────
    // Not a limit on any one struct: a limit on how many of them a function may
    // hold. This is the assert layout_tests needed and did not have.
    assert(frame_all < K_FRAME_SET &&
           "one frame's working set no longer fits half the main-thread stack");

    // Three Renderers is the documented edge, not a working configuration:
    // layout_tests declared SEVEN and died in __chkstk_darwin's prologue before
    // its first statement, twice. Pinned here so the margin is visible while it
    // still exists rather than discovered from a core dump.
    const std::size_t three = renderer * 3;
    printf("  3x Renderer       %10zu  (%6.2f MB)  edge of an 8 MB stack\n\n", three,
           double(three) / K_MB);
    assert(three <= 6500 * 1024 && "three Renderers no longer fit — the documented edge moved");

    // ─── TouchTracker is pinned by the engine already; assert it here too so
    // the number is visible in one place alongside the others it shares a frame
    // with. It is small and on every touch path.
    assert(sizeof(TouchTracker) <= 512 && "TouchTracker outgrew its budget");

    printf("[size_budget] all tests passed\n");
    return 0;
}
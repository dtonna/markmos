// rect tests — plain main() + assert(), no framework
#include "../math/mm_rect.h"
#include "../math/mm_vec2.h"
#include "../math/mm_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <utility>

static bool Near(f32 a, f32 b, f32 eps = 1e-5f) noexcept { return __builtin_fabsf(a - b) <= eps; }
static bool Near(const mm_math::vec2 &a, const mm_math::vec2 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps);
}
static bool Near(const mm_math::rect &a, const mm_math::rect &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.w, b.w, eps) && Near(a.h, b.h, eps);
}

int main() {
    using namespace mm_math;

    // ─── Constructors ───
    {
        rect r1; assert(Near(r1, rect(0, 0, 0, 0)));
        rect r2(10, 20, 100, 200); assert(Near(r2, rect(10, 20, 100, 200)));
        rect r3(vec2(5, 10), vec2(50, 60)); assert(Near(r3, rect(5, 10, 50, 60)));
        rect r4 = r2; assert(Near(r4, r2));
        rect r5 = std::move(r4); assert(Near(r5, rect(10, 20, 100, 200)));
    }

    // ─── Assignment ───
    {
        rect a(1, 2, 3, 4), b(5, 6, 7, 8);
        a = b; assert(Near(a, b));
        b = std::move(a); assert(Near(b, rect(5, 6, 7, 8)));
    }

    // ─── Equality ───
    {
        assert(rect(1, 2, 3, 4) == rect(1, 2, 3, 4));
        assert(rect(1, 2, 3, 4) != rect(1, 2, 3, 5));
        // epsilon comparison
        assert(rect(1, 2, 3, 4) == rect(1.000001f, 2.000001f, 3.000001f, 4.000001f));
    }

    // ─── Properties ───
    {
        rect r(10, 20, 100, 200);
        assert(Near(r.left(), 10));
        assert(Near(r.right(), 110));
        assert(Near(r.top(), 20));
        assert(Near(r.bottom(), 220));
        assert(Near(r.min(), vec2(10, 20)));
        assert(Near(r.max(), vec2(110, 220)));
        assert(Near(r.center(), vec2(60, 120)));
        assert(Near(r.size(), vec2(100, 200)));
        assert(Near(r.position(), vec2(10, 20)));
        assert(Near(r.area(), 20000));
        assert(!r.empty());
        assert(rect(0, 0, 0, 0).empty());
        assert(rect(0, 0, -1, 10).empty());
        assert(rect(0, 0, 10, -1).empty());
    }

    // ─── Mutators ───
    {
        rect r(10, 20, 100, 200);
        r.set_position(5, 6); assert(Near(r.position(), vec2(5, 6)));
        r.set_position(vec2(7, 8)); assert(Near(r.position(), vec2(7, 8)));
        r.set_size(50, 60); assert(Near(r.size(), vec2(50, 60)));
        r.set_size(vec2(70, 80)); assert(Near(r.size(), vec2(70, 80)));
        r.translate(1, 2); assert(Near(r.position(), vec2(8, 10)));
        r.translate(vec2(3, 4)); assert(Near(r.position(), vec2(11, 14)));
        r.inflate(5, 10);
        assert(Near(r, rect(6, 4, 80, 100))); // x-5, y-10, w+10, h+20
    }

    // ─── Containment & Intersection ───
    {
        rect a(0, 0, 100, 100);
        assert(a.contains(50, 50));
        assert(a.contains(vec2(50, 50)));
        assert(!a.contains(-1, 50));
        assert(!a.contains(50, 101));
        assert(a.contains(rect(10, 10, 20, 20)));
        assert(!a.contains(rect(-1, 10, 20, 20)));
        assert(a.intersects(rect(50, 50, 20, 20)));
        assert(!a.intersects(rect(200, 200, 20, 20)));
    }

    // ─── Set Operations ───
    {
        rect a(0, 0, 100, 100);
        rect b(50, 50, 100, 100);
        rect i = rect::intersection(a, b);
        assert(Near(i, rect(50, 50, 50, 50)));
        rect u = rect::unite(a, b);
        assert(Near(u, rect(0, 0, 150, 150)));
        // No intersection
        rect c(200, 200, 10, 10);
        rect empty = rect::intersection(a, c);
        assert(empty.empty());
    }

    // ─── Positioning ───
    {
        rect parent(0, 0, 100, 100);
        rect child(0, 0, 50, 50);

        // ABSOLUTE
        rect abs = rect::apply_positioning(child, parent, position_mode::ABSOLUTE, anchor::TOP_LEFT);
        assert(Near(abs, child));

        // RELATIVE
        rect rel = rect::apply_positioning(child, parent, position_mode::RELATIVE, anchor::TOP_LEFT);
        assert(Near(rel, rect(0, 0, 50, 50))); // parent (0,0) + child (0,0)

        rect rel2 = rect::apply_positioning(rect(10, 20, 30, 40), parent, position_mode::RELATIVE, anchor::TOP_LEFT);
        assert(Near(rel2, rect(10, 20, 30, 40)));

        // ANCHORED (center by default)
        rect anc = rect::apply_positioning(child, parent, position_mode::ANCHORED, anchor::CENTER);
        // center of parent = (50,50), child size 50x50, centered = (25, 25, 50, 50)
        assert(Near(anc, rect(25, 25, 50, 50)));

        // ANCHORED with offset
        rect anc2 = rect::apply_positioning(child, parent, position_mode::ANCHORED, anchor::CENTER, vec2(10, -5));
        assert(Near(anc2, rect(35, 20, 50, 50)));

        // All 9 anchors
        rect r(10, 10, 20, 20); // 20x20
        // ANCHORED centers child on anchor point
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::TOP_LEFT), rect(-10, -10, 20, 20)));
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::TOP_CENTER), rect(40, -10, 20, 20)));
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::TOP_RIGHT), rect(90, -10, 20, 20)));
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::CENTER_LEFT), rect(-10, 40, 20, 20)));
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::CENTER), rect(40, 40, 20, 20)));
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::CENTER_RIGHT), rect(90, 40, 20, 20)));
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::BOTTOM_LEFT), rect(-10, 90, 20, 20)));
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::BOTTOM_CENTER), rect(40, 90, 20, 20)));
        assert(Near(rect::apply_positioning(r, parent, position_mode::ANCHORED, anchor::BOTTOM_RIGHT), rect(90, 90, 20, 20)));
    }

    // ─── Anchor Offset ───
    {
        rect parent(0, 0, 100, 100);
        assert(Near(rect::compute_anchor_offset(parent, anchor::TOP_LEFT), vec2(0, 0)));
        assert(Near(rect::compute_anchor_offset(parent, anchor::TOP_CENTER), vec2(50, 0)));
        assert(Near(rect::compute_anchor_offset(parent, anchor::TOP_RIGHT), vec2(100, 0)));
        assert(Near(rect::compute_anchor_offset(parent, anchor::CENTER_LEFT), vec2(0, 50)));
        assert(Near(rect::compute_anchor_offset(parent, anchor::CENTER), vec2(50, 50)));
        assert(Near(rect::compute_anchor_offset(parent, anchor::CENTER_RIGHT), vec2(100, 50)));
        assert(Near(rect::compute_anchor_offset(parent, anchor::BOTTOM_LEFT), vec2(0, 100)));
        assert(Near(rect::compute_anchor_offset(parent, anchor::BOTTOM_CENTER), vec2(50, 100)));
        assert(Near(rect::compute_anchor_offset(parent, anchor::BOTTOM_RIGHT), vec2(100, 100)));
    }

    // ─── with_padding ───
    {
        rect r(10, 20, 100, 200);
        rect p = r.with_padding(10);
        assert(Near(p, rect(20, 30, 80, 180)));
    }

    // ─── Factories ───
    {
        rect r1 = rect::from_min_max(10, 20, 110, 120);
        assert(Near(r1, rect(10, 20, 100, 100)));
        rect r2 = rect::from_min_max(vec2(10, 20), vec2(110, 120));
        assert(Near(r2, rect(10, 20, 100, 100)));
        rect r3 = rect::from_center_size(50, 50, 100, 100);
        assert(Near(r3, rect(0, 0, 100, 100)));
        rect r4 = rect::from_center_size(vec2(50, 50), vec2(100, 100));
        assert(Near(r4, rect(0, 0, 100, 100)));
    }

    // ─── Operator== with epsilon ───
    {
        assert(rect(1, 2, 3, 4) == rect(1.000001f, 2.000001f, 3.000001f, 4.000001f));
        assert(rect(1, 2, 3, 4) != rect(1.01f, 2, 3, 4));
    }

    // ─── Assertion on invalid mode/anchor (would trigger in debug) ───
    {
        rect r(0, 0, 10, 10);
        rect parent(0, 0, 100, 100);
        // These would hit MM_ASSERT(false) in debug builds
        // Not testable in release, but we verify the enum covers all cases
        static_assert(static_cast<int>(anchor::BOTTOM_RIGHT) == 8, "anchor enum count");
        static_assert(static_cast<int>(position_mode::ANCHORED) == 2, "position_mode enum count");
    }

    printf("[rect] all tests passed\n");
    return 0;
}
// vec2 tests — plain main() + assert(), no framework
#include "../math/mm_vec2.h"
#include "../math/mm_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool Near(f32 a, f32 b, f32 eps = 1e-5f) noexcept { return __builtin_fabsf(a - b) <= eps; }
static bool Near(const mm_math::vec2 &a, const mm_math::vec2 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps);
}

int main() {
    using namespace mm_math;

    // ─── Constructors & Accessors ───
    {
        vec2 v1; assert(Near(v1.x, 0) && Near(v1.y, 0));
        vec2 v2(3.0f, 4.0f); assert(Near(v2.x, 3) && Near(v2.y, 4));
        vec2 v3(v2); assert(Near(v3.x, 3) && Near(v3.y, 4));
        vec2 v4(5.0f); assert(Near(v4.x, 5) && Near(v4.y, 5));
        vec2 v5(float2{1, 2}); assert(Near(v5.x, 1) && Near(v5.y, 2));
        // X/Y/W/H accessors
        assert(Near(v2.X(), 3) && Near(v2.Y(), 4));
        assert(Near(v2.W(), 3) && Near(v2.H(), 4));
    }

    // ─── Operators ───
    {
        vec2 a(1, 2), b(3, 4);
        assert(Near(a + b, vec2(4, 6)));
        assert(Near(a - b, vec2(-2, -2)));
        assert(Near(a * b, vec2(3, 8)));
        assert(Near(a * 2.0f, vec2(2, 4)));
        assert(Near(a / 2.0f, vec2(0.5f, 1)));
        assert(Near(a / b, vec2(1.0f/3, 0.5f)));
        assert(Near(-a, vec2(-1, -2)));
        a += b; assert(Near(a, vec2(4, 6)));
        a -= b; assert(Near(a, vec2(1, 2)));
        a *= b; assert(Near(a, vec2(3, 8)));
        a *= 2.0f; assert(Near(a, vec2(6, 16)));
        a /= 2.0f; assert(Near(a, vec2(3, 8)));
        // scalar * vec
        assert(Near(3.0f * vec2(2, 3), vec2(6, 9)));
        assert(Near(6.0f / vec2(2, 3), vec2(3, 2)));
        // equality
        assert(vec2(1, 2) == vec2(1, 2));
        assert(vec2(1, 2) != vec2(2, 1));
    }

    // ─── Math Functions ───
    {
        vec2 v(3, 4);
        assert(Near(v.dot(vec2(1, 2)), 11));
        assert(Near(v.length_squared(), 25));
        assert(Near(v.length(), 5));
        assert(Near(v.normalized(), vec2(0.6f, 0.8f)));
        assert(Near(vec2(0, 0).normalized(), vec2(0, 0)));
        assert(Near(v.distance(vec2(0, 0)), 5));
        // cross (2D = scalar)
        assert(Near(vec2(1, 0).cross(vec2(0, 1)), 1));
        // component-wise
        assert(Near(vec2(-1, 2).abs(), vec2(1, 2)));
        assert(Near(vec2(1.5f, -2.7f).floor(), vec2(1, -3)));
        assert(Near(vec2(1.5f, -2.7f).ceil(), vec2(2, -2)));
        assert(Near(vec2(1, 5).min(vec2(3, 2)), vec2(1, 2)));
        assert(Near(vec2(1, 5).max(vec2(3, 2)), vec2(3, 5)));
    }

    // ─── Utility Functions ───
    {
        vec2 a(0, 0), b(10, 10);
        assert(Near(a.lerp(b, 0.5f), vec2(5, 5)));
        assert(Near(a.lerp(b, 0), a));
        assert(Near(a.lerp(b, 1), b));
        // perpendicular
        assert(Near(vec2(1, 0).perpendicular(), vec2(0, 1)));
        assert(Near(vec2(0, 1).perpendicular(), vec2(-1, 0)));
        // reflect
        assert(Near(vec2(1, 0).reflect(vec2(0, 1)), vec2(1, 0)));
        assert(Near(vec2(1, 1).reflect_normalized(vec2(0, 1)), vec2(1, -1)));
        // project/reject
        assert(Near(vec2(3, 4).project_onto(vec2(1, 0)), vec2(3, 0)));
        assert(Near(vec2(3, 4).reject(vec2(1, 0)), vec2(0, 4)));
        // angle_between
        assert(Near(vec2(1, 0).angle_between(vec2(0, 1)), MM_HALF_PI));
        assert(Near(vec2(1, 0).angle_between(vec2(1, 0)), 0));
        // rotate
        assert(Near(vec2(1, 0).rotate(MM_HALF_PI), vec2(0, 1), 1e-4f));
        assert(Near(vec2(1, 0).rotate90(), vec2(0, 1)));
        assert(Near(vec2(1, 0).rotate270(), vec2(0, -1)));
        assert(Near(vec2(5, 5).rotate_around(vec2(0, 0), MM_HALF_PI), vec2(-5, 5), 1e-4f));
    }

    // ─── clamp / smoothstep ───
    {
        vec2 v(-1, 2);
        assert(Near(clamp(v, vec2(0, 0), vec2(1, 1)), vec2(0, 1)));
        vec2 t(0.5f, 0.5f);
        assert(Near(smoothstep2(t), vec2(0.5f, 0.5f)));
    }

    // ─── AABB2 ───
    {
        aabb2 box(vec2(0, 0), vec2(10, 10));
        assert(Near(box.center(), vec2(5, 5)));
        assert(Near(box.size(), vec2(10, 10)));
        assert(Near(box.half_size(), vec2(5, 5)));
        assert(box.contains(vec2(5, 5)));
        assert(!box.contains(vec2(-1, 5)));
        assert(intersects(aabb2(vec2(0, 0), vec2(5, 5)), aabb2(vec2(3, 3), vec2(8, 8))));
        assert(!intersects(aabb2(vec2(0, 0), vec2(5, 5)), aabb2(vec2(6, 6), vec2(10, 10))));
    }

    // ─── Constants ───
    {
        assert(Near(vec2::ZERO, vec2(0, 0)));
        assert(Near(vec2::ONE, vec2(1, 1)));
        assert(Near(vec2::UNIT_X, vec2(1, 0)));
        assert(Near(vec2::UNIT_Y, vec2(0, 1)));
    }

    printf("[vec2] all tests passed\n");
    return 0;
}
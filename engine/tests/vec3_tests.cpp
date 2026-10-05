// vec3 tests — plain main() + assert(), no framework
#include "../math/mm_vec3.h"
#include "../math/mm_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool Near(f32 a, f32 b, f32 eps = 1e-5f) noexcept { return __builtin_fabsf(a - b) <= eps; }
static bool Near(const mm_math::vec3 &a, const mm_math::vec3 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

int main() {
    using namespace mm_math;

    // ─── Constructors & Accessors ───
    {
        vec3 v1; assert(Near(v1.x, 0) && Near(v1.y, 0) && Near(v1.z, 0));
        vec3 v2(1, 2, 3); assert(Near(v2.x, 1) && Near(v2.y, 2) && Near(v2.z, 3));
        vec3 v3(v2); assert(Near(v3.x, 1) && Near(v3.y, 2) && Near(v3.z, 3));
        vec3 v4(5.0f); assert(Near(v4.x, 5) && Near(v4.y, 5) && Near(v4.z, 5));
        vec3 v5(float3{1, 2, 3}); assert(Near(v5.x, 1) && Near(v5.y, 2) && Near(v5.z, 3));
        // X/Y/Z/W accessors
        assert(Near(v2.X(), 1) && Near(v2.Y(), 2) && Near(v2.Z(), 3) && Near(v2.W(), 0));
    }

    // ─── Operators ───
    {
        vec3 a(1, 2, 3), b(4, 5, 6);
        assert(Near(a + b, vec3(5, 7, 9)));
        assert(Near(a - b, vec3(-3, -3, -3)));
        assert(Near(a * b, vec3(4, 10, 18)));
        assert(Near(a * 2.0f, vec3(2, 4, 6)));
        assert(Near(a / 2.0f, vec3(0.5f, 1, 1.5f)));
        assert(Near(a / b, vec3(0.25f, 0.4f, 0.5f)));
        assert(Near(-a, vec3(-1, -2, -3)));
        assert(Near(5.0f - a, vec3(4, 3, 2)));
        a += b; assert(Near(a, vec3(5, 7, 9)));
        a -= b; assert(Near(a, vec3(1, 2, 3)));
        a *= b; assert(Near(a, vec3(4, 10, 18)));
        a *= 2.0f; assert(Near(a, vec3(8, 20, 36)));
        a /= 2.0f; assert(Near(a, vec3(4, 10, 18)));
        // scalar * vec
        assert(Near(3.0f * vec3(2, 3, 4), vec3(6, 9, 12)));
        assert(Near(6.0f / vec3(2, 3, 4), vec3(3, 2, 1.5f)));
        // equality
        assert(vec3(1, 2, 3) == vec3(1, 2, 3));
        assert(vec3(1, 2, 3) != vec3(3, 2, 1));
    }

    // ─── Math Functions ───
    {
        vec3 v(1, 2, 3);
        assert(Near(v.dot(vec3(4, 5, 6)), 32));
        assert(Near(v.length_squared(), 14));
        assert(Near(v.length(), __builtin_sqrtf(14.0f)));
        assert(Near(v.normalized().length(), 1.0f, 1e-4f));
        assert(Near(vec3(0, 0, 0).normalized(), vec3(0, 0, 0)));
        assert(Near(v.distance(vec3(0, 0, 0)), __builtin_sqrtf(14.0f)));
        // cross
        assert(Near(vec3(1, 0, 0).cross(vec3(0, 1, 0)), vec3(0, 0, 1)));
        assert(Near(vec3(0, 1, 0).cross(vec3(0, 0, 1)), vec3(1, 0, 0)));
        assert(Near(vec3(0, 0, 1).cross(vec3(1, 0, 0)), vec3(0, 1, 0)));
        // component-wise
        assert(Near(vec3(-1, 2, -3).abs(), vec3(1, 2, 3)));
        assert(Near(vec3(1.5f, -2.7f, 3.9f).floor(), vec3(1, -3, 3)));
        assert(Near(vec3(1.5f, -2.7f, 3.9f).ceil(), vec3(2, -2, 4)));
        assert(Near(vec3(1, 5, 2).min(vec3(3, 2, 6)), vec3(1, 2, 2)));
        assert(Near(vec3(1, 5, 2).max(vec3(3, 2, 6)), vec3(3, 5, 6)));
        assert(Near(vec3(0.5f, 1.5f, 2.5f).clamp(vec3(0, 1, 2), vec3(1, 2, 3)), vec3(0.5f, 1.5f, 2.5f)));
        assert(Near(vec3(-1, 2, 5).clamp(vec3(0, 1, 2), vec3(1, 2, 3)), vec3(0, 2, 3)));
    }

    // ─── Utility Functions ───
    {
        vec3 a(0, 0, 0), b(10, 10, 10);
        assert(Near(a.lerp(b, 0.5f), vec3(5, 5, 5)));
        // reflect
        assert(Near(vec3(1, 0, 0).reflect(vec3(0, 1, 0)), vec3(1, 0, 0)));
        assert(Near(vec3(1, 1, 0).reflect_normalized(vec3(0, 1, 0)), vec3(1, -1, 0)));
        // project/reject
        assert(Near(vec3(3, 4, 5).project_onto(vec3(1, 0, 0)), vec3(3, 0, 0)));
        assert(Near(vec3(3, 4, 5).project_onto_normalized(vec3(1, 0, 0)), vec3(3, 0, 0)));
        assert(Near(vec3(3, 4, 5).reject(vec3(1, 0, 0)), vec3(0, 4, 5)));
        // angle_between
        assert(Near(vec3(1, 0, 0).angle_between(vec3(0, 1, 0)), MM_HALF_PI));
        assert(Near(vec3(1, 0, 0).angle_between(vec3(1, 0, 0)), 0));
    }

    // ─── clamp / smoothstep ───
    {
        vec3 v(-1, 2, 5);
        assert(Near(clamp(v, vec3(0, 0, 0), vec3(1, 1, 1)), vec3(0, 1, 1)));
        vec3 t(0.5f, 0.5f, 0.5f);
        assert(Near(smoothstep2(t), vec3(0.5f, 0.5f, 0.5f)));
    }

    // ─── Constants ───
    {
        assert(Near(vec3::ZERO, vec3(0, 0, 0)));
        assert(Near(vec3::ONE, vec3(1, 1, 1)));
        assert(Near(vec3::UNIT_X, vec3(1, 0, 0)));
        assert(Near(vec3::UNIT_Y, vec3(0, 1, 0)));
        assert(Near(vec3::UNIT_Z, vec3(0, 0, 1)));
    }

    printf("[vec3] all tests passed\n");
    return 0;
}
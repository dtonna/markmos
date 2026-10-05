// vec4 tests — plain main() + assert(), no framework
#include "../math/mm_vec4.h"
#include "../math/mm_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool Near(f32 a, f32 b, f32 eps = 1e-5f) noexcept { return __builtin_fabsf(a - b) <= eps; }
static bool Near(const mm_math::vec4 &a, const mm_math::vec4 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.w, b.w, eps);
}

int main() {
    using namespace mm_math;

    // ─── Constructors & Accessors ───
    {
        vec4 v1; assert(Near(v1.x, 0) && Near(v1.y, 0) && Near(v1.z, 0) && Near(v1.w, 0));
        vec4 v2(1, 2, 3, 4); assert(Near(v2.x, 1) && Near(v2.y, 2) && Near(v2.z, 3) && Near(v2.w, 4));
        vec4 v3(v2); assert(Near(v3.x, 1) && Near(v3.y, 2) && Near(v3.z, 3) && Near(v3.w, 4));
        vec4 v4(5.0f); assert(Near(v4.x, 5) && Near(v4.y, 5) && Near(v4.z, 5) && Near(v4.w, 5));
        vec4 v5(float4{1, 2, 3, 4}); assert(Near(v5.x, 1) && Near(v5.y, 2) && Near(v5.z, 3) && Near(v5.w, 4));
    }

    // ─── Operators ───
    {
        vec4 a(1, 2, 3, 4), b(5, 6, 7, 8);
        assert(Near(a + b, vec4(6, 8, 10, 12)));
        assert(Near(a - b, vec4(-4, -4, -4, -4)));
        assert(Near(a * b, vec4(5, 12, 21, 32)));
        assert(Near(a * 2.0f, vec4(2, 4, 6, 8)));
        assert(Near(a / 2.0f, vec4(0.5f, 1, 1.5f, 2)));
        assert(Near(a / b, vec4(0.2f, 1.0f/3, 3.0f/7, 0.5f)));
        assert(Near(-a, vec4(-1, -2, -3, -4)));
        
        vec4 a2(1, 2, 3, 4);
        a2 += b; assert(Near(a2, vec4(6, 8, 10, 12)));
        
        vec4 a3(1, 2, 3, 4);
        a3 -= b; assert(Near(a3, vec4(-4, -4, -4, -4)));
        
        vec4 a4(1, 2, 3, 4);
        a4 *= b; assert(Near(a4, vec4(5, 12, 21, 32)));
        
        vec4 a5(1, 2, 3, 4);
        a5 *= 2.0f; assert(Near(a5, vec4(2, 4, 6, 8)));
        
        vec4 a6(1, 2, 3, 4);
        a6 /= 2.0f; assert(Near(a6, vec4(0.5f, 1, 1.5f, 2)));
        // scalar * vec
        assert(Near(3.0f * vec4(2, 3, 4, 5), vec4(6, 9, 12, 15)));
        assert(Near(6.0f / vec4(2, 3, 4, 5), vec4(3, 2, 1.5f, 1.2f)));
        // equality
        assert(vec4(1, 2, 3, 4) == vec4(1, 2, 3, 4));
        assert(vec4(1, 2, 3, 4) != vec4(4, 3, 2, 1));
    }

    // ─── Math Functions ───
    {
        vec4 v(1, 2, 3, 4);
        assert(Near(v.dot(vec4(4, 5, 6, 7)), 60));
        assert(Near(v.length_squared(), 30));
        assert(Near(v.length(), __builtin_sqrtf(30.0f)));
        assert(Near(v.normalized().length(), 1.0f, 1e-4f));
        assert(Near(vec4(0, 0, 0, 0).normalized(), vec4(0, 0, 0, 0)));
        assert(Near(v.distance(vec4(0, 0, 0, 0)), __builtin_sqrtf(30.0f)));
        // cross (3D cross extended to 4D, w=0)
        assert(Near(vec4(1, 0, 0, 0).cross(vec4(0, 1, 0, 0)), vec4(0, 0, 1, 0)));
        // component-wise
        assert(Near(vec4(-1, 2, -3, 4).abs(), vec4(1, 2, 3, 4)));
        assert(Near(vec4(1.5f, -2.7f, 3.9f, -4.1f).floor(), vec4(1, -3, 3, -5)));
        assert(Near(vec4(1.5f, -2.7f, 3.9f, -4.1f).ceil(), vec4(2, -2, 4, -4)));
        assert(Near(vec4(1, 5, 2, 6).min(vec4(3, 2, 6, 1)), vec4(1, 2, 2, 1)));
        assert(Near(vec4(1, 5, 2, 6).max(vec4(3, 2, 6, 1)), vec4(3, 5, 6, 6)));
        assert(Near(vec4(0.5f, 1.5f, 2.5f, 3.5f).clamp(vec4(0, 1, 2, 3), vec4(1, 2, 3, 4)), vec4(0.5f, 1.5f, 2.5f, 3.5f)));
        assert(Near(vec4(-1, 2, 5, 10).clamp(vec4(0, 1, 2, 3), vec4(1, 2, 3, 4)), vec4(0, 2, 3, 4)));
    }

    // ─── Utility Functions ───
    {
        vec4 a(0, 0, 0, 0), b(10, 10, 10, 10);
        assert(Near(a.lerp(b, 0.5f), vec4(5, 5, 5, 5)));
        // reflect
        assert(Near(vec4(1, 0, 0, 0).reflect(vec4(0, 1, 0, 0)), vec4(1, 0, 0, 0)));
        assert(Near(vec4(1, 1, 0, 0).reflect_normalized(vec4(0, 1, 0, 0)), vec4(1, -1, 0, 0)));
        // project/reject
        assert(Near(vec4(3, 4, 5, 6).project_onto(vec4(1, 0, 0, 0)), vec4(3, 0, 0, 0)));
        assert(Near(vec4(3, 4, 5, 6).project_onto_normalized(vec4(1, 0, 0, 0)), vec4(3, 0, 0, 0)));
        assert(Near(vec4(3, 4, 5, 6).reject(vec4(1, 0, 0, 0)), vec4(0, 4, 5, 6)));
        // angle_between
        assert(Near(vec4(1, 0, 0, 0).angle_between(vec4(0, 1, 0, 0)), MM_HALF_PI));
        assert(Near(vec4(1, 0, 0, 0).angle_between(vec4(1, 0, 0, 0)), 0));
        // rotate_around (XY plane only)
        assert(Near(vec4(1, 0, 0, 0).rotate_around(vec4(0, 0, 0, 0), MM_HALF_PI), vec4(0, 1, 0, 0), 1e-4f));
        assert(Near(vec4(1, 0, 0, 0).rotate90(), vec4(0, 1, 0, 0)));
        assert(Near(vec4(1, 0, 0, 0).rotate270(), vec4(0, -1, 0, 0)));
    }

    // ─── clamp / smoothstep ───
    {
        vec4 v(-1, 2, 5, 10);
        assert(Near(clamp(v, vec4(0, 0, 0, 0), vec4(1, 1, 1, 1)), vec4(0, 1, 1, 1)));
        vec4 t(0.5f, 0.5f, 0.5f, 0.5f);
        assert(Near(smoothstep2(t), vec4(0.5f, 0.5f, 0.5f, 0.5f)));
    }

    // ─── Constants ───
    {
        assert(Near(vec4::ZERO, vec4(0, 0, 0, 0)));
        assert(Near(vec4::ONE, vec4(1, 1, 1, 1)));
        assert(Near(vec4::UNIT_X, vec4(1, 0, 0, 0)));
        assert(Near(vec4::UNIT_Y, vec4(0, 1, 0, 0)));
        assert(Near(vec4::UNIT_Z, vec4(0, 0, 1, 0)));
        assert(Near(vec4::UNIT_W, vec4(0, 0, 0, 1)));
    }

    printf("[vec4] all tests passed\n");
    return 0;
}
// mat3 tests — plain main() + assert(), no framework
#include "../math/mm_mat3.h"
#include "../math/mm_vec2.h"
#include "../math/mm_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool Near(f32 a, f32 b, f32 eps = 1e-5f) noexcept { return __builtin_fabsf(a - b) <= eps; }
static bool Near(const mm_math::vec2 &a, const mm_math::vec2 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps);
}
static bool Near(const mm_math::vec3 &a, const mm_math::vec3 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}
static bool Near(const mm_math::mat3 &a, const mm_math::mat3 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.cols[0][0], b.cols[0][0], eps) && Near(a.cols[0][1], b.cols[0][1], eps) && Near(a.cols[0][2], b.cols[0][2], eps) &&
           Near(a.cols[1][0], b.cols[1][0], eps) && Near(a.cols[1][1], b.cols[1][1], eps) && Near(a.cols[1][2], b.cols[1][2], eps) &&
           Near(a.cols[2][0], b.cols[2][0], eps) && Near(a.cols[2][1], b.cols[2][1], eps) && Near(a.cols[2][2], b.cols[2][2], eps);
}

int main() {
    using namespace mm_math;

    // ─── Constructors & Identity ───
    {
        mat3 m; assert(Near(m, mat3::identity()));
        mat3 m2(5.0f); assert(Near(m2.cols[0][0], 5) && Near(m2.cols[1][1], 5) && Near(m2.cols[2][2], 5));
        mat3 m3 = mat3::translation(5, 6);  // Use factory instead of raw float3
        assert(Near(m3.cols[2][0], 5) && Near(m3.cols[2][1], 6));
    }

    // ─── Operators ───
    {
        mat3 a = mat3::translation(1, 2);
        mat3 b = mat3::translation(3, 4);
        mat3 sum = a + b;
        assert(Near(sum.cols[2][0], 4) && Near(sum.cols[2][1], 6));
        mat3 diff = a - b;
        assert(Near(diff.cols[2][0], -2) && Near(diff.cols[2][1], -2));
        // mat3 * mat3 (column-major: result.col[j] = a * b.col[j])
        mat3 c = a * b;
        // Translation composition: (1,2) * (3,4) = (4,6)
        assert(Near(c.cols[2][0], 4) && Near(c.cols[2][1], 6));
        // scalar
        mat3 scaled = a * 2.0f;
        assert(Near(scaled.cols[2][0], 2) && Near(scaled.cols[2][1], 4));
        assert(Near(3.0f * a, a * 3.0f));
        a *= b; assert(Near(a.cols[2][0], 4) && Near(a.cols[2][1], 6));
    }

    // ─── mat3 × vec3 / vec2 ───
    {
        mat3 t = mat3::translation(5, 6);
        vec3 p(1, 2, 1);
        vec3 tp = t * p;
        assert(Near(tp.x, 6) && Near(tp.y, 8) && Near(tp.z, 1));
        vec2 p2(1, 2);
        vec2 tp2 = t.transform_point(p2);
        assert(Near(tp2, vec2(6, 8)));
    }

    // ─── Translation ───
    {
        mat3 t = mat3::translation(5, 6);
        assert(Near(t.cols[2][0], 5) && Near(t.cols[2][1], 6));
        mat3 t2 = mat3::translation(vec2(3, 4));
        assert(Near(t2.cols[2][0], 3) && Near(t2.cols[2][1], 4));
    }

    // ─── Rotation ───
    {
        mat3 r = mat3::rotation(0); assert(Near(r, mat3::identity()));
        mat3 r90 = mat3::rotation(MM_HALF_PI);
        vec2 p(1, 0);
        vec2 rp = r90.transform_point(p);
        assert(Near(rp, vec2(0, 1), 1e-4f));
        mat3 r180 = mat3::rotation(MM_PI);
        assert(Near(r180.transform_point(vec2(1, 0)), vec2(-1, 0), 1e-4f));
    }

    // ─── Scaling ───
    {
        mat3 s = mat3::scaling(2, 3);
        assert(Near(s.cols[0][0], 2) && Near(s.cols[1][1], 3));
        mat3 s2 = mat3::scaling(4, 5);
        assert(Near(s2.cols[0][0], 4) && Near(s2.cols[1][1], 5));
    }

    // ─── Rotation Around Center ───
    {
        mat3 r = mat3::rotation_around(MM_HALF_PI, vec2(1, 0));
        // Point at (2,0) relative to center (1,0) → vector (1,0) → rotated 90° → (0,1) → +center = (1,1)
        vec2 p(2, 0);
        vec2 rp = r.transform_point(p);
        assert(Near(rp, vec2(1, 1), 1e-4f));
    }

    // ─── Transpose ───
    {
        mat3 m = mat3::translation(1, 2);
        m.cols[0][0] = 1; m.cols[0][1] = 2; m.cols[0][2] = 0;
        m.cols[1][0] = 3; m.cols[1][1] = 4; m.cols[1][2] = 0;
        m.cols[2][0] = 5; m.cols[2][1] = 6; m.cols[2][2] = 1;
        mat3 mt = m.transpose();
        assert(Near(mt.cols[0][0], 1) && Near(mt.cols[0][1], 3) && Near(mt.cols[0][2], 5));
        assert(Near(mt.cols[1][0], 2) && Near(mt.cols[1][1], 4) && Near(mt.cols[1][2], 6));
    }

    // ─── Transform Vector (w=0) ───
    {
        mat3 t = mat3::translation(5, 6);
        vec3 v(1, 2, 0);
        vec3 tv = t.transform_vector(v);
        assert(Near(tv.x, 1) && Near(tv.y, 2) && Near(tv.z, 0)); // translation ignored
    }

    // ─── Transform 2D Convenience ───
    {
        mat3 t = mat3::translation(5, 6);
        vec2 p(1, 2);
        assert(Near(t.transform_point(p), vec2(6, 8)));
        vec2 v(1, 2);
        assert(Near(t.transform_vector(v), vec2(1, 2)));
        assert(Near(mat3::rotate_point(vec2(1, 0), MM_HALF_PI), vec2(0, 1), 1e-4f));
    }

    // ─── Operator*= ───
    {
        mat3 a = mat3::translation(1, 2);
        mat3 b = mat3::translation(3, 4);
        a *= b;
        assert(Near(a.cols[2][0], 4) && Near(a.cols[2][1], 6));
    }

    // ─── Inverse ───
    {
        // Identity is its own inverse
        assert(Near(mat3::identity().inverse(), mat3::identity()));
        // Translation inverse negates the offset
        mat3 t = mat3::translation(5, 6);
        mat3 ti = t.inverse();
        assert(Near(ti.cols[2][0], -5) && Near(ti.cols[2][1], -6));
        // M * M^-1 == I (and the reverse) for a rotation*translation
        mat3 m = mat3::rotation(MM_HALF_PI) * mat3::translation(3, 4);
        mat3 mi = m.inverse();
        assert(Near(m * mi, mat3::identity(), 1e-4f));
        assert(Near(mi * m, mat3::identity(), 1e-4f));
        // Singular matrix (zero column) has no inverse → identity, no crash
        mat3 sing;
        sing.cols[1] = vec3(0, 0, 0);
        assert(Near(sing.inverse(), mat3::identity()));
    }

    // ─── Constants ───
    {
        assert(Near(mat3::identity(), mat3(1.0f)));
    }

    printf("[mat3] all tests passed\n");
    return 0;
}
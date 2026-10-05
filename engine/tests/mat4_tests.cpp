// mat4 tests — plain main() + assert(), no framework
#include "../math/mm_mat4.h"
#include "../math/mm_vec3.h"
#include "../math/mm_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool Near(f32 a, f32 b, f32 eps = 1e-5f) noexcept { return __builtin_fabsf(a - b) <= eps; }
static bool Near(const mm_math::vec3 &a, const mm_math::vec3 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}
static bool Near(const mm_math::vec4 &a, const mm_math::vec4 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.w, b.w, eps);
}
static bool Near(const float4 &a, const float4 &b, f32 eps = 1e-5f) noexcept {
    return Near(a[0], b[0], eps) && Near(a[1], b[1], eps) && Near(a[2], b[2], eps) && Near(a[3], b[3], eps);
}
static bool Near(const mm_math::mat4 &a, const mm_math::mat4 &b, f32 eps = 1e-5f) noexcept {
    return Near(a.cols[0], b.cols[0], eps) && Near(a.cols[1], b.cols[1], eps) &&
           Near(a.cols[2], b.cols[2], eps) && Near(a.cols[3], b.cols[3], eps);
}

int main() {
    using namespace mm_math;

    // ─── Constructors & Identity ───
    {
        mat4 m; assert(Near(m, mat4::identity()));
        mat4 m2(5.0f);
        assert(Near(m2.cols[0].x, 5) && Near(m2.cols[1].y, 5) && Near(m2.cols[2].z, 5) && Near(m2.cols[3].w, 5));
        mat4 m3(float4{1,0,0,0}, float4{0,1,0,0}, float4{0,0,1,0}, float4{5,6,7,1});
        assert(Near(m3.cols[3].x, 5) && Near(m3.cols[3].y, 6) && Near(m3.cols[3].z, 7));
    }

    // ─── Operators ───
    {
        mat4 a = mat4::translation(1, 2, 3);
        mat4 b = mat4::translation(4, 5, 6);
        assert(Near(a + b, mat4(
            float4{2,0,0,0}, float4{0,2,0,0}, float4{0,0,2,0}, float4{5,7,9,2})));
        assert(Near(a - b, mat4(
            float4{0,0,0,0}, float4{0,0,0,0}, float4{0,0,0,0}, float4{-3,-3,-3,0})));
        // mat4 * mat4 (column-major: result.col[j] = a * b.col[j])
        mat4 c = a * b;
        // Translation composition: (1,2,3) + (4,5,6) = (5,7,9)
        assert(Near(c.cols[3].x, 5) && Near(c.cols[3].y, 7) && Near(c.cols[3].z, 9));
        // scalar
        assert(Near(a * 2.0f, mat4(
            float4{2,0,0,0}, float4{0,2,0,0}, float4{0,0,2,0}, float4{2,4,6,2})));
        assert(Near(3.0f * a, a * 3.0f));
        a *= b; assert(Near(a.cols[3].x, 5) && Near(a.cols[3].y, 7) && Near(a.cols[3].z, 9));
    }

    // ─── mat4 × vec4 / float4 ───
    {
        mat4 t = mat4::translation(5, 6, 7);
        vec4 p(1, 2, 3, 1);
        vec4 tp = t * p;
        assert(Near(tp.x, 6) && Near(tp.y, 8) && Near(tp.z, 10) && Near(tp.w, 1));
        float4 pf = {1, 2, 3, 1};
        float4 tpf = t * pf;
        assert(Near(tpf[0], 6) && Near(tpf[1], 8) && Near(tpf[2], 10) && Near(tpf[3], 1));
    }

    // ─── Translation ───
    {
        mat4 t = mat4::translation(5, 6, 7);
        assert(Near(t.cols[3].x, 5) && Near(t.cols[3].y, 6) && Near(t.cols[3].z, 7));
        mat4 t2 = mat4::translation(vec3(3, 4, 5));
        assert(Near(t2.cols[3].x, 3) && Near(t2.cols[3].y, 4) && Near(t2.cols[3].z, 5));
    }

    // ─── Rotation ───
    {
        mat4 rx = mat4::rotation_x(0); assert(Near(rx, mat4::identity()));
        mat4 rx90 = mat4::rotation_x(MM_HALF_PI);
        vec4 p(0, 1, 0, 1);
        vec4 rp = rx90 * p;
        assert(Near(rp, vec4(0, 0, 1, 1), 1e-4f));
        mat4 ry = mat4::rotation_y(MM_HALF_PI);
        vec4 p2(1, 0, 0, 1);
        vec4 rp2 = ry * p2;
        assert(Near(rp2, vec4(0, 0, -1, 1), 1e-4f));
        mat4 rz = mat4::rotation_z(MM_HALF_PI);
        vec4 p3(1, 0, 0, 1);
        vec4 rp3 = rz * p3;
        assert(Near(rp3, vec4(0, 1, 0, 1), 1e-4f));
    }

    // ─── Scaling ───
    {
        mat4 s = mat4::scaling(2, 3, 4);
        assert(Near(s.cols[0].x, 2) && Near(s.cols[1].y, 3) && Near(s.cols[2].z, 4));
        mat4 s2 = mat4::scaling(vec3(4, 5, 6));
        assert(Near(s2.cols[0].x, 4) && Near(s2.cols[1].y, 5) && Near(s2.cols[2].z, 6));
    }

    // ─── Look-At ───
    {
        vec3 eye(0, 0, 5);
        vec3 target(0, 0, 0);
        vec3 up(0, 1, 0);
        mat4 view = mat4::look_at(eye, target, up);
        // Point at origin should map to (0, 0, -5) in view space
        vec4 p(0, 0, 0, 1);
        vec4 vp = view * p;
        assert(Near(vp.z, -5, 1e-4f));
        // A view matrix must send the eye itself to the origin. The old
        // column-major conversion stored basis rows as columns, which this
        // target-only check could not catch because V*origin == col3 either way.
        vec4 ve = view * vec4(eye.x, eye.y, eye.z, 1);
        assert(Near(ve.x, 0, 1e-4f) && Near(ve.y, 0, 1e-4f) && Near(ve.z, 0, 1e-4f));
        // Asymmetric eye: with eye on an axis the transposed rotation still
        // sends it to the origin, so also cover the off-axis mm_02 camera.
        vec3 eye2(0, -18, 12);
        mat4 view2 = mat4::look_at(eye2, vec3(0, 0, 0), vec3(0, 0, 1));
        vec4 ve2 = view2 * vec4(eye2.x, eye2.y, eye2.z, 1);
        assert(Near(ve2.x, 0, 1e-4f) && Near(ve2.y, 0, 1e-4f) && Near(ve2.z, 0, 1e-4f));
    }

    // ─── Transpose ───
    {
        mat4 m(
            float4{1, 2, 3, 4},
            float4{5, 6, 7, 8},
            float4{9, 10, 11, 12},
            float4{13, 14, 15, 16}
        );
        mat4 mt = m.transpose();
        assert(Near(mt.cols[0].x, 1) && Near(mt.cols[0].y, 5) && Near(mt.cols[0].z, 9) && Near(mt.cols[0].w, 13));
        assert(Near(mt.cols[1].x, 2) && Near(mt.cols[1].y, 6) && Near(mt.cols[1].z, 10) && Near(mt.cols[1].w, 14));
        assert(Near(mt.cols[2].x, 3) && Near(mt.cols[2].y, 7) && Near(mt.cols[2].z, 11) && Near(mt.cols[2].w, 15));
        assert(Near(mt.cols[3].x, 4) && Near(mt.cols[3].y, 8) && Near(mt.cols[3].z, 12) && Near(mt.cols[3].w, 16));
    }

    // ─── Inverse Ortho ───
    // Was commented out with "known issue with column-major translation handling",
    // which was true: BOTH halves of the function read R's rows and used them as
    // columns (the 3x3 came out as R instead of R^T, and -R^T*t was computed as
    // -R*t). On translate(5,6,7) * rot_x(90°) the error was 14.
    //
    // Asserted as the INVARIANT, never as hand-computed numbers: an inverse that
    // does not multiply back to identity is not an inverse, whatever the individual
    // components look like. This is the same lesson as the SIMD `look_at` rewrite,
    // and it is also why the test is not one case — an inverse that is right for
    // one axis can still be transposed on another.
    {
        // Every axis, both orders, and a no-rotation case: a transpose bug that
        // only shows on one axis is exactly what the old version had.
        const mat4 mats[] = {
            mat4::translation(5, 6, 7) * mat4::rotation_x(MM_HALF_PI),
            mat4::translation(-2, 9, 0.5f) * mat4::rotation_y(MM_HALF_PI),
            mat4::translation(0.1f, -8, 3) * mat4::rotation_z(MM_HALF_PI),
            mat4::translation(4, 4, 4),
            mat4::identity(),
        };
        for (const mat4 &m : mats) {
            mat4 inv = m.inverse_ortho();
            assert(Near(m * inv, mat4::identity(), 1e-4f));
            assert(Near(inv * m, mat4::identity(), 1e-4f));
        }

        // The translation half specifically, stated on its own because it is the
        // half that was wrong in BOTH versions: the inverse must send the matrix's
        // own origin-offset back to the origin. A pure-translation matrix makes the
        // arithmetic readable (it is just -t) instead of hiding it inside a
        // rotation.
        mat4 tr = mat4::translation(5, 6, 7);
        mat4 tri = tr.inverse_ortho();
        assert(Near(tri.cols[3].x, -5.0f, 1e-4f));
        assert(Near(tri.cols[3].y, -6.0f, 1e-4f));
        assert(Near(tri.cols[3].z, -7.0f, 1e-4f));

        // A rotated translation: the -R^T*t term, which is what made the two
        // halves of the old fix disagree (3x3 right, translation sign wrong on two
        // of three components).
        mat4 rt = mat4::translation(5, 6, 7) * mat4::rotation_x(MM_HALF_PI);
        mat4 rti = rt.inverse_ortho();
        assert(Near(rti.cols[3].x, -5.0f, 1e-4f));
        assert(Near(rti.cols[3].y, -7.0f, 1e-4f));
        assert(Near(rti.cols[3].z, 6.0f, 1e-4f));
    }

    // ─── store_column_major ───
    {
        mat4 m = mat4::translation(1, 2, 3);
        f32 buf[16];
        m.store_column_major(buf);
        // Column-major: col0, col1, col2, col3
        assert(Near(buf[0], 1) && Near(buf[1], 0) && Near(buf[2], 0) && Near(buf[3], 0)); // col0
        assert(Near(buf[4], 0) && Near(buf[5], 1) && Near(buf[6], 0) && Near(buf[7], 0)); // col1
        assert(Near(buf[8], 0) && Near(buf[9], 0) && Near(buf[10], 1) && Near(buf[11], 0)); // col2
        assert(Near(buf[12], 1) && Near(buf[13], 2) && Near(buf[14], 3) && Near(buf[15], 1)); // col3 (translation)
    }

    // ─── Projection Matrices (compile-time selection) ───
    {
        mat4 ortho = mat4::ortho(-1, 1, -1, 1, 0.1f, 100.0f);
        assert(Near(ortho.cols[0].x, 1.0f)); // 2/(1-(-1)) = 1
        assert(Near(ortho.cols[1].y, 1.0f));
        assert(Near(ortho.cols[3].w, 1.0f));

        mat4 persp = mat4::perspective(MM_HALF_PI, 1.0f, 1.0f, 100.0f);
        // Just verify it constructs without crash
        assert(Near(persp.cols[3].w, 0.0f));
    }

    // ─── Operator*= ───
    {
        mat4 a = mat4::translation(1, 2, 3);
        mat4 b = mat4::translation(4, 5, 6);
        a *= b;
        assert(Near(a.cols[3].x, 5) && Near(a.cols[3].y, 7) && Near(a.cols[3].z, 9));
    }

    // ─── Constants ───
    {
        assert(Near(mat4::identity(), mat4(1.0f)));
    }

    printf("[mat4] all tests passed\n");
    return 0;
}
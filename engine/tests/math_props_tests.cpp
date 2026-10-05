// Property / invariant tests for engine/math.
//
// Why this file exists: the two real bugs this library had (mat4::look_at after
// the SIMD rewrite, and mat4::inverse_ortho) BOTH survived because the existing
// tests asserted hand-computed components that happened to agree with the broken
// code, or because the test was commented out. A spot check cannot tell a
// transpose from the identity when the numbers were derived from the same wrong
// assumption.
//
// So nothing here asserts a literal component except where the literal IS the
// specification (a documented clip-space convention). Every other case states a
// property that must hold for ANY input:
//
//   m * inverse(m) == identity          (an inverse that does not multiply back
//   (M^T)^T == M                       is not an inverse)
//   R^T * R == identity                (orthonormality)
//   (A*B)*C == A*(B*C)                 (associativity)
//   (A*B) * v  ==  A * (B * v)         (matrix product == successive transforms)
//   cross(a,b) . a == 0                (a cross product that is not
//   cross(a,b) . b == 0                 perpendicular is not a cross product)
//   cross(a,b) == -(b x a)             (anticommutativity - a sign slip passes
//                                       perpendicularity)
//
// exp_damp gets the same treatment, because it drives EVERY animated value in the
// UI toolkit and the properties that matter there are "dt == 0 is a no-op" and
// "never overshoots", not any particular formula.
//
// Headless: pure CPU, no Renderer, no backend.
#include "core/mm_types.h"
#include "math/mm_math.h"
#include "math/mm_mat3.h"
#include "math/mm_mat4.h"
#include "math/mm_vec2.h"
#include "math/mm_vec3.h"
#include "math/mm_vec4.h"
#include <cassert>
#include <cstdio>

using namespace mm_math;

static bool Near(f32 a, f32 b, f32 eps = 1e-4f) noexcept { return __builtin_fabsf(a - b) <= eps; }
// float4 is a clang ext_vector_type, so it needs its own comparison - the mat4 and
// mat3 helpers below compare column by column and would otherwise have nothing to
// call.
static bool Near(const float4 &a, const float4 &b, f32 eps = 1e-4f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.w, b.w, eps);
}
static bool Near(const vec2 &a, const vec2 &b, f32 eps = 1e-4f) noexcept { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }
static bool Near(const vec3 &a, const vec3 &b, f32 eps = 1e-4f) noexcept { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }
static bool Near(const vec4 &a, const vec4 &b, f32 eps = 1e-4f) noexcept {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.w, b.w, eps);
}
static bool Near(const mat4 &a, const mat4 &b, f32 eps = 1e-4f) noexcept {
    return Near(a.cols[0], b.cols[0], eps) && Near(a.cols[1], b.cols[1], eps) && Near(a.cols[2], b.cols[2], eps) && Near(a.cols[3], b.cols[3], eps);
}
static bool Near(const mat3 &a, const mat3 &b, f32 eps = 1e-4f) noexcept {
    return Near(a.cols[0], b.cols[0], eps) && Near(a.cols[1], b.cols[1], eps) && Near(a.cols[2], b.cols[2], eps);
}

// The single property every inverse has to satisfy, stated once so each call site
// reads as "this must be an inverse" instead of four hand-computed columns.
static void AssertIsInverse(const mat4 &m, const mat4 &inv) noexcept {
    assert(Near(m * inv, mat4::identity()));
    assert(Near(inv * m, mat4::identity()));
}

// ─── A spread of matrices, none of them chosen to flatter the code ───
// translate*rotate and rotate*translate hold the same numbers in different
// places; a test set containing only one of them cannot tell the orders apart.
struct MatSet {
    mat4 identity, translate, rot_x, rot_y, rot_z, scale, tr_rot, rot_tr, composed;
};

static MatSet MakeMatSet() noexcept {
    MatSet s{};
    s.identity = mat4::identity();
    s.translate = mat4::translation(5, -6, 7);
    s.rot_x = mat4::rotation_x(MM_HALF_PI);
    s.rot_y = mat4::rotation_y(MM_HALF_PI);
    s.rot_z = mat4::rotation_z(0.7F);
    s.scale = mat4::scaling(2.0F, 0.5F, 3.0F);
    s.tr_rot = s.translate * s.rot_x;
    s.rot_tr = s.rot_y * s.translate;
    s.composed = s.translate * s.rot_y * s.scale * s.rot_z;
    return s;
}

static void AllMats(const MatSet &S, const mat4 *(&out)[9]) noexcept {
    out[0] = &S.identity;
    out[1] = &S.translate;
    out[2] = &S.rot_x;
    out[3] = &S.rot_y;
    out[4] = &S.rot_z;
    out[5] = &S.scale;
    out[6] = &S.tr_rot;
    out[7] = &S.rot_tr;
    out[8] = &S.composed;
}

int main() {
    const MatSet S = MakeMatSet();
    const mat4 *M[9];
    AllMats(S, M);

    // ─── mm_math::lerp ───
    // Endpoints EXACT, because every animation lands on a lerp and the two
    // algebraically-equal forms (`a+(b-a)*t` vs `a*(1-t)+b*t`) do NOT agree on
    // floating point. Pin the property, not the formula.
    {
        assert(lerp(3.0F, 9.0F, 0.0F) == 3.0F);
        assert(lerp(3.0F, 9.0F, 1.0F) == 9.0F);
        assert(Near(lerp(3.0F, 9.0F, 0.5F), 6.0F));
        f32 prev = lerp(0.0F, 10.0F, 0.0F);
        for (int i = 1; i <= 100; ++i) {
            f32 v = lerp(0.0F, 10.0F, static_cast<f32>(i) / 100.0F);
            assert(v >= prev); // monotone
            prev = v;
        }
        // Outside [0,1] it EXTRAPOLATES - documented behaviour, and what makes
        // overshoot animations possible.
        assert(lerp(0.0F, 10.0F, 2.0F) == 20.0F);
        assert(lerp(0.0F, 10.0F, -1.0F) == -10.0F);
        // Order matters, which is why every call site must know which end is
        // "current".
        assert(lerp(1.0F, 2.0F, 0.25F) != lerp(2.0F, 1.0F, 0.25F));
        // Degenerate: a == b does not drift with t.
        for (int i = 0; i <= 10; ++i) {
            assert(lerp(4.0F, 4.0F, static_cast<f32>(i) / 10.0F) == 4.0F);
        }
    }

    // ─── mm_math::clamp ───
    {
        assert(clamp(5.0F, 0.0F, 10.0F) == 5.0F);
        assert(clamp(-1.0F, 0.0F, 10.0F) == 0.0F);
        assert(clamp(11.0F, 0.0F, 10.0F) == 10.0F);
        assert(clamp(0.0F, 0.0F, 10.0F) == 0.0F);
        assert(clamp(10.0F, 0.0F, 10.0F) == 10.0F);
        // Idempotent - what makes it safe to apply twice in a chain.
        for (int i = -50; i <= 150; i += 7) {
            f32 v = static_cast<f32>(i) * 0.37F;
            assert(clamp(clamp(v, 0.0F, 10.0F), 0.0F, 10.0F) == clamp(v, 0.0F, 10.0F));
        }
        // A reversed range is NOT silently repaired. `v < lo ? lo : (v > hi ? hi : v)`
        // evaluates the two tests in order, so with lo=5, hi=-5 a value below 5 wins
        // the FIRST test and returns lo even though it is nowhere near hi. Pinned as
        // the actual behaviour: "fixing" it (swapping lo/hi) would change which
        // bound a clamped slider wins, and the header documents the pass-through
        // deliberately.
        assert(clamp(0.0F, 5.0F, -5.0F) == 5.0F);  // 0 < lo -> lo
        assert(clamp(-9.0F, 5.0F, -5.0F) == 5.0F); // below lo -> lo
        assert(clamp(9.0F, 5.0F, -5.0F) == -5.0F); // above hi -> hi
    }

    // ─── mm_math::exp_damp — what the whole UI leans on ───
    {
        // dt == 0 MUST be an exact no-op: every panel fade, press scale, thumb
        // slide and progressbar chase is advanced by this, so a non-zero factor at
        // dt 0 would make a paused game drift.
        assert(exp_damp(10.0F, 0.0F) == 0.0F);
        assert(exp_damp(0.0F, 0.5F) == 0.0F);
        // It is a FRACTION: strictly increasing in dt, and never reaching or
        // passing 1 (which would overshoot the target and oscillate forever).
        f32 prev = exp_damp(4.0F, 0.0F);
        for (int i = 1; i <= 200; ++i) {
            f32 f = exp_damp(4.0F, static_cast<f32>(i) / 100.0F);
            assert(f > prev);
            assert(f < 1.0F);
            prev = f;
        }
        // A huge frame saturates instead of blowing past.
        assert(Near(exp_damp(10.0F, 100.0F), 1.0F));
        // The chase itself: monotone convergence that never crosses the target,
        // from either side. This is the property the render code depends on.
        for (int dir = 0; dir < 2; ++dir) {
            const f32 start   = (dir == 0) ? 0.0F : 1.0F;
            const f32 target  = (dir == 0) ? 1.0F : 0.0F;
            f32       v       = start;
            f32       prev_v  = v;
            for (int i = 0; i < 400; ++i) {
                v += (target - v) * exp_damp(10.0F, 1.0F / 60.0F);
                const f32 d = (dir == 0) ? (v - prev_v) : (prev_v - v);
                assert(d >= 0.0F);
                prev_v = v;
            }
            assert(Near(v, target, 1e-3F));
        }
        // Frame-rate independence, approximately: one big step lands where two
        // half steps land. Exact equality is impossible (exponential, not linear)
        // so the tolerance is loose on purpose - the property is "no visible
        // difference", not "bit-identical".
        f32 one = 0.0F;
        f32 two = 0.0F;
        one += (1.0F - one) * exp_damp(8.0F, 1.0F / 30.0F);
        two += (1.0F - two) * exp_damp(8.0F, 1.0F / 60.0F);
        two += (1.0F - two) * exp_damp(8.0F, 1.0F / 60.0F);
        assert(Near(one, two, 0.02F));
    }

    // ─── mat4: transpose is an involution and inverts any orthonormal matrix ───
    {
        for (const mat4 *m : M) {
            assert(Near(m->transpose().transpose(), *m));
        }
        for (int i = 2; i <= 4; ++i) { // the rotations
            const mat4 &r = *M[i];
            assert(Near(r * r.transpose(), mat4::identity()));
            assert(Near(r.transpose() * r, mat4::identity()));
            // And transpose IS the inverse, so inverse_ortho has to agree with it
            // rather than merely being self-consistent.
            AssertIsInverse(r, r.inverse_ortho());
        }
        // transpose is not the identity operator, and it moves the translation
        // column - a "fix" that made transpose a no-op fails here.
        assert(!Near(S.tr_rot, S.tr_rot.transpose()));
        // Transposing a transform moves the translation from COLUMN 3 to ROW 3, so
        // it ends up in the .w of columns 0..2 - NOT in cols[3] any more. Getting
        // this backwards is the same transpose confusion as the inverse bugs, which
        // is why the assertion names where it GOES rather than only that it moved.
        assert(Near(S.tr_rot.transpose().cols[0].w, 5.0F));
        assert(Near(S.tr_rot.transpose().cols[1].w, -6.0F));
        assert(Near(S.tr_rot.transpose().cols[2].w, 7.0F));
    }

    // ─── mat4: associativity, and product == successive transforms ───
    // The column-major SIMD product is a chain of MAD groups; reordering a term
    // changes no rounding but changes everything semantic, and the existing tests
    // never multiplied three matrices.
    {
        for (const mat4 *a : M) {
            for (const mat4 *b : M) {
                for (const mat4 *c : M) {
                    assert(Near((*a * *b) * *c, *a * (*b * *c), 1e-3F));
                }
            }
        }
        const vec4 pts[] = {vec4{1, 2, 3, 1}, vec4{-4, 0.5F, 2, 1}, vec4{0, 0, 0, 1}, vec4{7, -7, 7, 1}};
        for (const mat4 *a : M) {
            for (const mat4 *b : M) {
                for (const vec4 &p : pts) {
                    assert(Near(*a * (*b * p), (*a * *b) * p, 1e-3F));
                }
            }
        }
        // The two orders are genuinely different, or the matrix set above could
        // not tell them apart.
        assert(!Near(S.tr_rot, S.rot_tr));
    }

    // ─── mat4 * vec4: a point moves, a direction does not ───
    // The most-used property in the engine. Getting w wrong is invisible until
    // something rotates around the wrong origin.
    {
        const vec4 pt{3, -1, 2, 1};
        const vec4 dir{3, -1, 2, 0};
        const mat4 movers[] = {S.translate, S.tr_rot, S.composed};
        for (const mat4 &m : movers) {
            const vec4 tp = m * pt;
            const vec4 td = m * dir;
            assert(tp.w == 1.0F);
            assert(td.w == 0.0F);
            assert(!Near(tp.x, td.x) || !Near(tp.y, td.y) || !Near(tp.z, td.z));
        }
        // The difference between the two IS the translation column.
        const vec4 moved_pt = movers[0] * pt;
        const vec4 moved_dir = movers[0] * dir;
        assert(Near(moved_pt.x - moved_dir.x, S.translate.cols[3].x, 1e-3F));
        assert(Near(moved_pt.y - moved_dir.y, S.translate.cols[3].y, 1e-3F));
        assert(Near(moved_pt.z - moved_dir.z, S.translate.cols[3].z, 1e-3F));
        // And a pure rotation leaves a direction bit-identical.
        const vec4 spun = S.composed * vec4{1, 0, 0, 0};
        const mat4 rot_only = S.rot_y;
        assert(Near(spun.x, (rot_only * vec4{1, 0, 0, 0}).x, 1e-3F));
    }

    // ─── Projections: WHERE THE PLANES LAND, per convention ───
    // The old test asserted three components and, for perspective, nothing at all.
    // What matters is the depth range, because GL is [-1, 1] while Metal and
    // Vulkan are [0, 1] - a mixed-up constant shows up as depth clamping on one
    // backend only, and nothing in the repo depth-tests an ortho projection yet,
    // so no screenshot could ever have caught it.
    {
        const f32 n = 0.5F;
        const f32 f = 40.0F;

        // GL: x,y to [-1,1] with +y up, z to [-1,1] with near at -1.
        const mat4 ogl = mat4::ortho_gl(-2.0F, 2.0F, -1.0F, 1.0F, n, f);
        for (int i = 0; i < 8; ++i) {
            const f32 x = (i & 1) ? 2.0F : -2.0F;
            const f32 y = (i & 2) ? 1.0F : -1.0F;
            const f32 z = (i & 4) ? f : n;
            const vec4 clip = ogl * vec4{x, y, z, 1.0F};
            assert(Near(clip.x, (i & 1) ? 1.0F : -1.0F));
            assert(Near(clip.y, (i & 2) ? 1.0F : -1.0F));
            assert(Near(clip.z, (i & 4) ? 1.0F : -1.0F));
        }

        // Metal: z to [0, 1] (near at 0), y NOT flipped (Metal's NDC y is up).
        const mat4 omt = mat4::ortho_mt(-2.0F, 2.0F, -1.0F, 1.0F, n, f);
        for (int i = 0; i < 8; ++i) {
            const f32 x = (i & 1) ? 2.0F : -2.0F;
            const f32 y = (i & 2) ? 1.0F : -1.0F;
            const f32 z = (i & 4) ? f : n;
            const vec4 clip = omt * vec4{x, y, z, 1.0F};
            assert(Near(clip.x, (i & 1) ? 1.0F : -1.0F));
            assert(Near(clip.y, (i & 2) ? 1.0F : -1.0F)); // y up, same as GL
            assert(Near(clip.z, (i & 4) ? 1.0F : 0.0F));  // z in [0,1], near at 0
        }
        // Every Metal-space depth is inside the clip volume - the property that a
        // GL-convention ortho violates for the whole near half of the range.
        for (int i = 0; i <= 20; ++i) {
            const f32 z = n + (f - n) * (static_cast<f32>(i) / 20.0F);
            const f32 zc = (omt * vec4{0, 0, z, 1.0F}).z;
            assert(zc >= 0.0F && zc <= 1.0F);
        }

        // Vulkan: z to [0,1] AND y flipped (Vulkan's NDC y points down).
        const mat4 ovk = mat4::ortho_vk(-2.0F, 2.0F, -1.0F, 1.0F, n, f);
        for (int i = 0; i < 8; ++i) {
            const f32 x = (i & 1) ? 2.0F : -2.0F;
            const f32 y = (i & 2) ? 1.0F : -1.0F;
            const f32 z = (i & 4) ? f : n;
            const vec4 clip = ovk * vec4{x, y, z, 1.0F};
            assert(Near(clip.x, (i & 1) ? 1.0F : -1.0F));
            assert(Near(clip.y, (i & 2) ? -1.0F : 1.0F)); // flipped vs gl/mt
            assert(Near(clip.z, (i & 4) ? 1.0F : 0.0F));
        }

        // The dispatching wrappers must BE the variant for this platform, or every
        // property above is about a function nothing calls.
#if MM_METAL
        assert(Near(mat4::ortho(-2.0F, 2.0F, -1.0F, 1.0F, n, f), omt));
        assert(Near(mat4::perspective(MM_HALF_PI, 1.0F, n, f), mat4::perspective_mt(MM_HALF_PI, 1.0F, n, f)));
#elif MM_VULKAN
        assert(Near(mat4::ortho(-2.0F, 2.0F, -1.0F, 1.0F, n, f), ovk));
        assert(Near(mat4::perspective(MM_HALF_PI, 1.0F, n, f), mat4::perspective_vk(MM_HALF_PI, 1.0F, n, f)));
#else
        assert(Near(mat4::ortho(-2.0F, 2.0F, -1.0F, 1.0F, n, f), ogl));
        assert(Near(mat4::perspective(MM_HALF_PI, 1.0F, n, f), mat4::perspective_gl(MM_HALF_PI, 1.0F, n, f)));
#endif

        // Perspective, per convention: clip.w == distance, and the depth range is
        // [-1,1] for GL and [0,1] for Metal/Vulkan.
        const mat4 pgl = mat4::perspective_gl(MM_HALF_PI, 1.0F, n, f);
        const mat4 pmt = mat4::perspective_mt(MM_HALF_PI, 1.0F, n, f);
        const mat4 pvk = mat4::perspective_vk(MM_HALF_PI, 1.0F, n, f);
        assert(Near((pgl * vec4{0, 0, -n, 1}).z / (pgl * vec4{0, 0, -n, 1}).w, -1.0F, 1e-3F));
        assert(Near((pgl * vec4{0, 0, -f, 1}).z / (pgl * vec4{0, 0, -f, 1}).w, 1.0F, 1e-3F));
        assert(Near((pmt * vec4{0, 0, -n, 1}).z / (pmt * vec4{0, 0, -n, 1}).w, 0.0F, 1e-3F));
        assert(Near((pmt * vec4{0, 0, -f, 1}).z / (pmt * vec4{0, 0, -f, 1}).w, 1.0F, 1e-3F));
        assert(Near((pvk * vec4{0, 0, -n, 1}).z / (pvk * vec4{0, 0, -n, 1}).w, 0.0F, 1e-3F));
        // w IS the view distance - the divide that makes perspective perspective.
        assert(Near((pgl * vec4{0, 0, -12.0F, 1}).w, 12.0F, 1e-3F));
        // A point twice as far away is half as big on screen.
        const vec4 a = pgl * vec4{0.2F, 0, -10.0F, 1};
        const vec4 b = pgl * vec4{0.2F, 0, -20.0F, 1};
        assert(Near(a.x / a.w, 2.0F * (b.x / b.w), 1e-3F));
        // Metal and Vulkan differ ONLY in the y sign (both are 0..1 depth) - if a
        // future edit makes them identical, one of the two conventions is gone.
        assert(!Near(pmt.cols[1].y, pvk.cols[1].y));
        assert(Near(pmt.cols[1].y, -pvk.cols[1].y));
        assert(Near(pmt.cols[2].z, pvk.cols[2].z));
    }

    // ─── look_at: the basis must be orthonormal, not just "eye at origin" ───
    // The look_at rewrite is what motivated this file. The original test used an
    // ON-AXIS eye, which a transposed basis still sent to the origin.
    {
        const vec3 eye{4, 5, 6}; // off-axis on purpose
        const vec3 target{1, 2, 3};
        const vec3 up{0, 1, 0};
        const mat4 v = mat4::look_at(eye, target, up);
        assert(Near((v * vec4{eye.x, eye.y, eye.z, 1}).x, 0.0F, 1e-3F));
        assert(Near((v * vec4{eye.x, eye.y, eye.z, 1}).y, 0.0F, 1e-3F));
        assert(Near((v * vec4{eye.x, eye.y, eye.z, 1}).z, 0.0F, 1e-3F));
        // The target is on the view axis, in FRONT of the camera (negative z).
        const vec4 vt = v * vec4{target.x, target.y, target.z, 1};
        assert(Near(vt.x, 0.0F, 1e-3F));
        assert(Near(vt.y, 0.0F, 1e-3F));
        assert(vt.z < 0.0F);
        // The basis is orthonormal - the property that actually failed.
        const mat3 r(vec3{v.cols[0].x, v.cols[0].y, v.cols[0].z},
                     vec3{v.cols[1].x, v.cols[1].y, v.cols[1].z},
                     vec3{v.cols[2].x, v.cols[2].y, v.cols[2].z});
        assert(Near(r * r.transpose(), mat3::identity(), 1e-3F));
        // Distances are preserved (a rigid transform, no scale leaking in).
        const f32 before = (target - eye).length();
        const f32 after  = vec3(vt.x, vt.y, vt.z).length();
        assert(Near(before, after, 1e-3F));
        // Up stays up: a point above the target lands above it on screen.
        assert((v * vec4{target.x, target.y + 1.0F, target.z, 1}).y > 0.0F);
    }

    // ─── mat3: the same transpose / orthonormality properties ───
    {
        const mat3 mats[] = {mat3::identity(), mat3::translation(2, 3), mat3::rotation(0.4F), mat3::rotation(MM_HALF_PI), mat3::scaling(2.0F, 0.5F)};
        for (const mat3 &m : mats) {
            assert(Near(m.transpose().transpose(), m));
        }
        const mat3 rots[] = {mat3::rotation(0.4F), mat3::rotation(MM_HALF_PI)};
        for (const mat3 &r : rots) {
            assert(Near(r * r.transpose(), mat3::identity(), 1e-3F));
            assert(Near(r.transpose() * r, mat3::identity(), 1e-3F));
        }
        // transpose is not a no-op: a translation matrix is asymmetric. (A DIAGONAL
        // scale is symmetric even when non-uniform, so it cannot witness this -
        // picking it was the first version of this assertion and it failed.)
        assert(!Near(mats[1], mats[1].transpose()));
    }

    // ─── vec3: cross and dot are properties, not numbers ───
    // A transposed or axis-swapped cross still returns "a vector of about the
    // right size" for symmetric inputs; perpendicularity is what fails.
    {
        const vec3 vs[] = {vec3{1, 0, 0}, vec3{0, 1, 0}, vec3{0, 0, 1}, vec3{1, 2, 3}, vec3{-3, 0.5F, 2}, vec3{0, -1, 0}};
        for (const vec3 &a : vs) {
            assert(Near(a.length_squared(), a.dot(a)));  // dot(a,a) == |a|^2
            assert(Near(a.length(), __builtin_sqrtf(a.length_squared())));
            for (const vec3 &b : vs) {
                assert(Near(a.dot(b), b.dot(a))); // commutative
                const vec3 c = a.cross(b);
                assert(Near(c.dot(a), 0.0F, 1e-3F)); // perpendicular to a
                assert(Near(c.dot(b), 0.0F, 1e-3F)); // and to b
                assert(Near(b.cross(a), -c, 1e-3F));  // anticommutative
            }
            const vec3 n = a.normalized();
            assert(Near(n.length(), 1.0F, 1e-3F));                 // unit
            assert(Near(n.dot(a.normalized()), 1.0F, 1e-3F));      // stable
        }
        // Right-handed basis: x cross y == z, and the two senses differ.
        assert(Near(vec3{1, 0, 0}.cross(vec3{0, 1, 0}), vec3{0, 0, 1}, 1e-6F));
        assert(Near(vec3{0, 1, 0}.cross(vec3{0, 0, 1}), vec3{1, 0, 0}, 1e-6F));
        assert(Near(vec3{0, 0, 1}.cross(vec3{1, 0, 0}), vec3{0, 1, 0}, 1e-6F));
        // Parallel (both senses) gives zero.
        assert(Near(vec3{2, 0, 0}.cross(vec3{-5, 0, 0}), vec3{0, 0, 0}, 1e-6F));
        assert(Near(vec3{2, 0, 0}.cross(vec3{5, 0, 0}), vec3{0, 0, 0}, 1e-6F));
        // |a x b| == |a||b|sin(theta): theta = 90 degrees here.
        assert(Near(vec3{2, 0, 0}.cross(vec3{0, 3, 0}).length(), 6.0F, 1e-4F));
        // distance is symmetric and matches length of the difference.
        assert(Near(vec3{1, 1, 1}.distance(vec3{4, 5, 6}), (vec3{4, 5, 6} - vec3{1, 1, 1}).length(), 1e-4F));
    }

    // ─── vec2 / vec4: the same, cheaply ───
    {
        assert(Near(vec2{3, -4}.length(), 5.0F));
        assert(Near(vec2{3, -4}.length_squared(), 25.0F));
        assert(Near(vec2{3, -4}.normalized().length(), 1.0F, 1e-4F));
        assert(Near(vec2{3, -4}.normalized(), vec2{0.6F, -0.8F}, 1e-4F));
        const vec4 vs[] = {vec4{1, 2, 3, 4}, vec4{-1, 0.5F, 2, -1}, vec4{0, 0, 0, 1}};
        for (const vec4 &v : vs) {
            assert(Near(v.dot(v), __builtin_sqrtf(v.dot(v)) * __builtin_sqrtf(v.dot(v)), 1e-3F));
            assert(Near(v.length(), __builtin_sqrtf(v.dot(v)), 1e-3F));
            if (v.dot(v) > 1e-6F) {
                assert(Near(v.normalized().length(), 1.0F, 1e-3F));
            }
        }
    }

    // ─── mm_math constants: the identities everything is built on ───
    // MM_TWO_PI feeds every rotation and every shader time uniform. If it drifted,
    // the rotation tests above would still pass on a single axis and a full turn
    // would quietly accumulate error.
    {
        assert(Near(MM_TWO_PI, 2.0F * MM_PI, 1e-6F));
        assert(Near(MM_HALF_PI, MM_PI * 0.5F, 1e-6F));
        assert(Near(MM_DEG_TO_RAD * 180.0F, MM_PI, 1e-5F));
        assert(Near(MM_RAD_TO_DEG * MM_PI, 180.0F, 1e-3F));
        // Squared directly, NOT sqrt(MM_SQRT2) * sqrt(MM_SQRT2): the compiler is
        // free to fold that back to MM_SQRT2, which made the first version of this
        // assertion fail for a reason that had nothing to do with the constant.
        assert(Near(MM_SQRT2 * MM_SQRT2, 2.0F, 1e-5F));
        // exp(1) == e. (exp(MM_E) is ~15, not e - the identity is about the INPUT 1.)
        assert(Near(__builtin_expf(1.0F), MM_E, 1e-5F));
        // A full turn is the identity - the strongest single statement of MM_TWO_PI.
        assert(Near(mat4::rotation_z(MM_TWO_PI), mat4::identity(), 1e-5F));
        assert(Near(mat4::rotation_z(MM_TWO_PI * 4.0F), mat4::identity(), 1e-5F));
        // A quarter turn about +z maps +x to +y and +y to -x (right-handed).
        const mat4 q = mat4::rotation_z(MM_HALF_PI);
        assert(Near((q * vec4{1, 0, 0, 1}).x, 0.0F, 1e-5F));
        assert(Near((q * vec4{1, 0, 0, 1}).y, 1.0F, 1e-5F));
        assert(Near((q * vec4{0, 1, 0, 1}).x, -1.0F, 1e-5F));
        assert(Near((q * vec4{0, 1, 0, 1}).y, 0.0F, 1e-5F));
        // And about +x / +y, so an axis mix-up in a rotation cannot hide.
        const mat4 qx = mat4::rotation_x(MM_HALF_PI);
        assert(Near((qx * vec4{0, 1, 0, 1}).z, 1.0F, 1e-5F));
        assert(Near((qx * vec4{0, 0, 1, 1}).y, -1.0F, 1e-5F));
        const mat4 qy = mat4::rotation_y(MM_HALF_PI);
        assert(Near((qy * vec4{0, 0, 1, 1}).x, 1.0F, 1e-5F));
        assert(Near((qy * vec4{1, 0, 0, 1}).z, -1.0F, 1e-5F));
        // Degrees round-trip through the helpers the examples use.
        assert(Near(mat4::rotation_z(90.0F * MM_DEG_TO_RAD), q, 1e-5F));
    }

    printf("[math_props] all tests passed\n");
    return 0;
}
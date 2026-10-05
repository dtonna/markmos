// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

// ring.frag — analytic progress ring (replaces the 128-quad arc).
// One quad carries the circle bounds; the fragment computes the band +
// sweep caps mathematically, so there are no tile seams at any scale.
// Vertex format reuses rounded_sprite.vert (v_local drives everything).
#version 460

layout(location = 0) in vec2  v_uv;
layout(location = 1) in vec4  v_color;
layout(location = 2) in vec2  v_local;
layout(location = 3) in float v_radius;
layout(location = 4) in float v_border_w;
layout(location = 5) in vec4  v_border_color;

layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D u_texture;

layout(binding = 2) uniform RingParams {
    // x=outer_r, y=thickness (local units), w=aa_scale. There is NO sweep in
    // here on purpose: draw commands are recorded and executed at submit(), so
    // a UBO's CONTENT is global to the frame - every ring in the frame would
    // render the LAST ring's sweep (which is exactly the bug this replaced:
    // two animated rings looked plausible, but adding fixed rings made the
    // animated ones jump). The sweep rides the vertex stream instead, which is
    // written per draw and therefore correct.
    vec4 p;
} ring_params;

// v_radius is repurposed as the SWEEP for this pipeline. The ring draws no
// rounded corners, so the attribute is free - and a quad must never be flushed
// through both this pipeline and rounded_sprite, which reads the same slot as
// a corner radius. f16 precision is fine: 2π quantizes to ~0.002 rad, which is
// under a fifth of a pixel at the radii the ring is used at.
float sweep_from_vertex() {
    return v_radius;
}

void main() {
    // Local units throughout: the quad spans -0.5..0.5, so the circle
    // edge is at r = outer_r = 0.5. (An earlier revision doubled into
    // -1..1 but kept outer_r = 0.5 — the ring drew at half size while
    // the sparkle tip used full-size math. Never again.)
    float r    = length(v_local);
    float mid  = ring_params.p.x - ring_params.p.y * 0.5;
    float hw   = ring_params.p.y * 0.5;
    float aa   = max(fwidth(r) * ring_params.p.w, 0.001);
    float band = 1.0 - smoothstep(hw - aa, hw + aa, abs(r - mid));

    // Sweep from 12 o'clock, clockwise. Local y grows downward, so the
    // top is (0,-0.5): atan(x, -y) is 0 there and grows clockwise.
    // The cap test runs on d = ang - sweep so the only discontinuity sits
    // at the arc start (fully inside, evaluates correctly even with a
    // spiked fwidth there).
    float ang = atan(v_local.x, -v_local.y);
    if (ang < 0.0) {
        ang += 6.2831853;
    }
    float d    = ang - sweep_from_vertex();
    float aa_a = max(fwidth(d) * ring_params.p.w, 0.001);
    float cap  = 1.0 - smoothstep(-aa_a, aa_a, d);

    vec4 sampled = texture(u_texture, v_uv);
    vec4 col     = sampled * v_color;
    out_color    = vec4(col.rgb, col.a * band * cap);
}

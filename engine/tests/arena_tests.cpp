// FrameArena staging discipline in the renderer's per-frame temp allocations.
//
// temp_arena is a linear bump allocator reset once per frame, and every use in
// mm_renderer.hpp is SHORTER-LIVED than the frame: the staging array exists only
// until the update_buffer() that copies it into the GPU buffer. flush_sprites_impl
// rewound its allocation after each texture group. draw_text and flush_particles
// did not, so the arena only ever GREW within a frame — ~108 bytes per glyph
// (4 verts x 24B + 6 indices x 2B) for text, 64 bytes per particle — and a
// text-heavy or particle-heavy page could exhaust temp_storage and start
// silently dropping draws.
//
// What is observable without a GPU: Renderer::temp_arena is a public member over
// a public buffer, so a test can init() it directly and read used() before and
// after a call. The paths exercised here all return BEFORE touching `backend`,
// so no Metal device is needed.
//
// No test framework (see AGENTS.md): plain main() + assert().

#include "../render/mm_renderer.hpp"
#include "core/mm_types.h"

#include <cassert>
#include <cstdio>
#include <cstring>

// Renderer is ~2MB — see the stack-size note at the top of layout_tests.cpp.
// ONE instance for the whole file, never a function-local per block.
static Renderer g_r;

int main() {
    // Same line Renderer::init() runs (mm_renderer.hpp:317). The public
    // temp_storage is the backing buffer, so the test needs no backend.
    g_r.temp_arena.init(g_r.temp_storage, sizeof(g_r.temp_storage));

    // ─── draw_text leaves the arena as it found it ──────────────
    // A string of spaces produces no quads, so draw_text returns at
    // `if (vert_count == 0)` — after allocating both staging arrays and
    // before touching the backend. That early return used to leak both.
    {
        BitmapFont font;
        font.init(TextureHandle{}, 48);

        const char *spaces = "                      "; // 22 chars
        const size_t expect = 22 * (4 * sizeof(SpriteVertex) + 6 * sizeof(u16));

        g_r.temp_arena.reset();
        const size_t before = g_r.temp_arena.used();
        g_r.draw_text(font, spaces, 0.0f, 0.0f, 0xFFFFFFFF, 1.0f);
        const size_t after = g_r.temp_arena.used();

        // Sanity: the string is long enough that the leak is unmistakable, and
        // the no-quad path really was taken (a dropped string would still leak,
        // but a string that DID draw would need the backend — so assert the
        // cheap precondition rather than assuming it).
        assert(strlen(spaces) * 108 == expect);
        assert(after == before);
    }

    // ─── Repeat calls do not accumulate ─────────────────────────
    // The single-call assertion above would also pass against a fix that only
    // rewound once. Per frame this is called a thousand times, so the shape that
    // matters is "N calls cost the same as one".
    {
        BitmapFont font;
        font.init(TextureHandle{}, 48);

        g_r.temp_arena.reset();
        const size_t before = g_r.temp_arena.used();
        for (int i = 0; i < 200; ++i) {
            g_r.draw_text(font, "    ", 0.0f, 0.0f, 0xFFFFFFFF, 1.0f);
        }
        assert(g_r.temp_arena.used() == before);
    }

    // ─── An empty string allocates nothing at all ───────────────
    // Guards the `len == 0` early return, which must not even take a marker.
    {
        BitmapFont font;
        font.init(TextureHandle{}, 48);

        g_r.temp_arena.reset();
        const size_t before = g_r.temp_arena.used();
        g_r.draw_text(font, "", 0.0f, 0.0f, 0xFFFFFFFF, 1.0f);
        assert(g_r.temp_arena.used() == before);
    }

    // ─── The arena is not being reset behind our back ───────────
    // If draw_text called temp_arena.reset() instead of rewinding to its own
    // marker, allocations made EARLIER in the frame would be silently discarded
    // — a worse bug than the leak. Prove the marker discipline: consume the arena
    // first, then check draw_text neither grows it nor eats it.
    {
        BitmapFont font;
        font.init(TextureHandle{}, 48);

        g_r.temp_arena.reset();
        auto   *squatter = g_r.temp_arena.alloc_array<u32>(1024);
        assert(squatter != nullptr);
        const size_t occupied = g_r.temp_arena.used();
        assert(occupied >= 4096);

        g_r.draw_text(font, "    ", 0.0f, 0.0f, 0xFFFFFFFF, 1.0f);
        // Unchanged: neither rewound to 0 (which would clobber `squatter`)
        // nor grew.
        assert(g_r.temp_arena.used() == occupied);

        // The squatter is still intact — rewind-to-marker, not rewind-to-zero.
        for (u32 i = 0; i < 1024; ++i) {
            squatter[i] = 0xDEADBEEFu;
        }
        assert(squatter[1023] == 0xDEADBEEFu);
        g_r.temp_arena.reset();
    }

    // ─── rewind-to-marker is exact across alignment padding ────
    // alloc_array rounds the offset up to alignof(T), so a rewind that derived
    // its marker arithmetically (used() - bytes) would land short and leak the
    // padding every single call. Force the padding: a 1-byte squatter leaves
    // draw_text's SpriteVertex array misaligned, then check the arena comes back
    // to the exact same offset and the squatter is untouched.
    {
        BitmapFont font;
        font.init(TextureHandle{}, 48);

        g_r.temp_arena.reset();
        auto *squatter = g_r.temp_arena.alloc_array<u8>(1); // alignof(u8) == 1
        assert(squatter != nullptr);
        *squatter       = 0x5A;
        const size_t occupied = g_r.temp_arena.used();

        g_r.draw_text(font, "    ", 0.0f, 0.0f, 0xFFFFFFFF, 1.0f);
        assert(g_r.temp_arena.used() == occupied);
        assert(*squatter == 0x5A);
        g_r.temp_arena.reset();
    }

    // ─── A block with real glyphs also gives the arena back ───────
//
// This is the one that actually reaches a non-trivial rewind. Everything above
// uses a blank font, so `vert_count` stays 0 and draw_text returns at the
// `vert_count == 0` check — which returns BEFORE the final rewind after
// update_buffer. That made the "squatter" block above vacuous for the branch it
// appeared to guard: swapping the final rewind for temp_arena.reset() still
// passed every assertion in it.
//
// Baking a glyph gets past that check. The shared text budget is then pre-filled
// so the SECOND early return — the silent drop that increments
// text_dropped_calls — fires instead. That branch is reached with real staging
// allocated and, crucially, still BEFORE any backend->update_buffer call, so it
// needs no GPU.
    {
        BitmapFont font;
        font.init(TextureHandle{}, 48);
        font.atlas_w    = 512;
        font.atlas_h    = 512;
        font.glyphs['A'] = {0, 0, 8, 12, 0, 0, 8};

        g_r.text_vertex_count = Renderer::MAX_TEXT_VERTS; // budget exhausted
        g_r.text_dropped_calls = 0;

        g_r.temp_arena.reset();
        auto *squatter = g_r.temp_arena.alloc_array<u32>(1024);
        assert(squatter != nullptr);
        squatter[1023] = 0xDEADBEEFu;
        const size_t occupied = g_r.temp_arena.used();

        g_r.draw_text(font, "AAAA", 0.0f, 0.0f, 0xFFFFFFFF, 1.0f);

        // The drop happened (so we really were on that branch)...
        assert(g_r.text_dropped_calls == 1);
        // ...and the staging came back, without disturbing anything the caller
        // allocated earlier in the frame.
        assert(g_r.temp_arena.used() == occupied);
        assert(squatter[1023] == 0xDEADBEEFu);

        g_r.text_vertex_count = 0;
        g_r.text_dropped_calls = 0;
        g_r.temp_arena.reset();
    }

    // ─── NOT covered: the two rewind-only-behind-a-backend paths ──
    //
    // (a) The final rewind, after the two update_buffer calls. Reaching it means
    //     getting past the shared text budget, which needs a live
    //     `backend->update_buffer` — and `backend` is null in a headless test.
    //     Swapping it for temp_arena.reset() still passes every block above. That
    //     is not a weak test, it is an unreachable line, and it is recorded as
    //     such instead of being papered over with a test that would pass for the
    //     wrong reason. The same is true of flush_particles' rewind.
    //
    // (b) draw_text's `!verts` / `!indices` early-returns: FrameArena::alloc()
    //     asserts on overflow (mm_arena.hpp:107) and only returns nullptr once
    //     that assert is compiled out, so in a debug build the abort happens
    //     before the branch is reachable. Those two rewinds are release-only
    //     defence.
    //
    // What IS covered above: every rewind a headless test can reach is guarded,
    // including the silent-drop path a text-heavy page actually hits.

    printf("[arena_tests] all tests passed\n");
    return 0;
}
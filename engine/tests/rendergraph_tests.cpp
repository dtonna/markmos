// Render-graph ordering + SortKey packing tests.
//
// The renderer records commands every frame and reorders them with a stable
// sort by SortKey before submit(). Two invariants do the heavy lifting for the
// overlay-scissor and layer machinery:
//   1. CmdType::BeginPass carries SortKey::min() and EndPass carries
//      SortKey::max() - so the frame is exactly one pass with the UI/overlay
//      draws in between, however the host recorded them. If this ever drifted,
//      every pass would split and Metal would abort on the second
//      renderCommandEncoder.
//   2. Within one layer/key, stable_sort preserves the caller's submission
//      order - so a scissor recorded before text affects that text.
//
// Also pins SortKey::pack's packing bounds, and that overflow saturates the
// command buffer instead of corrupting it.
//
// Headless: pure CPU, no Renderer, no backend.
#include "../render/mm_render_graph.hpp"
#include "core/mm_types.h"
#include "../rhi/mm_rhi_concept.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

static Command g_cmds[16];

int main() {
    // ─── SortKey pack/unpack round-trip ───
    // Values pinned at both ends of every field's range so a bit-slip (e.g. a
    // field starting at the wrong shift) fails here, and a rounding decision at
    // depth=1.0 is either way the NAND of it doesn't get off-by-one.
    // Pack returns the u64 itself; SortKey wraps that key, so make a small helper
    // rather than expecting a conversion constructor to exist.
    auto pk = [](u8 l, u16 p, u16 m, f32 d) noexcept {
        SortKey k{};
        k.key = SortKey::pack(l, p, m, d);
        return k;
    };

    {
        SortKey k = pk(LAYER_UI, 0xFFF, 0xFFFF, 1.0F);
        assert(k.layer() == LAYER_UI);
        assert(k.pipeline() == 0xFFF);
        assert(k.material() == 0xFFFF);
        assert(k.depth() == 268435455U);
        SortKey k0 = pk(0, 0, 0, 0.0F);
        assert(k0.layer() == 0 && k0.pipeline() == 0 && k0.material() == 0 && k0.depth() == 0);
        // depth is quantised; a stable sort key must not flip endpoints.
        SortKey mid = pk(1, 1, 1, 0.5F);
        assert(mid.depth() > pk(1, 1, 1, 0.49F).depth());
        // A float outside [0, 1] is clamped, not widened.
        SortKey hi = pk(0, 0, 0, 2.0F);
        SortKey lo = pk(0, 0, 0, -1.0F);
        assert(hi.depth() == 268435455U && lo.depth() == 0);
    }

    // ─── Layer dominates pipeline dominates material dominates depth ───
    {
        SortKey a = SortKey(1, 0, 0, 1.0F);
        SortKey b = SortKey(0, 0xFFF, 0xFFFF, 1.0F);
        assert(b < a); // different layer wins over everything below it
        SortKey c = SortKey(1, 2, 0xFFFF, 1.0F);
        SortKey d = SortKey(1, 1, 0, 0.0F);
        assert(d < c); // pipeline wins over material+depth
        SortKey e = SortKey(1, 1, 1, 0.0F);
        SortKey f = SortKey(1, 1, 0, 1.0F);
        assert(f < e); // material wins over depth
        SortKey g = SortKey(1, 1, 1, 0.25F);
        SortKey h = SortKey(1, 1, 1, 0.75F);
        assert(g < h);
    }

    // ─── Pass boundaries sort to the frame's two ends ───
    // This is the engine's whole model: one pass, host records BeginPass/EndPass
    // around the scene, and the sort must yield exactly [BeginPass, ...draws...,
    // EndPass] no matter the interleaving.
    {
        RenderGraph g;
        g.init(g_cmds, 16);
        PassDesc pass{};
        g.begin_pass(pass);
        g.draw_indexed(SortKey{3, 5, 2, 0.7F}, 6, 1);
        g.end_pass();
        g.draw(SortKey{0, 0, 0, 0.0F}, 4, 1); // out-of-band draw (still sorts between)
        g.sort();
        assert(g.command_count == 4);
        assert(g.commands[0].type == CmdType::BeginPass);
        assert(g.commands[3].type == CmdType::EndPass);
        assert(g.commands[0].sort_key == SortKey::min());
        assert(g.commands[3].sort_key == SortKey::max());
    }

    // ─── Stable: same key keeps submission order ───
    // The UI relies on this: a text scissor and a label share kTextScissorKey,
    // and the scissor has to apply because it was recorded first.
    {
        RenderGraph g;
        g.init(g_cmds, 16);
        SortKey k{4, 0, 0, 0.5F};
        g.draw_indexed(k, 6, 1, 0, 0);   // A
        g.draw_indexed(k, 6, 1, 6, 0);   // B
        g.draw_indexed(k, 6, 1, 12, 0);  // C
        g.sort();
        assert(g.commands[0].data.draw_indexed.first_index == 0);
        assert(g.commands[1].data.draw_indexed.first_index == 6);
        assert(g.commands[2].data.draw_indexed.first_index == 12);
    }

    // ─── DrawIndexed payload survives the sort: fields aren't clobbered by
    // the union's default-constructing Command() ctor ───
    {
        RenderGraph g;
        g.init(g_cmds, 16);
        g.draw_indexed(SortKey{}, 7, 3, 9, 42);
        // clear the other command slots, then just sort - one command, trivially
        // ordered, but the payload must be exactly what add()/draw_indexed wrote.
        g.sort();
        assert(g.command_count == 1);
        assert(g.commands[0].type == CmdType::DrawIndexed);
        assert(g.commands[0].data.draw_indexed.index_count == 7);
        assert(g.commands[0].data.draw_indexed.instance_count == 3);
        assert(g.commands[0].data.draw_indexed.first_index == 9);
        assert(g.commands[0].data.draw_indexed.vertex_offset == 42);
    }

    // ─── Overflow saturates instead of corrupting ───
    // MAX_COMMANDS is a hard cap on the frame arena; past it we lose the frame's
    // tail (the bug class that used to eat text). At least the count must clamp
    // and the earlier commands must stay intact.
    {
        RenderGraph g;
        g.init(g_cmds, 3);
        g.draw_indexed(SortKey{}, 6, 1);
        g.draw_indexed(SortKey{}, 6, 1);
        g.draw_indexed(SortKey{}, 6, 1);
        g.draw_indexed(SortKey{}, 6, 1); // dropped
        g.draw_indexed(SortKey{}, 6, 1); // dropped
        assert(g.command_count == 3);
        // Overflow path must not have touched g_cmds beyond count - the three
        // draws there are still valid DrawIndexed commands.
        for (u32 i = 0; i < 3; ++i) {
            assert(g.commands[i].type == CmdType::DrawIndexed);
        }
        g.sort(); // must not crash or reorder away a command
        assert(g.command_count == 3);
        assert(g.commands[0].type == CmdType::DrawIndexed);
    }

    // ─── add() with a null graph (no init) must not write and must count as a
    // dropped command (the returned scratch object is a shared static) ───
    {
        RenderGraph g;
        assert(g.commands == nullptr);
        Command &c = g.add(CmdType::Draw, SortKey{});
        assert(c.type == CmdType::Draw);
        assert(g.command_count == 0); // the scratch object is never part of the graph
    }

    printf("[rendergraph] all tests passed\n");
    return 0;
}
// DescriptorBindCache tests — redundant-bind suppression across a pipeline change.
//
// THE BUG THIS GUARDS (found 2026-10-05, "cards are white on Android only").
// Renderer::submit() suppresses a backend bind it thinks is already in force.
// Vulkan stores texture/sampler/UBO bindings ON THE PIPELINE
// (VulkanBackend::pl->current_view / current_sampler / current_ubos), and
// flush_descriptors() reads them back off whichever pipeline is bound when the
// draw executes. Metal keeps the equivalent state in the command encoder, where
// it survives a pipeline change — so the suppression was correct on macOS and
// silently wrong on Vulkan, which is exactly the platform split it showed up on.
//
// The failure needs three things at once: a pipeline change, a sampler that
// repeats, and a texture that does not. The sampler bind is then suppressed —
// and suppressing means SKIP, not replay — so the sampler never reaches the new
// pipeline at all. flush_descriptors() sees a view without a sampler, skips the
// combined-image-sampler write, and descriptor binding 0 keeps whatever the
// freshly-allocated set happened to hold: undefined per spec, WHITE on
// Adreno/Mali.
//
// So the assertions here are about the OUTCOME (does the drawn pipeline end up
// with a complete descriptor pair?) rather than about which binds were emitted.
// An outcome assertion survives the cache being reimplemented, and it is the
// property that actually decides whether a draw is white.
//
// Headless: pure CPU, no Renderer, no backend. The Vulkan backend itself cannot
// be exercised without a device, which is precisely why this logic had to be
// extracted into something a test could reach.
#include "../render/mm_bind_cache.hpp"
#include "core/mm_handle.hpp"
#include "core/mm_types.h"
#include <cassert>
#include <cstdio>

// ─── Fake handles ───────────────────────────────────────────────────
// Slotmap hands out id=0, gen=1 for the first resource of each kind
// (mm_slotmap.hpp: Slot::gen defaults to 1). Reproduce that exactly — the
// "fresh cache must not look like a real handle" property depends on it.
static SlotHandle mk_slot(u32 id, u16 gen = 1) noexcept {
    SlotHandle h{};
    h.id  = id;
    h.gen = gen;
    return h;
}
static PipelineHandle mk_pipeline(u32 id) noexcept { return PipelineHandle{mk_slot(id)}; }
static TextureHandle mk_texture(u32 id) noexcept { return TextureHandle{mk_slot(id)}; }
static SamplerHandle mk_sampler(u32 id) noexcept { return SamplerHandle{mk_slot(id)}; }
static BufferHandle  mk_buffer(u32 id) noexcept { return BufferHandle{mk_slot(id)}; }

// ─── Fake backend ───────────────────────────────────────────────────
// The subset of VulkanBackend's contract the cache interacts with, reduced to
// the two facts that decide whether a draw is white.
struct FakePipeline {
    bool has_view   = false;   // pl->current_view
    bool has_sampler = false;  // pl->current_sampler
    bool ubo[8]    = {};       // pl->current_ubos[i]

    // flush_descriptors() writes the ONE combined-image-sampler descriptor at
    // binding 0 only when both halves are known. If either is missing the draw
    // samples whatever the fresh descriptor set held — white.
    bool texture_descriptor_written() const noexcept { return has_view && has_sampler; }
};

struct FakeBackend {
    static constexpr u32 MAX_PIPELINES = 8;

    FakePipeline      pipelines[MAX_PIPELINES];
    DescriptorBindCache cache;

    // Mirrors submit()'s command walk for ONE flush_sprites_impl() group: the
    // order matters and is copied from mm_renderer.hpp — sampler is bound before
    // texture, both after the pipeline.
    bool flush(u32 pipeline_id, TextureHandle tex, SamplerHandle samp, u32 ubo_slot = 0, BufferHandle ubo = {}) {
        FakePipeline &pl = pipelines[pipeline_id & (MAX_PIPELINES - 1)];

        if (cache.pipeline_changed(mk_pipeline(pipeline_id))) {
            // VulkanBackend::bind_pipeline — no descriptor side effect.
        }
        if (cache.texture_changed(0, tex)) {
            pl.has_view = tex.is_valid();
        }
        if (cache.sampler_changed(0, samp)) {
            pl.has_sampler = samp.is_valid();
        }
        if (cache.uniform_changed(ubo_slot, ubo)) {
            pl.ubo[ubo_slot & 7] = ubo.is_valid();
        }
        return pl.texture_descriptor_written();
    }

    void begin_pass() noexcept { cache.reset(); }
};

int main() {
    // ─── A fresh cache must not look like a real handle ───
    // If a default-constructed TypedHandle ever equalled the first handle the
    // slotmap hands out, the FIRST bind of every frame would be suppressed and
    // nothing would draw at all. Slotmap starts gen at 1 precisely so the
    // invalid sentinel (id=0xFFFFFFFF) and id=0 cannot collide; this pins that
    // the two representations the cache relies on stay distinct.
    {
        DescriptorBindCache c;
        assert(!(TextureHandle{} == mk_texture(0)));
        assert(!(SamplerHandle{} == mk_sampler(0)));
        assert(!(BufferHandle{} == mk_buffer(0)));
        assert(!(PipelineHandle{} == mk_pipeline(0)));
        // The sentinel must read as "no handle" so is_valid() rejects it, while
        // the first real slot still reads valid. (Note: mk_slot(0xFFFFFFFF) IS
        // the sentinel — id 0xFFFFFFFF is what makes a handle invalid — so there
        // is nothing to assert about "an id the slotmap could produce" here.)
        assert(TextureHandle{}.is_valid() == false);
        assert(mk_texture(0).is_valid() == true);
    }

    // ─── The regression: pipeline change + repeated sampler + new texture ───
    // Freecell's actual sequence: flush_rounded_sprites (board cards) then
    // flush_rounded_sprites_glow (selection glow). Both pass default_sampler;
    // the glow carries a different texture. Before the fix the second flush
    // suppressed its sampler bind and rendered white.
    {
        FakeBackend       be;
        const SamplerHandle default_sampler = mk_sampler(0);
        const TextureHandle card_atlas      = mk_texture(2);
        const TextureHandle glow_tex        = mk_texture(0);

        // Flush 1 — first of the frame, binds everything.
        bool wrote = be.flush(/*pipeline*/ 0, card_atlas, default_sampler);
        assert(wrote && "first flush of the frame must write its texture descriptor");

        // Flush 2 — DIFFERENT pipeline, SAME sampler, different texture.
        wrote = be.flush(/*pipeline*/ 1, glow_tex, default_sampler);
        assert(wrote && "pipeline change must re-bind the sampler that happened to repeat");
    }

    // ─── The mirror image: pipeline change + repeated texture + new sampler ───
    // Same defect with the roles swapped. The texture is the one that repeats,
    // so without invalidation the NEW pipeline would keep no view at all.
    {
        FakeBackend        be;
        const TextureHandle shared_tex = mk_texture(2);
        const SamplerHandle samp_a     = mk_sampler(0);
        const SamplerHandle samp_b     = mk_sampler(1);

        assert(be.flush(0, shared_tex, samp_a));
        assert(be.flush(1, shared_tex, samp_b) && "pipeline change must re-bind the texture that happened to repeat");
    }

    // ─── Pipeline change where BOTH repeat ───
    // Still must re-emit: the new pipeline has never been told about either,
    // and a stale cache entry would leave it with neither.
    {
        FakeBackend        be;
        const TextureHandle tex = mk_texture(2);
        const SamplerHandle sam = mk_sampler(0);

        assert(be.flush(0, tex, sam));
        assert(be.flush(1, tex, sam) && "a new pipeline has no descriptors even if every handle repeats");
    }

    // ─── Suppression WITHIN one pipeline still works ───
    // The fix must not throw away the optimisation it is protecting. This is
    // the assertion that fails if someone "fixes" it by never suppressing.
    {
        DescriptorBindCache c;
        const TextureHandle tex = mk_texture(2);
        const SamplerHandle sam = mk_sampler(0);

        assert(c.pipeline_changed(mk_pipeline(0)));
        assert(c.texture_changed(0, tex));
        assert(c.sampler_changed(0, sam));
        assert(c.uniform_changed(0, mk_buffer(0)));

        // Repeats within the same pipeline: every one suppressed.
        assert(!c.pipeline_changed(mk_pipeline(0)));
        assert(!c.texture_changed(0, tex));
        assert(!c.sampler_changed(0, sam));
        assert(!c.uniform_changed(0, mk_buffer(0)));

        // ...and slots are tracked independently of one another.
        assert(c.texture_changed(1, mk_texture(7)));
        assert(!c.texture_changed(0, tex));
        assert(!c.sampler_changed(0, sam));
    }

    // ─── Generation is part of the identity ───
    // A recycled slotmap id is a different resource. Comparing id alone would
    // suppress the rebind after a texture is destroyed and recreated.
    {
        DescriptorBindCache c;
        assert(c.pipeline_changed(mk_pipeline(3)));
        assert(c.texture_changed(0, mk_texture(5)));
        assert(c.texture_changed(0, TextureHandle{mk_slot(5, 2)}) && "same id, new generation, is a different texture");
        // ...and re-issuing THAT handle is now suppressed again.
        assert(!c.texture_changed(0, TextureHandle{mk_slot(5, 2)}));
    }

    // ─── begin_pass() forgets everything ───
    {
        DescriptorBindCache c;
        const TextureHandle tex = mk_texture(2);
        assert(c.pipeline_changed(mk_pipeline(0)));
        assert(c.texture_changed(0, tex));
        c.reset();
        assert(c.pipeline_changed(mk_pipeline(0)) && "reset must forget the pipeline too");
        assert(c.texture_changed(0, tex) && "reset must forget descriptor slots");
    }

    // ─── Out-of-range slot: suppress, and do not write out of bounds ───
    // The Vulkan backend stores ONE descriptor per pipeline regardless of the
    // index the caller passes (bind_fragment_texture ignores `binding`), so an
    // untracked slot must not silently swallow a real bind.
    {
        DescriptorBindCache c;
        assert(!c.texture_changed(DescriptorBindCache::MAX_SLOTS, mk_texture(2)));
        assert(!c.texture_changed(0xFFFF, mk_texture(2)));
        assert(!c.sampler_changed(DescriptorBindCache::MAX_SLOTS, mk_sampler(0)));
        assert(!c.uniform_changed(DescriptorBindCache::MAX_SLOTS, mk_buffer(0)));
        // In-range still works afterwards.
        assert(c.texture_changed(DescriptorBindCache::MAX_SLOTS - 1, mk_texture(2)));
    }

    // ─── A whole freecell frame ───
    // The order submit() actually executes after graph.sort(). Every flush here
    // must end with a complete descriptor pair, which is the property that
    // decides whether it draws its texture or a white quad.
    {
        FakeBackend        be;
        const SamplerHandle default_sampler = mk_sampler(0);
        const SamplerHandle font_sampler    = mk_sampler(1);
        const TextureHandle white_tex       = mk_texture(0);
        const TextureHandle font_tex        = mk_texture(1);
        const TextureHandle card_atlas      = mk_texture(2);
        const BufferHandle  camera_ubo      = mk_buffer(0);

        // PIPE ids stand in for the renderer's pipeline handles.
        constexpr u32 P_SPRITE  = 0;
        constexpr u32 P_ROUNDED = 1;
        constexpr u32 P_GLOW    = 2;
        constexpr u32 P_SDF     = 3;  // draw_text

        // RenderBoardShared: board cards (two texture groups in one flush), then
        // the selection glow, then the top-bar text.
        assert(be.flush(P_ROUNDED, card_atlas, default_sampler, 0, camera_ubo));
        assert(be.flush(P_ROUNDED, white_tex, default_sampler));
        assert(be.flush(P_GLOW, white_tex, default_sampler));
        assert(be.flush(P_SDF, font_tex, font_sampler, 0, camera_ubo));
        // ui_mgr.render(): sprites then labels, then the undo badge text.
        assert(be.flush(P_ROUNDED, white_tex, default_sampler));
        assert(be.flush(P_SDF, font_tex, font_sampler, 0, camera_ubo));

        // Second frame — same sequence, every pipeline must still be complete.
        be.begin_pass();
        assert(be.flush(P_SPRITE, white_tex, default_sampler, 0, camera_ubo));
        assert(be.flush(P_ROUNDED, card_atlas, default_sampler, 0, camera_ubo));
        assert(be.flush(P_GLOW, white_tex, default_sampler));
        assert(be.flush(P_SDF, font_tex, font_sampler, 0, camera_ubo));

        // Only the pipelines actually DRAWN this frame may be checked — an
        // undrawn one legitimately has no descriptors, so sweeping the whole
        // array would assert something the design never promised.
        const u32 drawn[] = {P_SPRITE, P_ROUNDED, P_GLOW, P_SDF};
        for (u32 id : drawn) {
            assert(be.pipelines[id].texture_descriptor_written() && "every pipeline drawn this frame must have view + sampler");
        }
    }

    // Success message LAST — a pass line printed before the remaining blocks
    // reads as "the file passed" while a later assert is still about to fire
    // (this mistake is recorded in AGENTS.md from listbox_tests).
    printf("[bind_cache_tests] all tests passed\n");
    return 0;
}
// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "core/mm_handle.hpp"
#include "core/mm_types.h"

// DescriptorBindCache — "have I already told the backend about this binding?"
//
// Renderer::submit() walks the recorded command list and calls the backend for
// each one. Re-issuing a bind that is already in force is wasted work, and on
// Metal redundant binds are worth avoiding outright, so submit() suppresses
// them: it remembers the last value it passed for each slot and skips the call
// when the next command repeats it.
//
// That cache is only sound if it describes the state the BACKEND actually holds,
// and on Vulkan the answer is per-PIPELINE: bind_fragment_texture/sampler and
// bind_uniform_buffer write into the bound pipeline's current_view /
// current_sampler / current_ubos[], and flush_descriptors() reads them back off
// that same pipeline. So one cache entry describes ONE pipeline's bindings, and
// a pipeline change invalidates every one of them.
//
// The bug this exists to prevent: a flush that reuses the previous flush's
// sampler while drawing a different texture has its sampler bind suppressed —
// and suppressing means "skip", not "replay", so that sampler never reaches the
// NEW pipeline. flush_descriptors() then sees a view without a sampler, skips
// the combined-image-sampler write, and binding 0 keeps whatever the freshly
// allocated descriptor set happened to hold (undefined per spec; WHITE on
// Adreno/Mali). Cards rendered white on Android while macOS was fine, because
// Metal keeps texture/sampler bindings in encoder state that outlives a pipeline
// change and Vulkan does not.
//
// pipeline_changed() is the only way to move to a different pipeline, so the
// invalidation cannot be forgotten at a new call site.
//
// Vertex buffers, index buffers and the scissor are NOT here: those are global
// dynamic state on Vulkan and encoder state on Metal, so they genuinely survive
// a pipeline change on both backends and suppressing across one is correct.

struct DescriptorBindCache {
    static constexpr u32 MAX_SLOTS = 4;

    PipelineHandle pipeline{};
    TextureHandle  tex[MAX_SLOTS]{};
    SamplerHandle  sam[MAX_SLOTS]{};
    BufferHandle   ubo[MAX_SLOTS]{};

    // True when `h` is not the pipeline already in force. Records it and drops
    // every cached descriptor binding, because those belonged to the old one.
    bool           pipeline_changed(PipelineHandle h) noexcept {
        if (h == pipeline) {
            return false;
        }
        pipeline = h;
        drop_descriptors();
        return true;
    }

    // Pass boundary (BeginPass): nothing is known to be in force any more.
    void           reset() noexcept {
        pipeline = {};
        drop_descriptors();
    }

    // Each returns true when the caller must actually issue the bind. An
    // out-of-range slot always returns false — there is nowhere to record it, and
    // the backend stores one descriptor per slot regardless of the index the
    // caller passed (see VulkanBackend::bind_fragment_texture), so suppressing
    // an untracked slot would silently drop a real bind.
    bool           texture_changed(u32 index, TextureHandle h) noexcept {
        if (index >= MAX_SLOTS || tex[index] == h) {
            return false;
        }
        tex[index] = h;
        return true;
    }

    bool           sampler_changed(u32 index, SamplerHandle h) noexcept {
        if (index >= MAX_SLOTS || sam[index] == h) {
            return false;
        }
        sam[index] = h;
        return true;
    }

    bool           uniform_changed(u32 index, BufferHandle h) noexcept {
        if (index >= MAX_SLOTS || ubo[index] == h) {
            return false;
        }
        ubo[index] = h;
        return true;
    }

  private:
    void           drop_descriptors() noexcept {
        for (u32 i = 0; i < MAX_SLOTS; ++i) {
            tex[i] = {};
            sam[i] = {};
            ubo[i] = {};
        }
    }
};
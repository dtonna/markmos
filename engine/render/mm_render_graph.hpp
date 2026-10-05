// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_cache_metrics.hpp"
#include "core/mm_types.h"
#include "../core/mm_handle.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include "mm_sort_key.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Render Graph — command buffer as enum + union, no virtual dispatch
// Cache reason:
//   - Commands stored as flat array, processed linearly
//   - Branch predictor learns the switch pattern (few command types)
//   - No virtual call per draw, no function pointer indirection
// Design:
//   - Command = tagged union (enum + struct data)
//   - Single array per frame, allocated in FrameArena
//   - Post-sort by SortKey before execution

static constexpr u32 MAX_COMMANDS = 4096;

// A command that does not fit is DROPPED, and the drop is silent apart from
// TRACK_POOL_OVERFLOW(): add() hands back a shared static scratch Command, so
// the caller records into an object nobody submits. What disappears is always
// the TAIL of the frame, and the tail is the highest SortKey work - draw_text
// records at {text_layer, 0, 0, 1.0f} and runs last, so an overflowing frame
// loses exactly its text. The symptom is "some labels are missing and it
// depends on what the page drew first", which reads as a widget bug.
// Text has its own, much tighter budget for the same reason: see
// Renderer::MAX_TEXT_VERTS and the early return in draw_text().

enum class CmdType : u8 {
    Draw                = 0,
    DrawIndexed         = 1,
    BindPipeline        = 2,
    BindVertexBuffer    = 3,
    BindIndexBuffer     = 4,
    SetScissor          = 6,
    Clear               = 8,
    BeginPass           = 9,
    EndPass             = 10,
    BindFragmentTexture = 14,
    BindFragmentSampler = 15,
    BindUniformBuffer   = 16,
};

struct CmdDraw {
    u32 vertex_count;
    u32 instance_count;
    u32 first_vertex;
    u32 first_instance;
};

struct CmdDrawIndexed {
    u32 index_count;
    u32 instance_count;
    u32 first_index;
    i32  vertex_offset;
};

struct CmdBindPipeline {
    PipelineHandle pipeline;
};

struct CmdBindVertexBuffer {
    BufferHandle buffer;
    u32     binding;
    u64     offset;
    u64     stride;
};

struct CmdBindIndexBuffer {
    BufferHandle buffer;
    IndexType    type;
    u64     offset;
};

struct CmdSetScissor {
    i16  x, y;
    u16 w, h;
};

struct CmdBeginPass {
    PassDesc pass;
};

struct CmdBindFragmentTexture {
    TextureHandle texture;
    u32      index;
};

struct CmdBindFragmentSampler {
    SamplerHandle sampler;
    u32      index;
};

struct CmdBindUniformBuffer {
    BufferHandle buffer;
    u32     binding;
};

struct Command {
    CmdType type;
    SortKey sort_key;

    union {
        CmdDraw                draw;
        CmdDrawIndexed         draw_indexed;
        CmdBindPipeline        bind_pipeline;
        CmdBindVertexBuffer    bind_vb;
        CmdBindIndexBuffer     bind_ib;
        CmdSetScissor          set_scissor;
        CmdBeginPass           begin_pass;
        CmdBindFragmentTexture bind_frag_tex;
        CmdBindFragmentSampler bind_frag_samp;
        CmdBindUniformBuffer   bind_ubo;
    } data;

    Command() noexcept : type(static_cast<CmdType>(0)), sort_key{}, data{} {}
};

static_assert(sizeof(Command) <= 80, "Command must be compact");

struct RenderGraph {
    Command *commands      = nullptr;
    u32 command_count = 0;
    u32 command_cap   = 0;

    void     init(Command *buffer, u32 capacity) noexcept {
        commands      = buffer;
        command_cap   = capacity;
        command_count = 0;
    }

    void reset() noexcept {
        command_count = 0;
    }

    Command &add(CmdType type, SortKey key = SortKey{}) noexcept {
        static Command dropped{};
        if (!commands || command_cap == 0) {
            TRACK_POOL_OVERFLOW();
            dropped.type     = type;
            dropped.sort_key = key;
            return dropped;
        }
        if (command_count >= command_cap) {
            TRACK_POOL_OVERFLOW();
            dropped.type     = type;
            dropped.sort_key = key;
            return dropped;
        }
        Command &cmd = commands[command_count++];
        cmd.type     = type;
        cmd.sort_key = key;
        return cmd;
    }

    void draw(SortKey key, u32 vertex_count, u32 instance_count, u32 first_vertex = 0, u32 first_instance = 0) noexcept {
        auto &cmd                    = add(CmdType::Draw, key);
        cmd.data.draw.vertex_count   = vertex_count;
        cmd.data.draw.instance_count = instance_count;
        cmd.data.draw.first_vertex   = first_vertex;
        cmd.data.draw.first_instance = first_instance;
    }

    void draw_indexed(SortKey key, u32 index_count, u32 instance_count, u32 first_index = 0, i32 vertex_offset = 0) noexcept {
        auto &cmd                            = add(CmdType::DrawIndexed, key);
        cmd.data.draw_indexed.index_count    = index_count;
        cmd.data.draw_indexed.instance_count = instance_count;
        cmd.data.draw_indexed.first_index    = first_index;
        cmd.data.draw_indexed.vertex_offset  = vertex_offset;
    }

    void bind_pipeline(PipelineHandle pipeline, SortKey key = {}) noexcept {
        auto &cmd                       = add(CmdType::BindPipeline, key);
        cmd.data.bind_pipeline.pipeline = pipeline;
    }

    void bind_vertex_buffer(BufferHandle buffer, u32 binding, u64 offset, u64 stride, SortKey key = {}) noexcept {
        auto &cmd                = add(CmdType::BindVertexBuffer, key);
        cmd.data.bind_vb.buffer  = buffer;
        cmd.data.bind_vb.binding = binding;
        cmd.data.bind_vb.offset  = offset;
        cmd.data.bind_vb.stride  = stride;
    }

    void bind_index_buffer(BufferHandle buffer, IndexType type, u64 offset = 0, SortKey key = {}) noexcept {
        auto &cmd               = add(CmdType::BindIndexBuffer, key);
        cmd.data.bind_ib.buffer = buffer;
        cmd.data.bind_ib.type   = type;
        cmd.data.bind_ib.offset = offset;
    }

    void begin_pass(const PassDesc &pass) noexcept {
        auto &cmd                = add(CmdType::BeginPass, SortKey::min());
        cmd.data.begin_pass.pass = pass;
    }

    void end_pass() noexcept { add(CmdType::EndPass, SortKey::max()); }

    void bind_fragment_texture(TextureHandle texture, u32 index, SortKey key = {}) noexcept {
        auto &cmd                      = add(CmdType::BindFragmentTexture, key);
        cmd.data.bind_frag_tex.texture = texture;
        cmd.data.bind_frag_tex.index   = index;
    }

    void bind_fragment_sampler(SamplerHandle sampler, u32 index, SortKey key = {}) noexcept {
        auto &cmd                       = add(CmdType::BindFragmentSampler, key);
        cmd.data.bind_frag_samp.sampler = sampler;
        cmd.data.bind_frag_samp.index   = index;
    }

    void bind_uniform_buffer(BufferHandle buffer, u32 binding, SortKey key = {}) noexcept {
        auto &cmd                = add(CmdType::BindUniformBuffer, key);
        cmd.data.bind_ubo.buffer  = buffer;
        cmd.data.bind_ubo.binding = binding;
    }

    void set_scissor(i16 x, i16 y, u16 w, u16 h, SortKey key = SortKey{}) noexcept {
        auto &cmd              = add(CmdType::SetScissor, key);
        cmd.data.set_scissor.x = x;
        cmd.data.set_scissor.y = y;
        cmd.data.set_scissor.w = w;
        cmd.data.set_scissor.h = h;
    }

    // Sort commands by SortKey using std::sort (in-place introsort, O(n log n))
    // @cache_reason In-place, no heap alloc, no alloca; u64 keys are hardware-fast
    void sort() noexcept {
        std::stable_sort(commands, commands + command_count, [](const Command &a, const Command &b) noexcept { return a.sort_key < b.sort_key; });
    }
};

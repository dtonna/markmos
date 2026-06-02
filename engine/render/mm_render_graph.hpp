// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_cache_metrics.hpp"
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

static constexpr uint32_t MAX_COMMANDS = 4096;

enum class CmdType : uint8_t {
    Draw                = 0,
    DrawIndexed         = 1,
    BindPipeline        = 2,
    BindVertexBuffer    = 3,
    BindIndexBuffer     = 4,
    PushConstant        = 5,
    SetScissor          = 6,
    SetViewport         = 7,
    Clear               = 8,
    BeginPass           = 9,
    EndPass             = 10,
    DispatchCompute     = 11,
    CopyBuffer          = 12,
    CopyTexture         = 13,
    BindFragmentTexture = 14,
    BindFragmentSampler = 15,
    BindUniformBuffer   = 16,
};

struct CmdDraw {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
};

struct CmdDrawIndexed {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t  vertex_offset;
};

struct CmdBindPipeline {
    PipelineHandle pipeline;
};

struct CmdBindVertexBuffer {
    BufferHandle buffer;
    uint32_t     binding;
    uint64_t     offset;
    uint64_t     stride;
};

struct CmdBindIndexBuffer {
    BufferHandle buffer;
    IndexType    type;
    uint64_t     offset;
};

struct CmdPushConstant {
    PipelineHandle pipeline;
    uint32_t       offset;
    uint32_t       size;
    // Data follows inline in command buffer (variable size)
};

struct CmdSetScissor {
    int16_t  x, y;
    uint16_t w, h;
};

struct CmdSetViewport {
    float x, y, w, h, min_depth, max_depth;
};

struct CmdBeginPass {
    PassDesc pass;
};

struct CmdDispatchCompute {
    uint32_t group_x, group_y, group_z;
};

struct CmdCopyBuffer {
    BufferHandle src, dst;
    uint32_t     src_offset, dst_offset, size;
};

struct CmdBindFragmentTexture {
    TextureHandle texture;
    uint32_t      index;
};

struct CmdBindFragmentSampler {
    SamplerHandle sampler;
    uint32_t      index;
};

struct CmdBindUniformBuffer {
    BufferHandle buffer;
    uint32_t     binding;
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
        CmdPushConstant        push_constant;
        CmdSetScissor          set_scissor;
        CmdSetViewport         set_viewport;
        CmdBeginPass           begin_pass;
        CmdDispatchCompute     dispatch;
        CmdCopyBuffer          copy_buffer;
        CmdBindFragmentTexture bind_frag_tex;
        CmdBindFragmentSampler bind_frag_samp;
        CmdBindUniformBuffer   bind_ubo;
    } data;

    Command() noexcept : type(static_cast<CmdType>(0)), sort_key{}, data{} {}
};

static_assert(sizeof(Command) <= 80, "Command must be compact");

struct RenderGraph {
    Command *commands      = nullptr;
    uint32_t command_count = 0;
    uint32_t command_cap   = 0;
    uint8_t *push_data     = nullptr; // inline push constant data
    uint32_t push_offset   = 0;

    void     init(Command *buffer, uint32_t capacity) noexcept {
        commands      = buffer;
        command_cap   = capacity;
        command_count = 0;
    }

    void reset() noexcept {
        command_count = 0;
        push_offset   = 0;
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

    void draw(SortKey key, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex = 0, uint32_t first_instance = 0) noexcept {
        auto &cmd                    = add(CmdType::Draw, key);
        cmd.data.draw.vertex_count   = vertex_count;
        cmd.data.draw.instance_count = instance_count;
        cmd.data.draw.first_vertex   = first_vertex;
        cmd.data.draw.first_instance = first_instance;
    }

    void draw_indexed(SortKey key, uint32_t index_count, uint32_t instance_count, uint32_t first_index = 0, int32_t vertex_offset = 0) noexcept {
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

    void bind_vertex_buffer(BufferHandle buffer, uint32_t binding, uint64_t offset, uint64_t stride, SortKey key = {}) noexcept {
        auto &cmd                = add(CmdType::BindVertexBuffer, key);
        cmd.data.bind_vb.buffer  = buffer;
        cmd.data.bind_vb.binding = binding;
        cmd.data.bind_vb.offset  = offset;
        cmd.data.bind_vb.stride  = stride;
    }

    void bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset = 0, SortKey key = {}) noexcept {
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

    void bind_fragment_texture(TextureHandle texture, uint32_t index, SortKey key = {}) noexcept {
        auto &cmd                      = add(CmdType::BindFragmentTexture, key);
        cmd.data.bind_frag_tex.texture = texture;
        cmd.data.bind_frag_tex.index   = index;
    }

    void bind_fragment_sampler(SamplerHandle sampler, uint32_t index, SortKey key = {}) noexcept {
        auto &cmd                       = add(CmdType::BindFragmentSampler, key);
        cmd.data.bind_frag_samp.sampler = sampler;
        cmd.data.bind_frag_samp.index   = index;
    }

    void bind_uniform_buffer(BufferHandle buffer, uint32_t binding, SortKey key = {}) noexcept {
        auto &cmd                = add(CmdType::BindUniformBuffer, key);
        cmd.data.bind_ubo.buffer  = buffer;
        cmd.data.bind_ubo.binding = binding;
    }

    void set_scissor(int16_t x, int16_t y, uint16_t w, uint16_t h) noexcept {
        auto &cmd              = add(CmdType::SetScissor);
        cmd.data.set_scissor.x = x;
        cmd.data.set_scissor.y = y;
        cmd.data.set_scissor.w = w;
        cmd.data.set_scissor.h = h;
    }

    // Sort commands by SortKey using std::sort (in-place introsort, O(n log n))
    // @cache_reason In-place, no heap alloc, no alloca; uint64_t keys are hardware-fast
    void sort() noexcept {
        std::stable_sort(commands, commands + command_count, [](const Command &a, const Command &b) noexcept { return a.sort_key < b.sort_key; });
    }
};

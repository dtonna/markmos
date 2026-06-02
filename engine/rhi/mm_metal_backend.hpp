// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_expected.hpp"
#include "../core/mm_handle.hpp"
#include "../core/mm_slotmap.hpp"
#include "mm_rhi_concept.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unistd.h>

// Metal Backend — flat struct, no Obj-C messaging in hot path
// Uses Apple's metal-cpp (Metal.hpp) instead of raw Obj-C.
// Cache reason: no virtual dispatch, no NSObject subclass overhead

#if defined(USE_METAL_BACKEND)

#    include <Metal/Metal.hpp>
#    include <QuartzCore/QuartzCore.hpp>

#    pragma clang diagnostic ignored "-Wnonnull"

struct MetalBuffer {
    MTL::Buffer *buffer;
    uint32_t     size;
    BufferType   type;
};

struct MetalTexture {
    MTL::Texture *texture;
    uint32_t      width, height;
    PixelFormat   format;
};

struct MetalPipeline {
    MTL::RenderPipelineState *pipeline;
    MTL::DepthStencilState   *depth_state;
};

struct MetalSampler {
    MTL::SamplerState *sampler;
};

struct MetalBackend {
    MTL::Device               *device    = nullptr;
    MTL::CommandQueue         *cmd_queue = nullptr;
    MTL::CommandBuffer        *cmd_buf   = nullptr;
    MTL::RenderCommandEncoder *encoder   = nullptr;
    CA::MetalLayer            *layer     = nullptr;
    CA::MetalDrawable         *drawable  = nullptr;
    MTL::Texture              *depth_tex = nullptr;
    MTL::BinaryArchive        *archive   = nullptr;

    MTL::Buffer               *volatile current_ib          = nullptr;
    MTL::IndexType              current_ib_type      = MTL::IndexTypeUInt16;

    Slotmap<MetalBuffer>       buffers;
    Slotmap<MetalTexture>      textures;
    Slotmap<MetalPipeline>     pipelines;
    Slotmap<MetalSampler>      samplers;

    uint32_t                   frame_index = 0;

    // Init — creates device, command queue, layer
    Expected<void, RHIError>   init(void *metal_layer) noexcept {
        if (cmd_queue) {
            return {};
        }
        layer  = static_cast<CA::MetalLayer *>(metal_layer);
        if (!layer) {
            return make_unexpected(RHIError::BackendError);
        }
        device = layer->device();
        if (!device) {
            return make_unexpected(RHIError::BackendError);
        }

        layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        layer->setFramebufferOnly(true);
        layer->setMaximumDrawableCount(3);

        cmd_queue = device->newCommandQueue();
        if (!cmd_queue) {
            return make_unexpected(RHIError::BackendError);
        }

        current_ib      = nullptr;
        current_ib_type = MTL::IndexTypeUInt16;
        frame_index     = 0;
        return {};
    }

    void shutdown() noexcept {
        cmd_queue->release();
        if (depth_tex) {
            depth_tex->release();
        }
    }

    // Resource creation
    Expected<BufferHandle, RHIError> create_buffer(const BufferDesc &desc) noexcept {
        MTL::ResourceOptions opts = MTL::ResourceStorageModeShared;
        if (desc.type == BufferType::Storage) {
            opts = MTL::ResourceStorageModePrivate;
        }

        MTL::Buffer *buf = device->newBuffer(desc.size, opts);
        if (!buf) {
            return make_unexpected(RHIError::OutOfMemory);
        }

        MetalBuffer mb = {buf, desc.size, desc.type};
        SlotHandle  sh = buffers.emplace(mb);
        return BufferHandle{sh};
    }

    Expected<TextureHandle, RHIError> create_texture(const TextureDesc &desc) noexcept {
        MTL::TextureDescriptor *td = MTL::TextureDescriptor::texture2DDescriptor(to_metal_format(desc.format), desc.width, desc.height, desc.mip_levels > 1);
        td->setStorageMode(MTL::StorageModeShared);
        td->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);

        MTL::Texture *tex = device->newTexture(td);
        if (!tex) {
            return make_unexpected(RHIError::OutOfMemory);
        }

        MetalTexture mt = {tex, desc.width, desc.height, desc.format};
        SlotHandle   sh = textures.emplace(mt);
        return TextureHandle{sh};
    }

    Expected<SamplerHandle, RHIError> create_sampler(const SamplerDesc &desc) noexcept {
        MTL::SamplerDescriptor *sd = MTL::SamplerDescriptor::alloc()->init();
        sd->setMinFilter(desc.min_filter == SamplerFilter::Linear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
        sd->setMagFilter(desc.mag_filter == SamplerFilter::Linear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
        sd->setSAddressMode(to_metal_address(desc.address_u));
        sd->setTAddressMode(to_metal_address(desc.address_v));
        sd->setMaxAnisotropy(static_cast<NS::UInteger>(desc.max_anisotropy));

        MTL::SamplerState *samp = device->newSamplerState(sd);
        sd->release();

        MetalSampler ms = {samp};
        SlotHandle   sh = samplers.emplace(ms);
        return SamplerHandle{sh};
    }

    Expected<PipelineHandle, RHIError> create_pipeline(const PipelineDesc &pdesc) noexcept {
        MTL::RenderPipelineDescriptor *rpd    = MTL::RenderPipelineDescriptor::alloc()->init();

        // Vertex shader
        NS::String                    *vs_src = NS::String::string(static_cast<const char *>(pdesc.vertex_shader.code), NS::UTF8StringEncoding);
        NS::Error                     *err    = nullptr;
        MTL::Library                  *vs_lib = device->newLibrary(vs_src, nullptr, &err);
        if (!vs_lib) {
            if (err) {
                const char *emsg = err->localizedDescription()->utf8String();
                { char buf[4096]; int n = snprintf(buf, sizeof(buf), "VS COMPILE ERROR (entry=%s): %s\n", pdesc.vertex_shader.entry, emsg ? emsg : "???"); write(2, buf, (size_t)n); }
            } else {
                { char buf[256]; int n = snprintf(buf, sizeof(buf), "VS COMPILE FAIL (entry=%s) — no error info\n", pdesc.vertex_shader.entry); write(2, buf, (size_t)n); }
            }
            return make_unexpected(RHIError::ShaderCompileFail);
        }
        NS::String *vs_entry = NS::String::string(pdesc.vertex_shader.entry, NS::UTF8StringEncoding);
        rpd->setVertexFunction(vs_lib->newFunction(vs_entry));
        vs_lib->release();

        // Fragment shader
        NS::String   *fs_src = NS::String::string(static_cast<const char *>(pdesc.fragment_shader.code), NS::UTF8StringEncoding);
        MTL::Library *fs_lib = device->newLibrary(fs_src, nullptr, &err);
        if (!fs_lib) {
            if (err) {
                const char *emsg = err->localizedDescription()->utf8String();
                { char buf[4096]; int n = snprintf(buf, sizeof(buf), "FS COMPILE ERROR (entry=%s): %s\n", pdesc.fragment_shader.entry, emsg ? emsg : "???"); write(2, buf, (size_t)n); }
            } else {
                { char buf[256]; int n = snprintf(buf, sizeof(buf), "FS COMPILE FAIL (entry=%s) — no error info\n", pdesc.fragment_shader.entry); write(2, buf, (size_t)n); }
            }
            return make_unexpected(RHIError::ShaderCompileFail);
        }
        NS::String *fs_entry = NS::String::string(pdesc.fragment_shader.entry, NS::UTF8StringEncoding);
        rpd->setFragmentFunction(fs_lib->newFunction(fs_entry));
        fs_lib->release();

        // Vertex descriptor from vertex_attrs
        MTL::VertexDescriptor *vd         = MTL::VertexDescriptor::alloc()->init();
        uint32_t               max_stride = 0;
        for (uint8_t i = 0; i < pdesc.vertex_attr_count; ++i) {
            auto &attr = pdesc.vertex_attrs[i];
            vd->attributes()->object(attr.location)->setFormat(to_metal_vertex_format(attr.format));
            vd->attributes()->object(attr.location)->setOffset(attr.offset);
            vd->attributes()->object(attr.location)->setBufferIndex(0);
            if (attr.stride > max_stride) {
                max_stride = attr.stride;
            }
        }
        if (pdesc.vertex_attr_count > 0) {
            vd->layouts()->object(0)->setStride(max_stride);
            vd->layouts()->object(0)->setStepFunction(MTL::VertexStepFunctionPerVertex);
        }
        rpd->setVertexDescriptor(vd);

        // Color attachments
        for (uint8_t i = 0; i < pdesc.color_count; ++i) {
            auto ca = rpd->colorAttachments()->object(i);
            ca->setPixelFormat(to_metal_format(pdesc.color_formats[i]));
            ca->setBlendingEnabled(true);
            ca->setSourceRGBBlendFactor(to_metal_blend(pdesc.src_blend));
            ca->setDestinationRGBBlendFactor(to_metal_blend(pdesc.dst_blend));
            ca->setRgbBlendOperation(to_metal_blend_op(pdesc.blend_op));
            ca->setSourceAlphaBlendFactor(to_metal_blend(pdesc.src_blend));
            ca->setDestinationAlphaBlendFactor(to_metal_blend(pdesc.dst_blend));
            ca->setAlphaBlendOperation(to_metal_blend_op(pdesc.blend_op));
        }

        // Depth attachment
        if (pdesc.depth_format != static_cast<PixelFormat>(0)) {
            rpd->setDepthAttachmentPixelFormat(to_metal_format(pdesc.depth_format));
        }

        // Depth-stencil state
        MTL::DepthStencilState *ds_state = nullptr;
        if (pdesc.depth_test || pdesc.depth_write) {
            MTL::DepthStencilDescriptor *dsd = MTL::DepthStencilDescriptor::alloc()->init();
            dsd->setDepthCompareFunction(pdesc.depth_test ? to_metal_compare(pdesc.depth_compare) : MTL::CompareFunctionAlways);
            dsd->setDepthWriteEnabled(pdesc.depth_write);
            ds_state = device->newDepthStencilState(dsd);
            dsd->release();
        }

        // Rasterization
        switch (pdesc.prim_type) {
        case PrimitiveType::Triangle:
        case PrimitiveType::TriangleStrip:
            rpd->setInputPrimitiveTopology(MTL::PrimitiveTopologyClassTriangle);
            break;
        case PrimitiveType::Line:
            rpd->setInputPrimitiveTopology(MTL::PrimitiveTopologyClassLine);
            break;
        case PrimitiveType::Point:
            rpd->setInputPrimitiveTopology(MTL::PrimitiveTopologyClassPoint);
            break;
        }

        rpd->setRasterSampleCount(1);

        // Create pipeline
        MTL::RenderPipelineState *pipeline = device->newRenderPipelineState(rpd, &err);
        rpd->release();
        if (!pipeline) {
            if (err) {
                const char *emsg = err->localizedDescription()->utf8String();
                { char buf[4096]; int n = snprintf(buf, sizeof(buf), "PIPELINE COMPILE ERROR: %s\n", emsg ? emsg : "???"); write(2, buf, (size_t)n); }
            } else {
                { char buf[256]; int n = snprintf(buf, sizeof(buf), "PIPELINE COMPILE FAIL — no error info\n"); write(2, buf, (size_t)n); }
            }
            return make_unexpected(RHIError::PipelineCompileFail);
        }

        MetalPipeline mp = {pipeline, ds_state};
        SlotHandle    sh = pipelines.emplace(mp);
        return PipelineHandle{sh};
    }

    void destroy_buffer(BufferHandle handle) noexcept {
        auto *buf = buffers.get(handle.handle);
        if (buf) {
            buf->buffer->release();
        }
        buffers.free(handle.handle);
    }
    void destroy_texture(TextureHandle handle) noexcept {
        auto *tex = textures.get(handle.handle);
        if (tex) {
            tex->texture->release();
        }
        textures.free(handle.handle);
    }
    void destroy_sampler(SamplerHandle handle) noexcept {
        auto *samp = samplers.get(handle.handle);
        if (samp) {
            samp->sampler->release();
        }
        samplers.free(handle.handle);
    }
    void destroy_pipeline(PipelineHandle handle) noexcept {
        auto *pl = pipelines.get(handle.handle);
        if (pl) {
            pl->pipeline->release();
            if (pl->depth_state) {
                pl->depth_state->release();
            }
        }
        pipelines.free(handle.handle);
    }

    Expected<void, RHIError> update_buffer(BufferHandle handle, const void *data, uint32_t offset, uint32_t size) noexcept {
        auto *buf = buffers.get(handle.handle);
        if (!buf) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        memcpy(static_cast<uint8_t *>(buf->buffer->contents()) + offset, data, size);
        return {};
    }

    Expected<void, RHIError> update_texture(TextureHandle handle, const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t mip,
                                            uint32_t slice) noexcept {
        auto *tex = textures.get(handle.handle);
        if (!tex) {
            return make_unexpected(RHIError::InvalidHandle);
        }

        MTL::Region  region(x, y, w, h);
        NS::UInteger bytes_per_row   = bytes_per_row_for_format(tex->format, w);
        NS::UInteger bytes_per_image = bytes_per_image_for_format(tex->format, w, h);
        tex->texture->replaceRegion(region, mip, slice, data, bytes_per_row, bytes_per_image);
        return {};
    }

    Expected<void, RHIError> begin_frame() noexcept {
        cmd_buf  = cmd_queue->commandBuffer();
        drawable = layer->nextDrawable();
        if (!drawable) {
            return make_unexpected(RHIError::DeviceLost);
        }
        return {};
    }

    void                     resize(uint32_t w, uint32_t h) noexcept { layer->setDrawableSize(CGSizeMake(w, h)); }

    Expected<void, RHIError> end_frame() noexcept {
        if (encoder) {
            encoder->endEncoding();
            encoder = nullptr;
        }
        cmd_buf->presentDrawable(drawable);
        cmd_buf->commit();
        ++frame_index;
        return {};
    }

    Expected<void, RHIError> begin_pass(const PassDesc &pass) noexcept {
        MTL::RenderPassDescriptor *rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
        auto                       ca  = rpd->colorAttachments()->object(0);
        ca->setTexture(drawable->texture());
        ca->setLoadAction(pass.color_load == LoadOp::Clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
        ca->setStoreAction(MTL::StoreActionStore);
        ca->setClearColor(MTL::ClearColor::Make(pass.clear_color[0], pass.clear_color[1], pass.clear_color[2], pass.clear_color[3]));

        encoder         = cmd_buf->renderCommandEncoder(rpd);
        current_ib      = nullptr;
        current_ib_type = MTL::IndexTypeUInt16;
        return {};
    }

    Expected<void, RHIError> end_pass() noexcept {
        if (encoder) {
            encoder->endEncoding();
            encoder = nullptr;
        }
        return {};
    }

    Expected<void, RHIError> bind_pipeline(PipelineHandle handle) noexcept {
        auto *pl = pipelines.get(handle.handle);
        if (!pl) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        encoder->setRenderPipelineState(pl->pipeline);
        if (pl->depth_state) {
            encoder->setDepthStencilState(pl->depth_state);
        }
        return {};
    }

    Expected<void, RHIError> bind_vertex_buffers(BufferHandle *handles, uint32_t count, const uint64_t *offsets, const uint64_t * /*strides*/,
                                                 const uint32_t *bindings = nullptr) noexcept {
        for (uint32_t i = 0; i < count; ++i) {
            auto *buf = buffers.get(handles[i].handle);
            if (buf) {
                uint32_t idx = bindings ? bindings[i] : i;
                uint64_t off = offsets ? offsets[i] : 0;
                encoder->setVertexBuffer(buf->buffer, off, idx);
            }
        }
        return {};
    }

    uint64_t                  current_ib_offset   = 0;

    Expected<void, RHIError> bind_index_buffer(BufferHandle handle, IndexType type, uint64_t offset = 0) noexcept {
        auto *buf = buffers.get(handle.handle);
        if (!buf) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        current_ib        = buf->buffer;
        current_ib_type   = (type == IndexType::Uint32) ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
        current_ib_offset = offset;
        return {};
    }

    Expected<void, RHIError> draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) noexcept {
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, first_vertex, vertex_count, instance_count, first_instance);
        return {};
    }

    Expected<void, RHIError> draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset = 0) noexcept {
        if (!this->encoder || !this->current_ib) {
            return {};
        }
        uint32_t index_size = (this->current_ib_type == MTL::IndexTypeUInt16) ? 2 : 4;
        this->encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, index_count, this->current_ib_type, this->current_ib, current_ib_offset + first_index * index_size, instance_count, vertex_offset, 0);
        return {};
    }

    Expected<void, RHIError> bind_fragment_texture(TextureHandle handle, uint32_t index) noexcept {
        auto *tex = textures.get(handle.handle);
        if (!tex) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        encoder->setFragmentTexture(tex->texture, index);
        return {};
    }

    Expected<void, RHIError> bind_fragment_sampler(SamplerHandle handle, uint32_t index) noexcept {
        auto *samp = samplers.get(handle.handle);
        if (!samp) {
            return make_unexpected(RHIError::InvalidHandle);
        }
        encoder->setFragmentSamplerState(samp->sampler, index);
        return {};
    }

    Expected<void, RHIError> set_scissor(int16_t x, int16_t y, uint16_t w, uint16_t h) noexcept {
        MTL::ScissorRect rect{};
        rect.x      = static_cast<NS::UInteger>(x);
        rect.y      = static_cast<NS::UInteger>(y);
        rect.width  = static_cast<NS::UInteger>(w);
        rect.height = static_cast<NS::UInteger>(h);
        encoder->setScissorRect(rect);
        return {};
    }

  private:
    static MTL::PixelFormat to_metal_format(PixelFormat fmt) noexcept {
        switch (fmt) {
        case PixelFormat::R8_UNORM:
            return MTL::PixelFormatR8Unorm;
        case PixelFormat::R8G8B8A8_UNORM:
            return MTL::PixelFormatRGBA8Unorm;
        case PixelFormat::R8G8B8A8_SRGB:
            return MTL::PixelFormatRGBA8Unorm_sRGB;
        case PixelFormat::B8G8R8A8_UNORM:
            return MTL::PixelFormatBGRA8Unorm;
        case PixelFormat::B8G8R8A8_SRGB:
            return MTL::PixelFormatBGRA8Unorm_sRGB;
        case PixelFormat::D32_FLOAT:
            return MTL::PixelFormatDepth32Float;
        case PixelFormat::ASTC_4x4:
            return MTL::PixelFormatASTC_4x4_LDR;
        default:
            return MTL::PixelFormatBGRA8Unorm;
        }
    }

    static NS::UInteger bytes_per_row_for_format(PixelFormat fmt, uint32_t width) noexcept {
        switch (fmt) {
        case PixelFormat::R8_UNORM:
            return width;
        case PixelFormat::R16G16B16A16_FLOAT:
            return width * 8;
        case PixelFormat::R32G32B32A32_FLOAT:
            return width * 16;
        case PixelFormat::D24_UNORM_S8_UINT:
        case PixelFormat::D32_FLOAT:
            return width * 4;
        case PixelFormat::D32_FLOAT_S8_UINT:
            return width * 8;
        case PixelFormat::ASTC_4x4:
            return ((width + 3) / 4) * 16;
        case PixelFormat::ASTC_6x6:
            return ((width + 5) / 6) * 16;
        case PixelFormat::ASTC_8x8:
            return ((width + 7) / 8) * 16;
        case PixelFormat::ETC2_RGB8:
            return ((width + 3) / 4) * 8;
        case PixelFormat::ETC2_RGBA8:
            return ((width + 3) / 4) * 16;
        default:
            return width * 4;
        }
    }

    static NS::UInteger bytes_per_image_for_format(PixelFormat fmt, uint32_t width, uint32_t height) noexcept {
        NS::UInteger row = bytes_per_row_for_format(fmt, width);
        switch (fmt) {
        case PixelFormat::ASTC_4x4:
        case PixelFormat::ETC2_RGB8:
        case PixelFormat::ETC2_RGBA8:
            return row * ((height + 3) / 4);
        case PixelFormat::ASTC_6x6:
            return row * ((height + 5) / 6);
        case PixelFormat::ASTC_8x8:
            return row * ((height + 7) / 8);
        default:
            return row * height;
        }
    }

    static MTL::VertexFormat to_metal_vertex_format(PixelFormat fmt) noexcept {
        switch (fmt) {
        case PixelFormat::R8G8B8A8_UNORM:
            return MTL::VertexFormatUChar4Normalized;
        case PixelFormat::R16G16_FLOAT:
            return MTL::VertexFormatHalf2;
        case PixelFormat::R16G16B16A16_FLOAT:
            return MTL::VertexFormatHalf4;
        case PixelFormat::R32G32B32_FLOAT:
            return MTL::VertexFormatFloat3;
        case PixelFormat::R32G32B32A32_FLOAT:
            return MTL::VertexFormatFloat4;
        case PixelFormat::R8_UNORM:
            return MTL::VertexFormatUCharNormalized;
        default:
            return MTL::VertexFormatHalf2;
        }
    }

    static MTL::BlendFactor to_metal_blend(BlendFactor factor) noexcept {
        switch (factor) {
        case BlendFactor::Zero:
            return MTL::BlendFactorZero;
        case BlendFactor::One:
            return MTL::BlendFactorOne;
        case BlendFactor::SrcAlpha:
            return MTL::BlendFactorSourceAlpha;
        case BlendFactor::OneMinusSrcAlpha:
            return MTL::BlendFactorOneMinusSourceAlpha;
        case BlendFactor::DstAlpha:
            return MTL::BlendFactorDestinationAlpha;
        case BlendFactor::OneMinusDstAlpha:
            return MTL::BlendFactorOneMinusDestinationAlpha;
        case BlendFactor::SrcColor:
            return MTL::BlendFactorSourceColor;
        case BlendFactor::OneMinusSrcColor:
            return MTL::BlendFactorOneMinusSourceColor;
        default:
            return MTL::BlendFactorOne;
        }
    }

    static MTL::BlendOperation to_metal_blend_op(BlendOp op) noexcept {
        switch (op) {
        case BlendOp::Add:
            return MTL::BlendOperationAdd;
        case BlendOp::Subtract:
            return MTL::BlendOperationSubtract;
        case BlendOp::ReverseSubtract:
            return MTL::BlendOperationReverseSubtract;
        case BlendOp::Min:
            return MTL::BlendOperationMin;
        case BlendOp::Max:
            return MTL::BlendOperationMax;
        default:
            return MTL::BlendOperationAdd;
        }
    }

    static MTL::CompareFunction to_metal_compare(CompareOp op) noexcept {
        switch (op) {
        case CompareOp::Never:
            return MTL::CompareFunctionNever;
        case CompareOp::Less:
            return MTL::CompareFunctionLess;
        case CompareOp::Equal:
            return MTL::CompareFunctionEqual;
        case CompareOp::LessEqual:
            return MTL::CompareFunctionLessEqual;
        case CompareOp::Greater:
            return MTL::CompareFunctionGreater;
        case CompareOp::NotEqual:
            return MTL::CompareFunctionNotEqual;
        case CompareOp::GreaterEqual:
            return MTL::CompareFunctionGreaterEqual;
        case CompareOp::Always:
            return MTL::CompareFunctionAlways;
        default:
            return MTL::CompareFunctionAlways;
        }
    }

    static MTL::SamplerAddressMode to_metal_address(SamplerAddress addr) noexcept {
        switch (addr) {
        case SamplerAddress::Repeat:
            return MTL::SamplerAddressModeRepeat;
        case SamplerAddress::ClampToEdge:
            return MTL::SamplerAddressModeClampToEdge;
        case SamplerAddress::MirroredRepeat:
            return MTL::SamplerAddressModeMirrorRepeat;
        default:
            return MTL::SamplerAddressModeClampToEdge;
        }
    }
};

static_assert(RHI_Backend<MetalBackend>, "MetalBackend must satisfy RHI_Backend concept");

#endif // USE_METAL_BACKEND

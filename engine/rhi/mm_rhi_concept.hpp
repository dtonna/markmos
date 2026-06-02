// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_handle.hpp"
#include "../core/mm_expected.hpp"
#include <cstdint>
#include <cstddef>
#include <concepts>

// RHI Backend Concept — zero virtual, compile-time polymorphism
// @cache_reason concept + template = no vtable lookup, inline dispatch, monomorphized
// @zero_virtual Concept-based compile-time interface — no vtable, no inheritance
// @fallback Expected-like return via Expected<T,E> fallback when std::expected unavailable

enum class ShaderStage : uint8_t { Vertex, Fragment, Compute };

enum class BufferType : uint8_t {
    Vertex, Index, Uniform, Storage, Indirect, Staging
};

enum class TextureType : uint8_t {
    Tex2D, Tex2DArray, CubeMap
};

enum class PixelFormat : uint8_t {
    R8_UNORM, R8G8B8A8_UNORM, R8G8B8A8_SRGB,
    B8G8R8A8_UNORM, B8G8R8A8_SRGB,
    R16G16_FLOAT,
    R32G32B32_FLOAT, R32G32B32A32_FLOAT, R16G16B16A16_FLOAT,
    D32_FLOAT, D24_UNORM_S8_UINT,
    D32_FLOAT_S8_UINT,
    ASTC_4x4, ASTC_6x6, ASTC_8x8,
    ETC2_RGB8, ETC2_RGBA8
};

enum class PrimitiveType : uint8_t {
    Triangle, TriangleStrip, Line, Point
};

enum class IndexType : uint8_t {
    Uint16, Uint32
};

enum class SamplerFilter : uint8_t {
    Nearest, Linear
};

enum class SamplerAddress : uint8_t {
    Repeat, ClampToEdge, MirroredRepeat
};

enum class LoadOp : uint8_t { Load, Clear, DontCare };
enum class StoreOp : uint8_t { Store, DontCare };

enum class BlendFactor : uint8_t {
    Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha,
    SrcColor, OneMinusSrcColor
};

enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };

enum class CompareOp : uint8_t {
    Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always
};

enum class CullMode : uint8_t { None, Front, Back };

// Descriptors — flat structs, no inheritance
struct BufferDesc {
    BufferType type;
    uint32_t   size;
    uint32_t   stride;
    bool       cpu_visible;
};

struct TextureDesc {
    TextureType type;
    PixelFormat format;
    uint16_t    width, height, depth;
    uint16_t    mip_levels;
    uint8_t     array_layers;
};

struct SamplerDesc {
    SamplerFilter min_filter, mag_filter, mip_filter;
    SamplerAddress address_u, address_v, address_w;
    CompareOp compare;
    float      max_anisotropy;
};

struct ShaderDesc {
    ShaderStage stage;
    const void* code;
    size_t      code_size;
    const char* entry;
};

enum class DescriptorType : uint8_t {
    UniformBuffer, CombinedImageSampler, StorageBuffer
};

struct DescriptorBinding {
    uint32_t       binding;
    DescriptorType type;
    uint32_t       stage_mask;  // bit 0=vertex, 1=fragment, 2=compute
    uint32_t       count;
};

struct VertexAttribute {
    uint32_t location;
    PixelFormat format;
    uint32_t offset;
    uint32_t stride;
};

struct PipelineDesc {
    ShaderDesc         vertex_shader;
    ShaderDesc         fragment_shader;
    PrimitiveType      prim_type;
    CullMode           cull_mode;
    BlendFactor        src_blend, dst_blend;
    BlendOp            blend_op;
    bool               depth_test;
    bool               depth_write;
    CompareOp          depth_compare;
    PixelFormat        color_formats[4];
    uint8_t            color_count;
    PixelFormat        depth_format;
    VertexAttribute    vertex_attrs[16];
    uint8_t            vertex_attr_count;
    DescriptorBinding  descriptor_bindings[8];
    uint8_t            descriptor_count;
};

struct PassDesc {
    float      clear_color[4];
    float      clear_depth;
    uint8_t    clear_stencil;
    LoadOp     color_load, depth_load;
    StoreOp    color_store, depth_store;
};

// RHIError type — small, fits in register
enum class RHIError : uint32_t {
    None = 0,
    OutOfMemory,
    InvalidHandle,
    BackendError,
    ShaderCompileFail,
    PipelineCompileFail,
    DeviceLost,
};

// RHI Backend Concept — per spec v4.6, uses ExpectedLike for return type checking
// Destructor must be noexcept (may hold RAII resources), not necessarily trivially destructible
template<typename T>
concept RHI_Backend = requires(T t, const BufferDesc& bd, const TextureDesc& td,
                                const SamplerDesc& sd, const PipelineDesc& pd,
                                const PassDesc& pass, BufferHandle bh, TextureHandle th,
                                PipelineHandle ph, SamplerHandle sh,
                                const void* data, uint32_t offset) {
    // Resource creation
    requires ExpectedLike<decltype(t.create_buffer(bd)), BufferHandle, RHIError>;
    requires ExpectedLike<decltype(t.create_texture(td)), TextureHandle, RHIError>;
    requires ExpectedLike<decltype(t.create_sampler(sd)), SamplerHandle, RHIError>;
    requires ExpectedLike<decltype(t.create_pipeline(pd)), PipelineHandle, RHIError>;

    // Resource destruction
    { t.destroy_buffer(bh) }                    -> std::same_as<void>;
    { t.destroy_texture(th) }                   -> std::same_as<void>;
    { t.destroy_sampler(sh) }                   -> std::same_as<void>;
    { t.destroy_pipeline(ph) }                  -> std::same_as<void>;

    // Data upload
    requires ExpectedLike<decltype(t.update_buffer(bh, data, offset, offset)), void, RHIError>;
    requires ExpectedLike<decltype(t.update_texture(th, data, 0, 0, 0, 0, 0, 0)), void, RHIError>;

    // Frame lifecycle
    requires ExpectedLike<decltype(t.begin_frame()), void, RHIError>;
    requires ExpectedLike<decltype(t.end_frame()), void, RHIError>;

    // Drawing
    requires ExpectedLike<decltype(t.begin_pass(pass)), void, RHIError>;
    requires ExpectedLike<decltype(t.end_pass()), void, RHIError>;
    requires ExpectedLike<decltype(t.bind_pipeline(ph)), void, RHIError>;
    requires ExpectedLike<decltype(t.bind_vertex_buffers(&bh, 1, nullptr, nullptr)), void, RHIError>;
    requires ExpectedLike<decltype(t.bind_index_buffer(bh, IndexType::Uint32)), void, RHIError>;
    requires ExpectedLike<decltype(t.bind_uniform_buffer(bh, 0)), void, RHIError>;
    requires ExpectedLike<decltype(t.draw(0, 0, 0, 0)), void, RHIError>;
    requires ExpectedLike<decltype(t.draw_indexed(0, 0, 0)), void, RHIError>;

    // Scissor test
    requires ExpectedLike<decltype(t.set_scissor(0, 0, 0, 0)), void, RHIError>;

    // Destructor must be noexcept (may hold RAII resources)
    requires std::is_nothrow_destructible_v<T>;
};

// Backend selection — compile-time only
// @cache_reason no runtime branch, dead code elimination by linker
// Single source of truth: CMake defines the backend macro per platform.
// This fallback only applies when building without CMake (e.g. header lints).
#if !defined(USE_METAL_BACKEND) && !defined(USE_VULKAN_BACKEND)
    #if defined(TARGET_IOS) || defined(TARGET_MACOS)
        #define USE_METAL_BACKEND
    #elif defined(TARGET_ANDROID)
        #define USE_VULKAN_BACKEND
    #else
        #if defined(__APPLE__)
            #define USE_METAL_BACKEND
        #else
            #define USE_VULKAN_BACKEND
        #endif
    #endif
#endif

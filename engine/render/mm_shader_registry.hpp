// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../rhi/mm_rhi_concept.hpp"
#include <cstdint>

// Unified shader access: returns ShaderDesc for the active backend
// Metal  → inline MSL strings from mm_shaders.hpp
// Vulkan → embedded SPIR-V from glslangValidator

#if defined(USE_VULKAN_BACKEND)

// Embedded SPIR-V symbols (compiled by glslangValidator → embed_spv.py)
extern const uint32_t sprite_vert_spv[];
extern const uint32_t sprite_vert_spv_count;
extern const uint32_t sprite_frag_spv[];
extern const uint32_t sprite_frag_spv_count;

extern const uint32_t sdf_vert_spv[];
extern const uint32_t sdf_vert_spv_count;
extern const uint32_t sdf_frag_spv[];
extern const uint32_t sdf_frag_spv_count;

extern const uint32_t particle_vert_spv[];
extern const uint32_t particle_vert_spv_count;
extern const uint32_t particle_frag_spv[];
extern const uint32_t particle_frag_spv_count;

extern const uint32_t cube_vert_spv[];
extern const uint32_t cube_vert_spv_count;
extern const uint32_t cube_frag_spv[];
extern const uint32_t cube_frag_spv_count;

namespace shader {

inline ShaderDesc sprite_vertex() noexcept {
    return { ShaderStage::Vertex, sprite_vert_spv,
             sprite_vert_spv_count * sizeof(uint32_t), "main" };
}
inline ShaderDesc sprite_fragment() noexcept {
    return { ShaderStage::Fragment, sprite_frag_spv,
             sprite_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc sdf_vertex() noexcept {
    return { ShaderStage::Vertex, sdf_vert_spv,
             sdf_vert_spv_count * sizeof(uint32_t), "main" };
}
inline ShaderDesc sdf_fragment() noexcept {
    return { ShaderStage::Fragment, sdf_frag_spv,
             sdf_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc particle_vertex() noexcept {
    return { ShaderStage::Vertex, particle_vert_spv,
             particle_vert_spv_count * sizeof(uint32_t), "main" };
}
inline ShaderDesc particle_fragment() noexcept {
    return { ShaderStage::Fragment, particle_frag_spv,
             particle_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc cube_vertex() noexcept {
    return { ShaderStage::Vertex, cube_vert_spv,
             cube_vert_spv_count * sizeof(uint32_t), "main" };
}
inline ShaderDesc cube_fragment() noexcept {
    return { ShaderStage::Fragment, cube_frag_spv,
             cube_frag_spv_count * sizeof(uint32_t), "main" };
}

} // namespace shader

#else
// Metal backend — inline MSL strings
#include "mm_shaders.hpp"

namespace shader {

inline ShaderDesc sprite_vertex() noexcept {
    return { ShaderStage::Vertex, shader::sprite_vertex_msl,
             shader::sprite_vertex_msl_size, "sprite_vertex_main" };
}
inline ShaderDesc sprite_fragment() noexcept {
    return { ShaderStage::Fragment, shader::sprite_fragment_msl,
             shader::sprite_fragment_msl_size, "sprite_fragment_main" };
}

inline ShaderDesc sdf_vertex() noexcept {
    return { ShaderStage::Vertex, shader::sdf_vertex_msl,
             shader::sdf_vertex_msl_size, "sdf_vertex_main" };
}
inline ShaderDesc sdf_fragment() noexcept {
    return { ShaderStage::Fragment, shader::sdf_fragment_msl,
             shader::sdf_fragment_msl_size, "sdf_fragment_main" };
}

inline ShaderDesc particle_vertex() noexcept {
    return { ShaderStage::Vertex, shader::particle_vertex_msl,
             shader::particle_vertex_msl_size, "particle_vertex_main" };
}
inline ShaderDesc particle_fragment() noexcept {
    return { ShaderStage::Fragment, shader::particle_fragment_msl,
             shader::particle_fragment_msl_size, "particle_fragment_main" };
}

inline ShaderDesc cube_vertex() noexcept {
    return { ShaderStage::Vertex, shader::cube_vertex_msl,
             shader::cube_vertex_msl_size, "cube_vertex_main" };
}
inline ShaderDesc cube_fragment() noexcept {
    return { ShaderStage::Fragment, shader::cube_fragment_msl,
             shader::cube_fragment_msl_size, "cube_fragment_main" };
}

} // namespace shader

#endif

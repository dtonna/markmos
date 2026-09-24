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

extern const uint32_t rounded_sprite_vert_spv[];
extern const uint32_t rounded_sprite_vert_spv_count;
extern const uint32_t rounded_sprite_frag_spv[];
extern const uint32_t rounded_sprite_frag_spv_count;

extern const uint32_t screen_quad_vert_spv[];
extern const uint32_t screen_quad_vert_spv_count;

extern const uint32_t blur_frag_spv[];
extern const uint32_t blur_frag_spv_count;

extern const uint32_t color_grade_frag_spv[];
extern const uint32_t color_grade_frag_spv_count;

extern const uint32_t sprite_outline_frag_spv[];
extern const uint32_t sprite_outline_frag_spv_count;

extern const uint32_t clip_rect_frag_spv[];
extern const uint32_t clip_rect_frag_spv_count;

extern const uint32_t dissolve_frag_spv[];
extern const uint32_t dissolve_frag_spv_count;

extern const uint32_t grayscale_frag_spv[];
extern const uint32_t grayscale_frag_spv_count;

extern const uint32_t button_frag_spv[];
extern const uint32_t button_frag_spv_count;

extern const uint32_t normal_derive_frag_spv[];
extern const uint32_t normal_derive_frag_spv_count;

extern const uint32_t normal_map_frag_spv[];
extern const uint32_t normal_map_frag_spv_count;

extern const uint32_t cartoon_frag_spv[];
extern const uint32_t cartoon_frag_spv_count;

extern const uint32_t plastic_frag_spv[];
extern const uint32_t plastic_frag_spv_count;

extern const uint32_t glow_pulse_frag_spv[];
extern const uint32_t glow_pulse_frag_spv_count;

extern const uint32_t gold_border_frag_spv[];
extern const uint32_t gold_border_frag_spv_count;

extern const uint32_t gold_stay_frag_spv[];
extern const uint32_t gold_stay_frag_spv_count;

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

inline ShaderDesc rounded_sprite_vertex() noexcept {
    return { ShaderStage::Vertex, rounded_sprite_vert_spv,
             rounded_sprite_vert_spv_count * sizeof(uint32_t), "main" };
}
inline ShaderDesc rounded_sprite_fragment() noexcept {
    return { ShaderStage::Fragment, rounded_sprite_frag_spv,
             rounded_sprite_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc screen_quad_vertex() noexcept {
    return { ShaderStage::Vertex, screen_quad_vert_spv,
             screen_quad_vert_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc blur_fragment() noexcept {
    return { ShaderStage::Fragment, blur_frag_spv,
             blur_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc color_grade_fragment() noexcept {
    return { ShaderStage::Fragment, color_grade_frag_spv,
             color_grade_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc sprite_outline_fragment() noexcept {
    return { ShaderStage::Fragment, sprite_outline_frag_spv,
             sprite_outline_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc clip_rect_fragment() noexcept {
    return { ShaderStage::Fragment, clip_rect_frag_spv,
             clip_rect_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc dissolve_fragment() noexcept {
    return { ShaderStage::Fragment, dissolve_frag_spv,
             dissolve_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc grayscale_fragment() noexcept {
    return { ShaderStage::Fragment, grayscale_frag_spv,
             grayscale_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc button_fragment() noexcept {
    return { ShaderStage::Fragment, button_frag_spv,
             button_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc normal_derive_fragment() noexcept {
    return { ShaderStage::Fragment, normal_derive_frag_spv,
             normal_derive_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc normal_map_fragment() noexcept {
    return { ShaderStage::Fragment, normal_map_frag_spv,
             normal_map_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc cartoon_fragment() noexcept {
    return { ShaderStage::Fragment, cartoon_frag_spv,
             cartoon_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc plastic_fragment() noexcept {
    return { ShaderStage::Fragment, plastic_frag_spv,
             plastic_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc glow_pulse_fragment() noexcept {
    return { ShaderStage::Fragment, glow_pulse_frag_spv,
             glow_pulse_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc gold_border_fragment() noexcept {
    return { ShaderStage::Fragment, gold_border_frag_spv,
             gold_border_frag_spv_count * sizeof(uint32_t), "main" };
}

inline ShaderDesc gold_stay_fragment() noexcept {
    return { ShaderStage::Fragment, gold_stay_frag_spv,
             gold_stay_frag_spv_count * sizeof(uint32_t), "main" };
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

inline ShaderDesc rounded_sprite_vertex() noexcept {
    return { ShaderStage::Vertex, shader::rounded_sprite_vertex_msl,
             shader::rounded_sprite_vertex_msl_size, "rounded_sprite_vertex_main" };
}
inline ShaderDesc rounded_sprite_fragment() noexcept {
    return { ShaderStage::Fragment, shader::rounded_sprite_fragment_msl,
             shader::rounded_sprite_fragment_msl_size, "rounded_sprite_fragment_main" };
}

inline ShaderDesc screen_quad_vertex() noexcept {
    return { ShaderStage::Vertex, shader::screen_quad_vertex_msl,
             shader::screen_quad_vertex_msl_size, "screen_quad_vertex_main" };
}

inline ShaderDesc blur_fragment() noexcept {
    return { ShaderStage::Fragment, shader::blur_fragment_msl,
             shader::blur_fragment_msl_size, "blur_fragment_main" };
}

inline ShaderDesc color_grade_fragment() noexcept {
    return { ShaderStage::Fragment, shader::color_grade_fragment_msl,
             shader::color_grade_fragment_msl_size, "color_grade_fragment_main" };
}

inline ShaderDesc sprite_outline_fragment() noexcept {
    return { ShaderStage::Fragment, shader::sprite_outline_fragment_msl,
             shader::sprite_outline_fragment_msl_size, "sprite_outline_fragment_main" };
}

inline ShaderDesc clip_rect_fragment() noexcept {
    return { ShaderStage::Fragment, shader::clip_rect_fragment_msl,
             shader::clip_rect_fragment_msl_size, "clip_rect_fragment_main" };
}

inline ShaderDesc dissolve_fragment() noexcept {
    return { ShaderStage::Fragment, shader::dissolve_fragment_msl,
             shader::dissolve_fragment_msl_size, "dissolve_fragment_main" };
}

inline ShaderDesc grayscale_fragment() noexcept {
    return { ShaderStage::Fragment, shader::grayscale_fragment_msl,
             shader::grayscale_fragment_msl_size, "grayscale_fragment_main" };
}

inline ShaderDesc button_fragment() noexcept {
    return { ShaderStage::Fragment, shader::button_fragment_msl,
             shader::button_fragment_msl_size, "button_fragment_main" };
}

inline ShaderDesc normal_derive_fragment() noexcept {
    return { ShaderStage::Fragment, shader::normal_derive_fragment_msl,
             shader::normal_derive_fragment_msl_size, "normal_derive_fragment_main" };
}

inline ShaderDesc normal_map_fragment() noexcept {
    return { ShaderStage::Fragment, shader::normal_map_fragment_msl,
             shader::normal_map_fragment_msl_size, "normal_map_fragment_main" };
}

inline ShaderDesc cartoon_fragment() noexcept {
    return { ShaderStage::Fragment, shader::cartoon_fragment_msl,
             shader::cartoon_fragment_msl_size, "cartoon_fragment_main" };
}

inline ShaderDesc plastic_fragment() noexcept {
    return { ShaderStage::Fragment, shader::plastic_fragment_msl,
             shader::plastic_fragment_msl_size, "plastic_fragment_main" };
}

inline ShaderDesc glow_pulse_fragment() noexcept {
    return { ShaderStage::Fragment, shader::glow_pulse_fragment_msl,
             shader::glow_pulse_fragment_msl_size, "glow_pulse_fragment_main" };
}

inline ShaderDesc gold_border_fragment() noexcept {
    return { ShaderStage::Fragment, shader::gold_border_fragment_msl,
             shader::gold_border_fragment_msl_size, "gold_border_fragment_main" };
}

inline ShaderDesc gold_stay_fragment() noexcept {
    return { ShaderStage::Fragment, shader::gold_stay_fragment_msl,
             shader::gold_stay_fragment_msl_size, "gold_stay_fragment_main" };
}

} // namespace shader

#endif

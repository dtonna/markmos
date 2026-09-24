// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_handle.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include <cstdint>
#include <cstring>

// ─── Built-in material types ─────────────────────────────────────
enum class e_material_type : uint8_t {
    SPRITEALPHA,    // alpha blend (default)
    SPRITEADDITIVE, // additive blend
    SPRITEMULTIPLY, // multiply blend
    SPRITEOPAQUE,   // no blending
    SDF,            // SDF text
    PARTICLE,       // particle system
    ROUNDEDSPRITE,  // SDF rounded rect sprite
    SPRITEOUTLINE,  // edge-detection outline
    CLIPRECT,       // screen-space clipping
    DISSOLVE,       // procedural dissolve transition
    GRAYSCALE,      // desaturation
    SDFBUTTON,      // aspect-correct SDF button (pill/circle/rounded rect)
    NORMALDERIVE,   // Sobel normal from albedo + Blinn-Phong (lab D6)
    NORMALMAP,      // tangent-space normal map lighting (lab D7/D8/D9 card)
    CARTOON,        // toon bands + ink edges (lab C)
    PLASTIC,        // gloss + clearcoat + fresnel rim (lab P)
    GLOWPULSE,      // expanding + fading ring (lab G)
    GOLDBORDER,     // static band + breathe (lab Y)
    GOLDSTAY,       // persistent base + gentle pulse (lab S)
    COUNT,
};

// Pulse,          // expanding + fade-out pulse
//     Glow,           // soft bloom glow
//     Sparkle,        // particle sparkle burst
//     Ripple,         // ripple touch effect
//     Confetti,       // confetti fall
//     Trail,          // motion trail

// ─── Material — bindable render state bundle ─────────────────────
struct Material {
    PipelineHandle pipeline;
    TextureHandle  texture;
    SamplerHandle  sampler;

    bool           is_valid() const noexcept { return pipeline.is_valid(); }

    bool           operator==(const Material &o) const noexcept { return pipeline == o.pipeline && texture == o.texture && sampler == o.sampler; }

    bool           operator!=(const Material &o) const noexcept { return !(*this == o); }
};

// ─── Technique — render pass descriptor ─────────────────────────
// Single-pass for now; pass_count reserved for multi-pass (e.g., glow, shadow).
static constexpr uint8_t TECHNIQUE_MAX_PASSES = 4;

struct Technique {
    Material         passes[TECHNIQUE_MAX_PASSES];
    uint8_t          pass_count;
    char             name[32];

    static Technique make_single(const Material &mat, const char *label = "") noexcept {
        Technique t{};
        t.passes[0]  = mat;
        t.pass_count = 1;
        if (label) {
            size_t len = std::strlen(label);
            if (len >= sizeof(t.name)) {
                len = sizeof(t.name) - 1;
            }
            std::memcpy(t.name, label, len);
            t.name[len] = '\0';
        }
        return t;
    }

    const Material &current_pass() const noexcept { return passes[0]; }
};

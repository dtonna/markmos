#pragma once
#include "../rhi/mm_rhi_concept.hpp"
#include "../core/mm_handle.hpp"
#include <cstdint>
#include <cstring>

// ─── Built-in material types ─────────────────────────────────────
enum class MaterialType : uint8_t {
    SpriteAlpha,      // alpha blend (default)
    SpriteAdditive,   // additive blend
    SpriteMultiply,   // multiply blend
    SpriteOpaque,     // no blending
    SDF,              // SDF text
    Particle,         // particle system
    COUNT,
};

// ─── Material — bindable render state bundle ─────────────────────
struct Material {
    PipelineHandle pipeline;
    TextureHandle  texture;
    SamplerHandle  sampler;

    bool is_valid() const noexcept {
        return pipeline.is_valid();
    }

    bool operator==(const Material& o) const noexcept {
        return pipeline == o.pipeline && texture == o.texture && sampler == o.sampler;
    }

    bool operator!=(const Material& o) const noexcept {
        return !(*this == o);
    }
};

// ─── Technique — render pass descriptor ─────────────────────────
// Single-pass for now; pass_count reserved for multi-pass (e.g., glow, shadow).
static constexpr uint8_t TECHNIQUE_MAX_PASSES = 4;

struct Technique {
    Material passes[TECHNIQUE_MAX_PASSES];
    uint8_t  pass_count;
    char     name[32];

    static Technique make_single(const Material& mat, const char* label = "") noexcept {
        Technique t{};
        t.passes[0] = mat;
        t.pass_count = 1;
        if (label) {
            size_t len = std::strlen(label);
            if (len >= sizeof(t.name)) len = sizeof(t.name) - 1;
            std::memcpy(t.name, label, len);
            t.name[len] = '\0';
        }
        return t;
    }

    const Material& current_pass() const noexcept {
        return passes[0];
    }
};

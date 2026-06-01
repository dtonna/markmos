#pragma once
#include "mm_sprite.hpp"
#include "mm_texture_loader.hpp"
#include "../rhi/mm_rhi_concept.hpp"
#include "../core/mm_expected.hpp"
#include "../core/mm_vfs.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>

// ─── Parser: .sprite text format ─────────────────────────────────
// Format:
//   # comment
//   texture <path>
//   filter nearest|linear
//   frame <name> <x> <y> <w> <h> [px] [py]
//   anim <name> [loop]
//     <frame_name> <duration>
//
// Whitespace-delimited, UTF-8 text (ASCII-compatible names).

// Internal: skip whitespace, return ptr to first non-whitespace
static inline const char* skip_ws(const char* p) noexcept {
    while (*p && (*p == ' ' || *p == '\t')) ++p;
    return p;
}

// Internal: skip to end of line
static inline const char* skip_line(const char* p) noexcept {
    while (*p && *p != '\n') ++p;
    if (*p == '\n') ++p;
    return p;
}

// Internal: copy next whitespace-delimited token, returns ptr after token
static inline const char* next_token(const char* p, char* out, uint16_t max_len) noexcept {
    p = skip_ws(p);
    uint16_t i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
        if (i < max_len - 1) out[i++] = *p;
        ++p;
    }
    out[i] = '\0';
    return p;
}

// Internal: check if line starts with a prefix
static inline bool line_starts_with(const char* p, const char* prefix) noexcept {
    p = skip_ws(p);
    while (*prefix) {
        if (*p != *prefix) return false;
        ++p; ++prefix;
    }
    return true;
}

// ─── Sprite atlas error ──────────────────────────────────────────
enum class SpriteError : uint32_t {
    None = 0,
    FileNotFound,
    ParseError,
    TextureLoadFailed,
    TooManyFrames,
    TooManyAnims,
    TooManyAnimFrames,
    BadFilter,
};

// Load sprite atlas from a .sprite config file.
// The config references a texture image which is loaded via texture_load_from_file.
template<RHI_Backend B>
Expected<SpriteAtlas, SpriteError> sprite_atlas_load_from_file(
    B& backend, Vfs& vfs, const char* config_path, bool srgb = false) noexcept
{
    SpriteAtlas atlas{};
    atlas.init();

    VfsBlob blob = vfs.read_bundle(config_path);
    if (!blob.valid()) return make_unexpected(SpriteError::FileNotFound);

    const char* p = static_cast<const char*>(blob.data);
    const char* end = p + blob.size;

    char texture_path[256] = {};
    char filter_str[16] = {};
    bool has_texture = false;

    // Animation parsing state
    SpriteAnim* current_anim = nullptr;
    bool in_anim = false;

    while (p < end) {
        p = skip_ws(p);
        if (!*p || *p == '\n' || *p == '\r') {
            p = skip_line(p);
            continue;
        }

        // Comment
        if (*p == '#') {
            p = skip_line(p);
            continue;
        }

        // `texture <path>`
        if (line_starts_with(p, "texture")) {
            p = skip_ws(p + 7);
            char path[256] = {};
            p = next_token(p, path, 256);
            if (path[0]) {
                std::memcpy(texture_path, path, 256);
                has_texture = true;
            }
            p = skip_line(p);
            continue;
        }

        // `filter <mode>`
        if (line_starts_with(p, "filter")) {
            p = skip_ws(p + 6);
            p = next_token(p, filter_str, 16);
            p = skip_line(p);
            continue;
        }

        // `frame <name> <x> <y> <w> <h> [px] [py]`
        if (line_starts_with(p, "frame")) {
            p = skip_ws(p + 5);
            if (in_anim) {
                // This is an animation sub-frame (in anim block): <frame_name> <duration>
                char frame_name[SPRITE_MAX_NAME] = {};
                p = next_token(p, frame_name, SPRITE_MAX_NAME);
                char dur_str[16] = {};
                p = next_token(p, dur_str, 16);

                if (current_anim && frame_name[0] && dur_str[0]) {
                    // Find the frame index by name
                    uint16_t fi = 0;
                    while (fi < atlas.frame_count &&
                           std::strcmp(atlas.frame_names[fi], frame_name) != 0) {
                        ++fi;
                    }
                    if (fi < atlas.frame_count &&
                        current_anim->frame_count < SPRITE_MAX_ANIM_FRAMES) {
                        auto& af = current_anim->frames[current_anim->frame_count++];
                        af.frame_index = fi;
                        af.duration = static_cast<float>(std::atof(dur_str));
                    }
                }
            } else {
                // Top-level frame definition
                if (atlas.frame_count >= SPRITE_MAX_FRAMES) {
                    blob.free();
                    return make_unexpected(SpriteError::TooManyFrames);
                }
                auto& f = atlas.frames[atlas.frame_count];
                char name[SPRITE_MAX_NAME] = {};
                char xs[16], ys[16], ws[16], hs[16];
                char pxs[16] = "0.5", pys[16] = "0.5";

                p = next_token(p, name, SPRITE_MAX_NAME);
                p = next_token(p, xs, 16);
                p = next_token(p, ys, 16);
                p = next_token(p, ws, 16);
                p = next_token(p, hs, 16);

                // Optional pivot
                const char* after = skip_ws(p);
                if (*after && *after != '\n' && *after != '\r' && *after != '#') {
                    p = next_token(p, pxs, 16);
                    p = next_token(p, pys, 16);
                } else {
                    p = after;
                }

                if (name[0] && ws[0] && hs[0]) {
                    std::memcpy(atlas.frame_names[atlas.frame_count], name, SPRITE_MAX_NAME);
                    f.x = static_cast<uint16_t>(std::atoi(xs));
                    f.y = static_cast<uint16_t>(std::atoi(ys));
                    f.w = static_cast<uint16_t>(std::atoi(ws));
                    f.h = static_cast<uint16_t>(std::atoi(hs));
                    f.pivot_x = static_cast<float>(std::atof(pxs));
                    f.pivot_y = static_cast<float>(std::atof(pys));
                    ++atlas.frame_count;
                }
            }
            p = skip_line(p);
            continue;
        }

        // `anim <name> [loop]`
        if (line_starts_with(p, "anim")) {
            p = skip_ws(p + 4);
            if (atlas.anim_count >= SPRITE_MAX_ANIMS) {
                blob.free();
                return make_unexpected(SpriteError::TooManyAnims);
            }
            current_anim = &atlas.anims[atlas.anim_count];
            current_anim->frame_count = 0;
            current_anim->loop = false;

            char name[SPRITE_MAX_NAME] = {};
            p = next_token(p, name, SPRITE_MAX_NAME);
            std::memcpy(current_anim->name, name, SPRITE_MAX_NAME);

            // Check for 'loop' flag
            char loop_str[8] = {};
            const char* after = skip_ws(p);
            if (*after && *after != '\n' && *after != '\r') {
                p = next_token(p, loop_str, 8);
                if (std::strcmp(loop_str, "loop") == 0) {
                    current_anim->loop = true;
                }
            } else {
                p = after;
            }

            ++atlas.anim_count;
            in_anim = true;
            p = skip_line(p);
            continue;
        }

        // Unknown line — skip
        p = skip_line(p);
    }

    blob.free();

    // Load texture
    if (!has_texture || !texture_path[0]) {
        atlas.init();
        return make_unexpected(SpriteError::ParseError);
    }

    SamplerFilter min_filter = SamplerFilter::Nearest;
    SamplerFilter mag_filter = SamplerFilter::Nearest;
    if (std::strcmp(filter_str, "linear") == 0) {
        min_filter = SamplerFilter::Linear;
        mag_filter = SamplerFilter::Linear;
    } else if (filter_str[0] && std::strcmp(filter_str, "nearest") != 0) {
        atlas.init();
        return make_unexpected(SpriteError::BadFilter);
    }

    auto tex = texture_load_from_file(backend, vfs, texture_path, srgb);
    if (!tex) {
        atlas.init();
        return make_unexpected(SpriteError::TextureLoadFailed);
    }

    atlas.texture = tex->handle;
    atlas.tex_w   = tex->width;
    atlas.tex_h   = tex->height;

    // Fill in texture info for each frame
    for (uint16_t i = 0; i < atlas.frame_count; ++i) {
        atlas.frames[i].texture = atlas.texture;
        atlas.frames[i].tex_w   = atlas.tex_w;
        atlas.frames[i].tex_h   = atlas.tex_h;
    }

    return atlas;
}

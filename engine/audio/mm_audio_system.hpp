// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "../core/mm_vfs.hpp"
#include "core/mm_types.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// Forward declare miniaudio types (avoid full include in header)
struct ma_engine;
struct ma_sound;

static constexpr u8 MAX_SFX_VOICES   = 16;
static constexpr u8 MAX_SOUNDS       = 32;
static constexpr u8 SFX_PRIORITY_MAX = 255;
static constexpr u8 SFX_NO_SOUND     = UINT8_MAX;

struct SfxVoice {
    ma_sound *sound;    // miniaudio sound handle
    void     *buffer;   // per-voice ma_audio_buffer (independent cursor)
    u8   priority; // lower = more important
    u8   active;
    u8   buffer_inited;
    u8   pad;
};

struct SfxPool {
    ma_sound   *sources[MAX_SOUNDS];
    bool        source_loaded[MAX_SOUNDS];
    SfxVoice    voices[MAX_SFX_VOICES];
    u8     voice_count;

    ma_engine  *engine;

    // Sound registry: index → path mapping
    const char *sound_paths[MAX_SOUNDS];
    u8     sound_count;

    f32       master_volume = 1.0f;

    void        init(ma_engine *eng) noexcept;
    void        shutdown() noexcept;

    // Register a sound path, returns u8 ID (SFX_NO_SOUND on full)
    u8     register_sound(const char *path) noexcept;

    // Play by path string
    bool        play(const char *path, u8 priority = 128) noexcept;
    bool        play(const char *path, u8 priority, f32 pitch) noexcept;
    bool        play(const char *path, u8 priority, f32 pitch, f32 volume_scale) noexcept;

    // Play by registered ID
    bool        play_id(u8 id, u8 priority = 128) noexcept;
    bool        play_id(u8 id, u8 priority, f32 pitch) noexcept;
    bool        play_id(u8 id, u8 priority, f32 pitch, f32 volume_scale) noexcept;

    // Stop all sounds
    void        stop_all() noexcept;

    // Set master volume [0, 1]
    void        set_master_volume(f32 vol) noexcept;

  private:
    // Find lowest-priority active voice for replacement
    u8 find_lowest_priority() noexcept;
};

struct MusicLayer {
    ma_sound  *track[2];      // [0] = current, [1] = next
    void      *track_bufs[2]; // Persistent buffers (ma_audio_buffer*)
    void      *track_pcm[2];  // persistent decoded data
    f32      volume[2];     // crossfade volumes
    f32      crossfade_t;   // 0→1 during crossfade
    f32      crossfade_duration;
    u8    active_track; // 0 or 1
    u8    crossfading;
    bool       track_loaded[2];

    ma_engine *engine;

    void       init(ma_engine *eng) noexcept;
    void       shutdown() noexcept;

    void       play(const char *path, f32 fade_duration = 1.0f) noexcept;
    void       stop(f32 fade_duration = 0.5f) noexcept;
    void       update(f32 dt) noexcept;
    void       set_volume(f32 vol) noexcept;
    bool       is_playing() const noexcept;
};

// AudioSystem — top-level wrapper that owns ma_engine + SfxPool + MusicLayer
struct AudioSystem {
    SfxPool    sfx;
    MusicLayer music;

    bool       init() noexcept;
    void       shutdown() noexcept;
    void       update(f32 dt) noexcept;

  private:
    ma_engine *engine = nullptr;
};

extern AudioSystem g_audio_system;

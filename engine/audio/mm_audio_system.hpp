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

    // ─── Suspended (app in the background) ────────────────────────────
    // A backgrounded app that keeps making noise is a bug the player hears
    // with the phone in a pocket. pause() cuts what is playing right now;
    // while suspended every play() is refused rather than queued, so a sound
    // queued behind the pause does not arrive seconds late on resume.
    void pause() noexcept;
    void resume() noexcept;
    bool is_paused() const noexcept { return paused; }

    // Set master volume [0, 1]
    void        set_master_volume(f32 vol) noexcept;

  private:
    // Find lowest-priority active voice for replacement
    u8 find_lowest_priority() noexcept;

    bool paused = false;
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
    bool       paused = false;

    ma_engine *engine;

    void       init(ma_engine *eng) noexcept;
    void       shutdown() noexcept;

    void       play(const char *path, f32 fade_duration = 1.0f) noexcept;
    void       stop(f32 fade_duration = 0.5f) noexcept;
    void       update(f32 dt) noexcept;
    void       set_volume(f32 vol) noexcept;

    // resume() restarts the track pause() stopped. It does not need the path:
    // pause() only stops the sound, so the decoded buffer is still resident and
    // the restart is ma_sound_start rather than a decode. What it will NOT do is
    // restart a track that had been stopped on purpose — stop() leaves
    // active_track on the empty slot, and resume() reads that as "nothing to
    // play". An app that wants music after a resume still has to call play();
    // what it no longer has to do is un-stick a layer that was interrupted.
    void       pause() noexcept;
    void       resume() noexcept;
    bool       is_paused() const noexcept { return paused; }

    bool       is_playing() const noexcept;
};

// AudioSystem — top-level wrapper that owns ma_engine + SfxPool + MusicLayer
struct AudioSystem {
    SfxPool    sfx;
    MusicLayer music;

    // Idempotent: a second call while already initialised returns true and
    // allocates nothing (a second ma_engine_init would leak the first engine).
    bool       init() noexcept;
    void       shutdown() noexcept;
    void       update(f32 dt) noexcept;

    bool       is_initialized() const noexcept { return engine != nullptr; }

    // Suspends both layers. The host calls this on the platform's background
    // event; nothing below has to know whether a pause is a lifecycle pause or
    // a settings mute.
    void       pause() noexcept;
    void       resume() noexcept;
    bool       is_paused() const noexcept { return sfx.is_paused(); }

  private:
    ma_engine *engine = nullptr;
};

extern AudioSystem g_audio_system;

// ── ENGINE_AUDIO=OFF ─────────────────────────────────────────────
// Compiled against mm_audio_system_stub.cpp instead of the real backend, so the
// 1.1 MB miniaudio translation unit is not linked in. The API above is
// unchanged and every call becomes a no-op that returns false / 0.
//
// Callers compile either way, which is deliberate: a game that plays sound must
// be able to say so in one CMake line, not in every call site. The cost is that
// forgetting the flag is invisible until someone listens for a sound that is not
// there — so the flag belongs next to the other app's engine features in the
// ROOT CMakeLists, and a game that registers sfx should say why it is off.

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// Audio System — miniaudio wrapper, SFX pool + music crossfade
// Cache reason: pre-allocated voice pool, no heap alloc in play()
// Design:
//   - SFX pool: play-and-forget, priority-based replacement
//   - Music: 2 track crossfade (current + next)
//   - No std::function, no virtual, no heap in hot path
//   - miniaudio header-only, minimal overhead

// Forward declare miniaudio types (avoid full include in header)
struct ma_engine;
struct ma_sound;

static constexpr uint8_t MAX_SFX_VOICES = 16;
static constexpr uint8_t MAX_SOUNDS    = 32;
static constexpr uint8_t SFX_PRIORITY_MAX = 255;
static constexpr uint8_t SFX_NO_SOUND  = UINT8_MAX;

struct SfxVoice {
    ma_sound* sound;       // miniaudio sound handle
    uint8_t   priority;    // lower = more important
    uint8_t   active;
    uint16_t  _pad;
};

struct SfxPool {
    ma_sound  *sources[MAX_SOUNDS];
    bool      source_loaded[MAX_SOUNDS];
    SfxVoice voices[MAX_SFX_VOICES];
    uint8_t  voice_count;

    ma_engine* engine;

    // Sound registry: index → path mapping
    const char* sound_paths[MAX_SOUNDS];
    uint8_t     sound_count;

    void init(ma_engine* eng) noexcept;
    void shutdown() noexcept;

    // Register a sound path, returns uint8_t ID (SFX_NO_SOUND on full)
    uint8_t register_sound(const char* path) noexcept;

    // Play by path string
    bool play(const char* path, uint8_t priority = 128) noexcept;
    bool play(const char* path, uint8_t priority, float pitch) noexcept;

    // Play by registered ID
    bool play_id(uint8_t id, uint8_t priority = 128) noexcept;
    bool play_id(uint8_t id, uint8_t priority, float pitch) noexcept;

    // Stop all sounds
    void stop_all() noexcept;

    // Set master volume [0, 1]
    void set_master_volume(float vol) noexcept;

private:
    // Find lowest-priority active voice for replacement
    uint8_t find_lowest_priority() noexcept;
};

struct MusicLayer {
    ma_sound* track[2];      // [0] = current, [1] = next
    float     volume[2];     // crossfade volumes
    float     crossfade_t;   // 0→1 during crossfade
    float     crossfade_duration;
    uint8_t   active_track;  // 0 or 1
    uint8_t   crossfading;

    ma_engine* engine;

    void init(ma_engine* eng) noexcept;
    void shutdown() noexcept;

    void play(const char* path, float fade_duration = 1.0f) noexcept;
    void stop(float fade_duration = 0.5f) noexcept;
    void update(float dt) noexcept;
    void set_volume(float vol) noexcept;
    bool is_playing() const noexcept;
};

// AudioSystem — top-level wrapper that owns ma_engine + SfxPool + MusicLayer
struct AudioSystem {
    SfxPool     sfx;
    MusicLayer  music;

    bool init() noexcept;
    void shutdown() noexcept;
    void update(float dt) noexcept;

private:
    ma_engine* engine_ = nullptr;
};

extern AudioSystem g_audio_system;

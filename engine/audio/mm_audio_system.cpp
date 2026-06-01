#include "mm_audio_system.hpp"
#include "../core/mm_arena.hpp"
#include "../thirdparty/miniaudio/miniaudio.h"
#include <cmath>

void SfxPool::init(ma_engine* eng) noexcept {
    engine = eng;
    voice_count = MAX_SFX_VOICES;
    memset(voices, 0, sizeof(voices));
    for (uint8_t i = 0; i < voice_count; ++i) {
        voices[i].priority = SFX_PRIORITY_MAX;
        voices[i].active  = 0;
        voices[i].sound   = static_cast<ma_sound*>(
            ma_malloc(sizeof(ma_sound), nullptr));
    }
    sound_count = 0;
}

uint8_t SfxPool::register_sound(const char* path) noexcept {
    if (sound_count >= MAX_SOUNDS) return SFX_NO_SOUND;
    uint8_t id = sound_count;
    sound_paths[id] = path;
    ++sound_count;
    return id;
}

bool SfxPool::play_id(uint8_t id, uint8_t priority) noexcept {
    return play_id(id, priority, 1.0f);
}

bool SfxPool::play_id(uint8_t id, uint8_t priority, float pitch) noexcept {
    if (id >= sound_count) return false;
    return play(sound_paths[id], priority, pitch);
}

void SfxPool::shutdown() noexcept {
    for (uint8_t i = 0; i < voice_count; ++i) {
        if (voices[i].sound) {
            ma_sound_stop(voices[i].sound);
            ma_sound_uninit(voices[i].sound);
            ma_free(voices[i].sound, nullptr);
            voices[i].sound = nullptr;
        }
    }
}

bool SfxPool::play(const char* path, uint8_t priority) noexcept {
    return play(path, priority, 1.0f);
}

bool SfxPool::play(const char* path, uint8_t priority, float pitch) noexcept {
    uint8_t idx = MAX_SFX_VOICES;
    for (uint8_t i = 0; i < voice_count; ++i) {
        if (voices[i].active && voices[i].sound &&
            !ma_sound_is_playing(voices[i].sound)) {
            ma_sound_uninit(voices[i].sound);
            voices[i].active = 0;
            voices[i].priority = SFX_PRIORITY_MAX;
        }
        if (!voices[i].active) { idx = i; break; }
    }

    if (idx == MAX_SFX_VOICES) {
        idx = find_lowest_priority();
        if (voices[idx].priority > priority) return false;
        if (voices[idx].sound) {
            ma_sound_stop(voices[idx].sound);
            ma_sound_uninit(voices[idx].sound);
        }
    }

    ma_sound* sound = voices[idx].sound;
    if (!sound) return false;

    if (ma_sound_init_from_file(engine, path, 0, nullptr, nullptr, sound) != MA_SUCCESS) {
        return false;
    }

    voices[idx].priority = priority;
    voices[idx].active   = 1;
    ma_sound_set_pitch(sound, pitch);
    ma_sound_start(sound);
    return true;
}

void SfxPool::stop_all() noexcept {
    for (uint8_t i = 0; i < voice_count; ++i) {
        if (voices[i].active && voices[i].sound) {
            ma_sound_stop(voices[i].sound);
        }
    }
}

void SfxPool::set_master_volume(float vol) noexcept {
    for (uint8_t i = 0; i < voice_count; ++i) {
        if (voices[i].active && voices[i].sound) {
            ma_sound_set_volume(voices[i].sound, vol);
        }
    }
}

uint8_t SfxPool::find_lowest_priority() noexcept {
    uint8_t lowest = 0;
    uint8_t lowest_pri = voices[0].priority;
    for (uint8_t i = 1; i < voice_count; ++i) {
        // Among inactive, prefer replacing lower priority (higher value)
        if (voices[i].priority > lowest_pri) {
            lowest_pri = voices[i].priority;
            lowest = i;
        }
    }
    return lowest;
}

// MusicLayer

void MusicLayer::init(ma_engine* eng) noexcept {
    engine = eng;
    track[0] = track[1] = nullptr;
    volume[0] = volume[1] = 0.0f;
    crossfade_t = 0.0f;
    crossfade_duration = 1.0f;
    active_track = 0;
    crossfading = 0;
}

void MusicLayer::shutdown() noexcept {
    for (int i = 0; i < 2; ++i) {
        if (track[i]) {
            ma_sound_stop(track[i]);
            ma_sound_uninit(track[i]);
            ma_free(track[i], nullptr);
            track[i] = nullptr;
        }
    }
}

void MusicLayer::play(const char* path, float fade_duration) noexcept {
    uint8_t next = 1 - active_track;

    // Stop previous next-track
    if (track[next]) {
        ma_sound_stop(track[next]);
        ma_sound_uninit(track[next]);
        ma_free(track[next], nullptr);
        track[next] = nullptr;
    }

    track[next] = static_cast<ma_sound*>(
        ma_malloc(sizeof(ma_sound), nullptr));
    if (!track[next]) return;
    memset(track[next], 0, sizeof(ma_sound));

    if (ma_sound_init_from_file(engine, path, 0, nullptr, nullptr, track[next]) != MA_SUCCESS) {
        ma_free(track[next], nullptr);
        track[next] = nullptr;
        return;
    }

    ma_sound_set_looping(track[next], true);
    ma_sound_set_volume(track[next], 0.0f);
    ma_sound_start(track[next]);

    // Start crossfade
    crossfade_duration = fade_duration;
    crossfade_t = 0.0f;
    crossfading = 1;
}

void MusicLayer::stop(float fade_duration) noexcept {
    crossfade_duration = fade_duration;
    crossfade_t = 0.0f;
    crossfading = 1;
    // volume will fade to 0 then stop
}

void MusicLayer::update(float dt) noexcept {
    if (!crossfading) return;

    crossfade_t += dt;
    float t = crossfade_t / crossfade_duration;

    if (t >= 1.0f) {
        // Crossfade complete
        uint8_t next = 1 - active_track;
        if (track[next]) {
            volume[next] = 1.0f;
            ma_sound_set_volume(track[next], volume[next]);
        }
        // Fade out old track
        if (track[active_track]) {
            ma_sound_stop(track[active_track]);
            ma_sound_set_volume(track[active_track], 0.0f);
        }
        active_track = next;
        crossfading = 0;
        return;
    }

    uint8_t next = 1 - active_track;
    volume[active_track] = 1.0f - t;
    volume[next] = t;

    if (track[active_track]) {
        ma_sound_set_volume(track[active_track], volume[active_track]);
    }
    if (track[next]) {
        ma_sound_set_volume(track[next], volume[next]);
    }
}

void MusicLayer::set_volume(float vol) noexcept {
    for (int i = 0; i < 2; ++i) {
        if (track[i]) ma_sound_set_volume(track[i], vol);
    }
}

bool MusicLayer::is_playing() const noexcept {
    return (track[active_track] && ma_sound_is_playing(track[active_track]));
}

// ─── AudioSystem ─────────────────────────────────────────────────
AudioSystem g_audio_system;

bool AudioSystem::init() noexcept {
    engine_ = static_cast<ma_engine*>(ma_malloc(sizeof(ma_engine), nullptr));
    if (!engine_) return false;
    memset(engine_, 0, sizeof(ma_engine));

    if (ma_engine_init(nullptr, engine_) != MA_SUCCESS) {
        ma_free(engine_, nullptr);
        engine_ = nullptr;
        return false;
    }

    sfx.init(engine_);
    music.init(engine_);
    return true;
}

void AudioSystem::shutdown() noexcept {
    sfx.shutdown();
    music.shutdown();
    if (engine_) {
        ma_engine_uninit(engine_);
        ma_free(engine_, nullptr);
        engine_ = nullptr;
    }
}

void AudioSystem::update(float dt) noexcept {
    music.update(dt);
}

// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_audio_system.hpp"
#include "../thirdparty/miniaudio/miniaudio.h"
#include <cstring>
#include "../core/mm_log.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Design: bypass ma_engine resource manager entirely for SFX.
// ─────────────────────────────────────────────────────────────────────────────

struct SfxEntry {
    void*           pcm;        // heap-allocated decoded samples
    ma_audio_buffer buf;        // wraps pcm, owned by this entry
    ma_format       fmt;
    uint32_t        channels;
    uint32_t        sample_rate;
    ma_uint64       frame_count;
    bool            loaded;
};

static SfxEntry s_entries[MAX_SOUNDS];

// ─── helpers ─────────────────────────────────────────────────────────────────

static bool decode_file(ma_engine*   engine,
                        const char*  path,
                        void**       out_pcm,
                        ma_format*   out_fmt,
                        uint32_t*    out_channels,
                        uint32_t*    out_sample_rate,
                        ma_uint64*   out_frames) noexcept
{
    ma_decoder_config cfg = ma_decoder_config_init_default();
    cfg.format      = ma_format_f32;
    cfg.channels    = ma_engine_get_channels(engine);
    cfg.sampleRate  = ma_engine_get_sample_rate(engine);

    ma_decoder dec;
    if (ma_decoder_init_file(path, &cfg, &dec) != MA_SUCCESS) return false;

    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &frames);

    if (frames == 0) {
        const ma_uint64 CHUNK = 4096;
        ma_uint64       cap   = CHUNK;
        uint32_t       bpf   = ma_get_bytes_per_frame(cfg.format, cfg.channels);
        void*          buf   = ma_malloc(cap * bpf, nullptr);
        if (!buf) { ma_decoder_uninit(&dec); return false; }
        ma_uint64 total = 0;
        for (;;) {
            if (total + CHUNK > cap) {
                cap *= 2;
                void* nb = ma_realloc(buf, cap * bpf, nullptr);
                if (!nb) { ma_free(buf, nullptr); ma_decoder_uninit(&dec); return false; }
                buf = nb;
            }
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(&dec, static_cast<uint8_t*>(buf) + total * bpf, CHUNK, &read);
            total += read;
            if (read < CHUNK) break;
        }
        frames = total;
        *out_pcm         = buf;
        *out_fmt         = cfg.format;
        *out_channels    = cfg.channels;
        *out_sample_rate = cfg.sampleRate;
        *out_frames      = frames;
        ma_decoder_uninit(&dec);
        return frames > 0;
    }

    uint32_t bpf  = ma_get_bytes_per_frame(cfg.format, cfg.channels);
    void*    buf  = ma_malloc(frames * bpf, nullptr);
    if (!buf) { ma_decoder_uninit(&dec); return false; }

    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&dec, buf, frames, &read);
    ma_decoder_uninit(&dec);

    if (read == 0) { ma_free(buf, nullptr); return false; }

    *out_pcm         = buf;
    *out_fmt         = cfg.format;
    *out_channels    = cfg.channels;
    *out_sample_rate = cfg.sampleRate;
    *out_frames      = read;
    return true;
}

// ─── SfxPool ─────────────────────────────────────────────────────────────────

void SfxPool::init(ma_engine* eng) noexcept {
    engine      = eng;
    voice_count = MAX_SFX_VOICES;
    sound_count = 0;

    memset(sound_paths,   0, sizeof(sound_paths));
    memset(source_loaded, 0, sizeof(source_loaded));
    memset(sources,       0, sizeof(sources));
    memset(s_entries,     0, sizeof(s_entries));

    for (uint8_t i = 0; i < voice_count; ++i) {
        voices[i].sound    = static_cast<ma_sound*>(ma_malloc(sizeof(ma_sound), nullptr));
        voices[i].priority = SFX_PRIORITY_MAX;
        voices[i].active   = 0;
        if (voices[i].sound) memset(voices[i].sound, 0, sizeof(ma_sound));
    }
}

uint8_t SfxPool::register_sound(const char* path) noexcept {
    if (sound_count >= MAX_SOUNDS) return SFX_NO_SOUND;
    
    uint8_t    id  = sound_count;
    SfxEntry&  e   = s_entries[id];

    e.loaded       = false;
    e.pcm          = nullptr;
    sound_paths[id] = path;
    source_loaded[id] = false;

    if (!decode_file(engine, path,
                     &e.pcm, &e.fmt, &e.channels, &e.sample_rate, &e.frame_count)) {
        return SFX_NO_SOUND;
    }

    ma_audio_buffer_config bcfg = ma_audio_buffer_config_init(
        e.fmt, e.channels, e.frame_count, e.pcm, nullptr);
    bcfg.sampleRate = e.sample_rate;

    if (ma_audio_buffer_init(&bcfg, &e.buf) != MA_SUCCESS) {
        ma_free(e.pcm, nullptr);
        e.pcm = nullptr;
        return SFX_NO_SOUND;
    }

    e.loaded          = true;
    source_loaded[id] = true;
    ++sound_count;
    return id;
}

static bool play_entry(SfxPool& pool, uint8_t id, uint8_t priority, float pitch) noexcept {
    SfxEntry& e = s_entries[id];
    if (!e.loaded) return false;

    uint8_t idx = MAX_SFX_VOICES;
    for (uint8_t i = 0; i < pool.voice_count; ++i) {
        SfxVoice& v = pool.voices[i];
        if (v.active && v.sound && !ma_sound_is_playing(v.sound)) {
            ma_sound_uninit(v.sound);
            v.active   = 0;
            v.priority = SFX_PRIORITY_MAX;
        }
        if (!v.active && idx == MAX_SFX_VOICES) idx = i;
    }

    if (idx == MAX_SFX_VOICES) {
        uint8_t worst     = 0;
        uint8_t worst_pri = pool.voices[0].priority;
        for (uint8_t i = 1; i < pool.voice_count; ++i) {
            if (pool.voices[i].priority > worst_pri) {
                worst_pri = pool.voices[i].priority;
                worst     = i;
            }
        }
        if (pool.voices[worst].priority <= priority) return false;
        SfxVoice& v = pool.voices[worst];
        ma_sound_stop(v.sound);
        ma_sound_uninit(v.sound);
        v.active   = 0;
        v.priority = SFX_PRIORITY_MAX;
        idx        = worst;
    }

    SfxVoice& v = pool.voices[idx];
    if (!v.sound) return false;

    ma_audio_buffer_seek_to_pcm_frame(&e.buf, 0);

    ma_sound_config scfg  = ma_sound_config_init();
    scfg.pDataSource      = &e.buf;
    scfg.flags            = MA_SOUND_FLAG_NO_SPATIALIZATION;

    if (ma_sound_init_ex(pool.engine, &scfg, v.sound) != MA_SUCCESS) return false;

    v.priority = priority;
    v.active   = 1;
    ma_sound_set_pitch(v.sound, pitch);
    ma_sound_start(v.sound);
    return true;
}

bool SfxPool::play_id(uint8_t id, uint8_t priority) noexcept {
    return play_id(id, priority, 1.0f);
}
bool SfxPool::play_id(uint8_t id, uint8_t priority, float pitch) noexcept {
    if (id >= sound_count || !source_loaded[id]) return false;
    return play_entry(*this, id, priority, pitch);
}

bool SfxPool::play(const char* path, uint8_t priority) noexcept {
    return play(path, priority, 1.0f);
}
bool SfxPool::play(const char* path, uint8_t priority, float pitch) noexcept {
    for (uint8_t i = 0; i < sound_count; ++i) {
        if (sound_paths[i] && strcmp(sound_paths[i], path) == 0)
            return play_entry(*this, i, priority, pitch);
    }
    return false;
}

void SfxPool::shutdown() noexcept {
    for (uint8_t i = 0; i < voice_count; ++i) {
        if (voices[i].sound) {
            if (voices[i].active) {
                ma_sound_stop(voices[i].sound);
                ma_sound_uninit(voices[i].sound);
            }
            ma_free(voices[i].sound, nullptr);
            voices[i].sound  = nullptr;
            voices[i].active = 0;
        }
    }
    for (uint8_t i = 0; i < sound_count; ++i) {
        SfxEntry& e = s_entries[i];
        if (e.loaded) {
            ma_audio_buffer_uninit(&e.buf);
            ma_free(e.pcm, nullptr);
            e.pcm    = nullptr;
            e.loaded = false;
        }
        if (sources[i]) {
            ma_free(sources[i], nullptr);
            sources[i] = nullptr;
        }
        source_loaded[i] = false;
    }
    sound_count = 0;
}

void SfxPool::stop_all() noexcept {
    for (uint8_t i = 0; i < voice_count; ++i)
        if (voices[i].active && voices[i].sound)
            ma_sound_stop(voices[i].sound);
}

void SfxPool::set_master_volume(float vol) noexcept {
    for (uint8_t i = 0; i < voice_count; ++i)
        if (voices[i].active && voices[i].sound)
            ma_sound_set_volume(voices[i].sound, vol);
}

uint8_t SfxPool::find_lowest_priority() noexcept {
    uint8_t lowest = 0, lowest_pri = voices[0].priority;
    for (uint8_t i = 1; i < voice_count; ++i)
        if (voices[i].priority > lowest_pri) { lowest_pri = voices[i].priority; lowest = i; }
    return lowest;
}

void MusicLayer::init(ma_engine* eng) noexcept {
    engine             = eng;
    track[0]           = track[1] = nullptr;
    volume[0]          = volume[1] = 0.0f;
    crossfade_t        = 0.0f;
    crossfade_duration = 1.0f;
    active_track       = 0;
    crossfading        = 0;
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
    if (track[next]) {
        ma_sound_stop(track[next]);
        ma_sound_uninit(track[next]);
        ma_free(track[next], nullptr);
        track[next] = nullptr;
    }
    track[next] = static_cast<ma_sound*>(ma_malloc(sizeof(ma_sound), nullptr));
    if (!track[next]) return;
    memset(track[next], 0, sizeof(ma_sound));
    if (ma_sound_init_from_file(engine, path, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                nullptr, nullptr, track[next]) != MA_SUCCESS) {
        ma_free(track[next], nullptr);
        track[next] = nullptr;
        return;
    }
    ma_sound_set_looping(track[next], MA_TRUE);
    ma_sound_set_volume(track[next], 0.0f);
    ma_sound_start(track[next]);
    crossfade_duration = fade_duration;
    crossfade_t        = 0.0f;
    crossfading        = 1;
}

void MusicLayer::stop(float fade_duration) noexcept {
    crossfade_duration = fade_duration;
    crossfade_t        = 0.0f;
    crossfading        = 1;
}

void MusicLayer::update(float dt) noexcept {
    if (!crossfading) return;
    crossfade_t += dt;
    float t = crossfade_t / crossfade_duration;
    if (t >= 1.0f) {
        uint8_t next = 1 - active_track;
        if (track[next])         { volume[next] = 1.0f; ma_sound_set_volume(track[next], 1.0f); }
        if (track[active_track]) { ma_sound_stop(track[active_track]); ma_sound_set_volume(track[active_track], 0.0f); }
        active_track = next;
        crossfading  = 0;
        return;
    }
    uint8_t next         = 1 - active_track;
    volume[active_track] = 1.0f - t;
    volume[next]         = t;
    if (track[active_track]) ma_sound_set_volume(track[active_track], volume[active_track]);
    if (track[next])         ma_sound_set_volume(track[next],         volume[next]);
}

void MusicLayer::set_volume(float vol) noexcept {
    for (int i = 0; i < 2; ++i)
        if (track[i]) ma_sound_set_volume(track[i], vol);
}

bool MusicLayer::is_playing() const noexcept {
    return track[active_track] && ma_sound_is_playing(track[active_track]);
}

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

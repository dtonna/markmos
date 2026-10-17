// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT
//
// Audio stub — compiled INSTEAD of mm_audio_system.cpp when ENGINE_AUDIO=OFF.
//
// miniaudio is a 1.1 MB translation unit that is linked into every binary the
// moment anything references AudioSystem, because the platform hosts call
// g_audio_system.init() unconditionally. This file keeps that reference valid
// without the backend, so an app that never plays a sound does not carry it.
//
// The API is byte-for-byte the one in mm_audio_system.hpp: a caller compiles
// unchanged and finds out at runtime (silently) rather than at build time. That
// silence is the point — see the note in the header.
//
// build/: `option(ENGINE_AUDIO … ON)` decides which of the two is compiled.

#include "mm_audio_system.hpp"

// ─── SfxPool ─────────────────────────────────────────────────────
void     SfxPool::init(ma_engine *eng) noexcept { (void)eng; }
void     SfxPool::shutdown() noexcept {}
u8       SfxPool::register_sound(const char *path) noexcept { (void)path; return 0; }
bool     SfxPool::play(const char *path, u8 priority) noexcept { (void)path; (void)priority; return false; }
bool     SfxPool::play(const char *path, u8 priority, f32 pitch) noexcept {
    (void)path; (void)priority; (void)pitch; return false;
}
bool     SfxPool::play(const char *path, u8 priority, f32 pitch, f32 volume_scale) noexcept {
    (void)path; (void)priority; (void)pitch; (void)volume_scale; return false;
}
bool     SfxPool::play_id(u8 id, u8 priority) noexcept { (void)id; (void)priority; return false; }
bool     SfxPool::play_id(u8 id, u8 priority, f32 pitch) noexcept {
    (void)id; (void)priority; (void)pitch; return false;
}
bool     SfxPool::play_id(u8 id, u8 priority, f32 pitch, f32 volume_scale) noexcept {
    (void)id; (void)priority; (void)pitch; (void)volume_scale; return false;
}
void     SfxPool::stop_all() noexcept {}
void     SfxPool::pause() noexcept {}
void     SfxPool::resume() noexcept {}
void     SfxPool::set_master_volume(f32 vol) noexcept { (void)vol; }
u8       SfxPool::find_lowest_priority() noexcept { return 0; }

// ─── MusicLayer ──────────────────────────────────────────────────
void     MusicLayer::init(ma_engine *eng) noexcept { (void)eng; }
void     MusicLayer::shutdown() noexcept {}
void     MusicLayer::play(const char *path, f32 fade_duration) noexcept { (void)path; (void)fade_duration; }
void     MusicLayer::stop(f32 fade_duration) noexcept { (void)fade_duration; }
void     MusicLayer::pause() noexcept {}
void     MusicLayer::resume() noexcept {}
void     MusicLayer::update(f32 dt) noexcept { (void)dt; }
void     MusicLayer::set_volume(f32 vol) noexcept { (void)vol; }
bool     MusicLayer::is_playing() const noexcept { return false; }

// ─── AudioSystem ─────────────────────────────────────────────────
bool     AudioSystem::init() noexcept { return false; }
void     AudioSystem::shutdown() noexcept {}
void     AudioSystem::update(f32 dt) noexcept { (void)dt; }
void     AudioSystem::pause() noexcept {}
void     AudioSystem::resume() noexcept {}

AudioSystem g_audio_system;
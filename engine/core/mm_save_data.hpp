// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include "core/mm_types.h"
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <bit>

// SaveData — binary flat struct, CRC32 verified
// Cache reason: single cache line read/write, no heap, no serialization overhead
// Design: flat POD, write via Vfs->write_doc_atomic (temp + atomic rename)

class Vfs;

static constexpr u32 SAVE_MAGIC   = 0xCAFE2D01u;
static constexpr u32 MAX_LEVELS   = 200;
static constexpr u32 MAX_STARS    = 3u;  // 0-3 stars per level (2 bits)

struct SaveData {
    u32 magic;                      // version check
    u32 level_stars[(MAX_LEVELS + 15) / 16];  // 2 bits per level, packed
    u32 high_scores[MAX_LEVELS];
    u32 settings_flags;             // bit 0: sound on, bit 1: haptic on, etc.
    u32 checksum;                   // CRC32 at end

    void init() noexcept {
        magic = SAVE_MAGIC;
        memset(this, 0, sizeof(*this));
        magic = SAVE_MAGIC;
        settings_flags = 3; // sound + particles ON by default
    }

    bool valid() const noexcept {
        if (magic != SAVE_MAGIC) return false;
        u32 stored_crc = checksum;
        const_cast<SaveData*>(this)->checksum = 0;
        bool ok = (stored_crc == crc32());
        const_cast<SaveData*>(this)->checksum = stored_crc;
        return ok;
    }

    void finalize() noexcept {
        checksum = 0;
        checksum = crc32();
    }

    u8 get_stars(u32 level) const noexcept {
        u32 idx = level / 16;
        u32 bit = (level % 16) * 2;
        return static_cast<u8>((level_stars[idx] >> bit) & 3);
    }

    void set_stars(u32 level, u8 stars) noexcept {
        u32 idx = level / 16;
        u32 bit = (level % 16) * 2;
        level_stars[idx] = (level_stars[idx] & ~(3u << bit)) | ((stars & 3u) << bit);
    }

    u32 get_score(u32 level) const noexcept {
        return high_scores[level];
    }

    void set_score(u32 level, u32 score) noexcept {
        if (score > high_scores[level]) {
            high_scores[level] = score;
        }
    }

    bool sound_enabled()  const noexcept { return (settings_flags & 1) != 0; }
    bool haptic_enabled() const noexcept { return (settings_flags & 2) != 0; }
    void set_sound(bool on)  noexcept { settings_flags = (settings_flags & ~1u) | (on ? 1u : 0u); }
    void set_haptic(bool on) noexcept { settings_flags = (settings_flags & ~2u) | (on ? 2u : 0u); }

private:
    // CRC32-C (Castagnoli) — hardware accelerated on ARM/x86
    static u32 crc32c(u32 crc, const u8* data, size_t len) noexcept {
#if defined(__ARM_FEATURE_CRC32)
        for (size_t i = 0; i < len; ++i) {
            crc = __builtin_arm_crc32b(crc, data[i]);
        }
#elif defined(__x86_64__) || defined(_M_X64)
        // SSE 4.2 CRC32 — use intrinsics if available
        for (size_t i = 0; i < len; ++i) {
            crc = __builtin_ia32_crc32qi(crc, data[i]);
        }
#else
        // Software fallback — simple table-less CRC32
        static constexpr u32 POLY = 0x82F63B78u;
        for (size_t i = 0; i < len; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ (POLY & ~((crc & 1) - 1));
            }
        }
#endif
        return crc;
    }

    u32 crc32() const noexcept {
        // CRC over everything except checksum field
        constexpr size_t crc_offset = offsetof(SaveData, checksum);
        return crc32c(0xFFFFFFFF, reinterpret_cast<const u8*>(this), crc_offset) ^ 0xFFFFFFFF;
    }
};

static_assert(sizeof(SaveData) <= 1024, "SaveData should fit in 1KB");
static_assert(std::is_trivially_copyable<SaveData>::value, "SaveData must be POD");

// Global instance — init() at app start, load() on first access
extern SaveData g_save_data;

// Load from doc directory — returns true if file existed and CRC valid.
// On failure (missing/corrupt), g_save_data is init() to defaults.
bool save_data_load(Vfs& vfs, const char* path = "save.bin") noexcept;

// Save to doc directory — write via write_doc_atomic (temp + atomic rename)
bool save_data_save(Vfs& vfs, const SaveData& data = g_save_data,
                    const char* path = "save.bin") noexcept;

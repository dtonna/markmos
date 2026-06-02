// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include "mm_cache_metrics.hpp"

// ─── VfsBlob ─────────────────────────────────────────────────────
// Allocated by Vfs (via pool_alloc for sizes <= 4KB, rpmalloc for larger)
// Freed via VfsBlob::free()
struct VfsBlob {
    void*    data = nullptr;
    uint64_t size = 0;

    void free() noexcept;
    bool valid() const noexcept { return data != nullptr && size > 0; }
};

// ─── Vfs — Platform File Abstraction ─────────────────────────────
class Vfs {
public:
    // bundle_path: read-only resource directory (e.g. .app bundle)
    // doc_path:    read-write user document directory
    void init(const char* bundle_path, const char* doc_path) noexcept;

    VfsBlob read_bundle(const char* path) noexcept;
    VfsBlob read_doc(const char* path) noexcept;
    VfsBlob read_absolute(const char* path) noexcept;

    bool write_doc(const char* path, const void* data, uint64_t size) noexcept;
    bool write_doc_atomic(const char* path, const void* data, uint64_t size) noexcept;
    bool exists(const char* path) noexcept;
    const char* get_bundle_path() noexcept {
        return bundle_path_;
    }

#if defined(TARGET_ANDROID)
    static void set_asset_manager(void* mgr) noexcept;
#endif

private:
    char bundle_path_[512]{};
    char doc_path_[512]{};
    VfsBlob read_file(const char* full_path) noexcept;
};

extern Vfs g_vfs;

// ─── Async LoadRequest ───────────────────────────────────────────
struct LoadRequest {
    const char* path;                           // caller keeps alive
    void (*callback)(VfsBlob blob, void* user) noexcept;
    void*       user;
};

// ─── AssetLoader — Async Dispatch + Completion ───────────────────
class AssetLoader {
public:
    void init(Vfs* vfs) noexcept;
    void submit(const LoadRequest& req) noexcept;
    void process_completed() noexcept;  // call once per frame on main thread

private:
    static constexpr uint32_t kMaxPending = 256;

    struct LoadResult {
        VfsBlob  blob;
        void   (*callback)(VfsBlob, void*) noexcept;
        void*    user;
    };

    Vfs*      vfs_ = nullptr;
    LoadResult  results_[kMaxPending];
    uint32_t    head_   = 0;
    uint32_t    tail_   = 0;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

    bool push_result(const LoadResult& r) noexcept;
    bool pop_result(LoadResult& r) noexcept;

    static void load_job(void* data) noexcept;
};

extern AssetLoader g_asset_loader;

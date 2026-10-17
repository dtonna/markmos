// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include "mm_cache_metrics.hpp"
#include "core/mm_types.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

// ─── VfsBlob ─────────────────────────────────────────────────────
// Allocated by Vfs (via pool_alloc for sizes <= 4KB, rpmalloc for larger)
// Freed via VfsBlob::free()
struct VfsBlob {
    void    *data = nullptr;
    u64 size = 0;

    void     free() noexcept;
    bool     valid() const noexcept { return data != nullptr && size > 0; }
};

// ─── Vfs — Platform File Abstraction ─────────────────────────────
class Vfs {
  public:
    // bundle_path: read-only resource directory (e.g. .app bundle)
    // doc_path:    read-write user document directory
    void        init(const char *bundle_path, const char *doc_path) noexcept;

    VfsBlob     read_bundle(const char *path) noexcept;
    VfsBlob     read_doc(const char *path) noexcept;
    VfsBlob     read_absolute(const char *path) noexcept;

    bool        write_doc(const char *path, const void *data, u64 size) noexcept;
    bool        write_doc_atomic(const char *path, const void *data, u64 size) noexcept;
    bool        exists(const char *path) noexcept;
    // Delete a file in the DOCUMENTS directory, by the same relative name
    // write_doc/read_doc take ("fc_slot_0.bin", not a full path).
    //
    // Returns false when the file was not there — deleting something absent is the
    // state the caller already wanted, so "erase my data" should treat it as
    // success rather than as a failure to report.
    //
    // NOTE on exists() above: it stats the path AS GIVEN, with no doc_path_ prefix,
    // while read_doc/write_doc/delete_doc all join doc_path_ first. Nothing in the
    // repo calls exists() today, so the inconsistency is dormant; a caller who finds
    // it surprising is the third name for one directory, which is how the solved
    // browser's 62px scroll bug happened. Treat exists() as the odd one out.
    bool        delete_doc(const char *path) noexcept;
    const char *get_bundle_path() noexcept { return bundle_path_; }

#if defined(TARGET_ANDROID)
    static void set_asset_manager(void *mgr) noexcept;
#endif

  private:
    char    bundle_path_[512]{};
    char    doc_path_[512]{};
    VfsBlob read_file(const char *full_path) noexcept;
};

extern Vfs g_vfs;

// ─── Async LoadRequest ───────────────────────────────────────────
struct LoadRequest {
    const char *path; // caller keeps alive
    void (*callback)(VfsBlob blob, void *user) noexcept;
    void *user;
};

// ─── AssetLoader — Async Dispatch + Completion ───────────────────
class AssetLoader {
  public:
    void init(Vfs *vfs) noexcept;
    void submit(const LoadRequest &req) noexcept;
    void process_completed() noexcept; // call once per frame on main thread

  private:
    static constexpr u32 kMaxPending = 256;

    struct LoadResult {
        VfsBlob blob;
        void (*callback)(VfsBlob, void *) noexcept;
        void *user;
    };

    Vfs             *vfs_ = nullptr;
    LoadResult       results_[kMaxPending];
    u32         head_ = 0;
    u32         tail_ = 0;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

    bool             push_result(const LoadResult &r) noexcept;
    bool             pop_result(LoadResult &r) noexcept;

    static void      load_job(void *data) noexcept;
};

extern AssetLoader g_asset_loader;

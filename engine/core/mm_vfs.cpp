// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_vfs.hpp"
#include "mm_job_system.hpp"
#include "mm_log.hpp"
#include "mm_pool.hpp"
#include "mm_tracy.hpp"

#include <cstdio>
#include <cstring>

// ─── Platform ────────────────────────────────────────────────────
#if defined(TARGET_IOS) || defined(TARGET_MACOS) || defined(__APPLE__)
#    include <unistd.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#elif defined(TARGET_ANDROID)
#    include <android/asset_manager.h>
#    include <unistd.h>
#    include <fcntl.h>
#    include <sys/stat.h>
static AAssetManager *g_asset_mgr = nullptr;
#elif defined(__linux__) || defined(__EMSCRIPTEN__)
#    include <unistd.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#elif defined(_WIN32)
#    include <windows.h>
#    include <sys/stat.h>
#endif

// ─── VfsBlob ─────────────────────────────────────────────────────
void VfsBlob::free() noexcept {
    if (!data) {
        return;
    }
    // Try pool free first; if unowned, falls through to rpmalloc
    pool_free_sized(data, size);
    data = nullptr;
    size = 0;
}

// ─── Vfs ─────────────────────────────────────────────────────────
Vfs  g_vfs;

void Vfs::init(const char *bundle_path, const char *doc_path) noexcept {
    if (bundle_path) {
        size_t len = std::strlen(bundle_path);
        if (len >= sizeof(bundle_path_) - 1) {
            len = sizeof(bundle_path_) - 1;
        }
        std::memcpy(bundle_path_, bundle_path, len);
        bundle_path_[len] = '\0';
    }
    if (doc_path) {
        size_t len = std::strlen(doc_path);
        if (len >= sizeof(doc_path_) - 1) {
            len = sizeof(doc_path_) - 1;
        }
        std::memcpy(doc_path_, doc_path, len);
        doc_path_[len] = '\0';
    }
}

VfsBlob Vfs::read_bundle(const char *path) noexcept {
    char full[1024];
    int  n;
    if (bundle_path_[0] == '\0') {
        n = std::snprintf(full, sizeof(full), "%s", path);
    } else {
        n = std::snprintf(full, sizeof(full), "%s/%s", bundle_path_, path);
    }
    if (n < 0 || static_cast<size_t>(n) >= sizeof(full)) {
        return {};
    }
    return read_file(full);
}

VfsBlob Vfs::read_doc(const char *path) noexcept {
    char full[1024];
    int  n = std::snprintf(full, sizeof(full), "%s/%s", doc_path_, path);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(full)) {
        return {};
    }
    return read_file(full);
}

VfsBlob Vfs::read_absolute(const char *path) noexcept { return read_file(path); }

bool    Vfs::write_doc(const char *path, const void *data, uint64_t size) noexcept {
    char full[1024];
    int  n = std::snprintf(full, sizeof(full), "%s/%s", doc_path_, path);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(full)) {
        return false;
    }

    FILE *f = std::fopen(full, "wb");
    if (!f) {
        return false;
    }
    bool ok = (std::fwrite(data, 1, static_cast<size_t>(size), f) == size);
    (void)std::fclose(f);
    return ok;
}

bool Vfs::write_doc_atomic(const char *path, const void *data, uint64_t size) noexcept {
    char tmp[1024], dst[1024];
    int  n = std::snprintf(tmp, sizeof(tmp), "%s/%s.tmp", doc_path_, path);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(tmp)) {
        return false;
    }
    n = std::snprintf(dst, sizeof(dst), "%s/%s", doc_path_, path);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(dst)) {
        return false;
    }

    FILE *f = std::fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    bool ok = (std::fwrite(data, 1, static_cast<size_t>(size), f) == size);
    (void)std::fclose(f);
    if (!ok) {
        (void)std::remove(tmp);
        return false;
    }

    if (std::rename(tmp, dst) != 0) {
        (void)std::remove(tmp);
        return false;
    }
    return true;
}

bool Vfs::exists(const char *path) noexcept {
#if defined(__APPLE__) || defined(__linux__) || defined(__EMSCRIPTEN__)
    struct stat st{};
    return ::stat(path, &st) == 0;
#elif defined(_WIN32)
    struct __stat64 st{};
    return ::_stat64(path, &st) == 0;
#else
    return false;
#endif
}

#if defined(TARGET_ANDROID)
void Vfs::set_asset_manager(void *mgr) noexcept { g_asset_mgr = static_cast<AAssetManager *>(mgr); }
#endif

VfsBlob Vfs::read_file(const char *full_path) noexcept {
    ZoneScoped;
#if defined(TARGET_ANDROID)
    // Try AAsset first (bundle assets are in APK)
    if (g_asset_mgr) {
        AAsset *asset = AAssetManager_open(g_asset_mgr, full_path, AASSET_MODE_BUFFER);
        if (asset) {
            MM_LOG("VFS: Successfully opened asset: %s", full_path);
            off64_t size = AAsset_getLength64(asset);

            if (size <= 0) {
                MM_ERROR("VFS: Asset found but size <= 0: %s", full_path);
                AAsset_close(asset);
                return {};
            }
            // mmap the asset data directly (avoids copy)
            const void *src = AAsset_getBuffer(asset);
            if (!src) {
                MM_ERROR("VFS: Failed to get buffer for asset: %s", full_path);
                AAsset_close(asset);
                return {};
            }

            auto  blob_size = static_cast<uint64_t>(size);
            void *dst       = pool_alloc(static_cast<size_t>(blob_size));
            if (!dst) {
                MM_ERROR("VFS: Out of memory for asset blob: %s (%llu bytes)", full_path, (unsigned long long)blob_size);
                AAsset_close(asset);
                return {};
            }
            std::memcpy(dst, src, static_cast<size_t>(blob_size));
            AAsset_close(asset);
            return {dst, blob_size};
        } else {
            // MM_LOG("VFS: Asset not found in AAssetManager: %s", full_path);
        }
    } else {
        MM_ERROR("VFS: AAssetManager is NULL! Cannot read asset: %s", full_path);
    }
#endif
    // POSIX fallback (works on all platforms including Android for doc path)
#if defined(__APPLE__) || defined(__linux__) || defined(__EMSCRIPTEN__)
    int fd = ::open(full_path, O_RDONLY);
    if (fd < 0) {
        return {};
    }

    struct stat st{};
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        return {};
    }
    auto blob_size = static_cast<uint64_t>(st.st_size);
    if (blob_size == 0) {
        ::close(fd);
        return {};
    }

    void *dst = pool_alloc(static_cast<size_t>(blob_size));
    if (!dst) {
        ::close(fd);
        return {};
    }

    ssize_t bytes = ::read(fd, dst, static_cast<size_t>(blob_size));
    ::close(fd);
    if (bytes < 0 || static_cast<uint64_t>(bytes) != blob_size) {
        pool_free_sized(dst, static_cast<size_t>(blob_size));
        return {};
    }
    return {dst, blob_size};
#elif defined(_WIN32)
    // Windows: use CreateFile/ReadFile pattern
    HANDLE hFile = CreateFileA(full_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return {};
    }

    LARGE_INTEGER size_li{};
    if (!GetFileSizeEx(hFile, &size_li) || size_li.QuadPart <= 0) {
        CloseHandle(hFile);
        return {};
    }

    auto blob_size = static_cast<uint64_t>(size_li.QuadPart);
    void *dst = pool_alloc(static_cast<size_t>(blob_size));
    if (!dst) {
        CloseHandle(hFile);
        return {};
    }

    DWORD bytes_read = 0;
    bool ok = ReadFile(hFile, dst, static_cast<DWORD>(blob_size), &bytes_read, nullptr) && bytes_read == blob_size;
    CloseHandle(hFile);
    if (!ok) {
        pool_free_sized(dst, static_cast<size_t>(blob_size));
        return {};
    }
    return {dst, blob_size};
#else
    return {};
#endif
}

// ─── AssetLoader ─────────────────────────────────────────────────
AssetLoader g_asset_loader;

struct LoadJobData {
    LoadRequest req;
    char        path_buf[256];
};

void AssetLoader::init(Vfs *vfs) noexcept { vfs_ = vfs; }

void AssetLoader::submit(const LoadRequest &req) noexcept {
    auto *jd = static_cast<LoadJobData *>(pool_alloc(sizeof(LoadJobData)));
    if (!jd) {
        return;
    }

    jd->req.callback = req.callback;
    jd->req.user     = req.user;
    jd->req.path     = jd->path_buf;

    size_t len       = std::strlen(req.path);
    if (len >= sizeof(jd->path_buf)) {
        len = sizeof(jd->path_buf) - 1;
    }
    std::memcpy(jd->path_buf, req.path, len);
    jd->path_buf[len] = '\0';

    g_job_system.run_fn(load_job, jd);
}

void AssetLoader::process_completed() noexcept {
    LoadResult r;
    while (pop_result(r)) {
        if (r.callback) {
            r.callback(r.blob, r.user);
        }
    }
}

bool AssetLoader::push_result(const LoadResult &r) noexcept {
    while (lock_.test_and_set(std::memory_order_acquire)) {
    }
    uint32_t next = (tail_ + 1) & (kMaxPending - 1);
    if (next == head_) {
        lock_.clear(std::memory_order_release);
        return false;
    }
    results_[tail_] = r;
    tail_           = next;
    lock_.clear(std::memory_order_release);
    return true;
}

bool AssetLoader::pop_result(LoadResult &r) noexcept {
    while (lock_.test_and_set(std::memory_order_acquire)) {
    }
    if (head_ == tail_) {
        lock_.clear(std::memory_order_release);
        return false;
    }
    r     = results_[head_];
    head_ = (head_ + 1) & (kMaxPending - 1);
    lock_.clear(std::memory_order_release);
    return true;
}

void AssetLoader::load_job(void *data) noexcept {
    ZoneScoped;
    auto      *jd   = static_cast<LoadJobData *>(data);
    VfsBlob    blob = g_vfs.read_bundle(jd->req.path);

    LoadResult r;
    r.blob     = blob;
    r.callback = jd->req.callback;
    r.user     = jd->req.user;

    if (!g_asset_loader.push_result(r)) {
        // Queue full — free blob and leak the request
        blob.free();
    }

    pool_free_sized(jd, sizeof(LoadJobData));
}

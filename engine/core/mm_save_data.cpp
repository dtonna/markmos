// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_save_data.hpp"
#include "mm_vfs.hpp"
#include <cstring>

SaveData g_save_data;

bool save_data_load(Vfs& vfs, const char* path) noexcept {
    SaveData data;
    VfsBlob blob = vfs.read_doc(path);
    if (!blob.valid()) {
        data.init();
        g_save_data = data;
        return false;
    }

    size_t copy_size = static_cast<size_t>(blob.size);
    if (copy_size > sizeof(SaveData)) copy_size = sizeof(SaveData);
    std::memcpy(&data, blob.data, copy_size);
    blob.free();

    if (!data.valid()) {
        data.init();
        g_save_data = data;
        return false;
    }

    g_save_data = data;
    return true;
}

bool save_data_save(Vfs& vfs, const SaveData& data, const char* path) noexcept {
    SaveData tmp = data;
    tmp.finalize();
    return vfs.write_doc_atomic(path, &tmp, sizeof(tmp));
}

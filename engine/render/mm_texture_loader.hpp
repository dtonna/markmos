#pragma once
#include "../rhi/mm_rhi_concept.hpp"
#include "../core/mm_expected.hpp"
#include "../core/mm_vfs.hpp"
#include <cstdint>
#include <cstddef>

// Result of a successful texture load
struct TextureInfo {
    TextureHandle handle;
    uint16_t      width;
    uint16_t      height;
    uint8_t       channels;  // always 4 (RGBA)
};

// stb_image forward declarations
extern "C" {
    unsigned char* stbi_load_from_memory(unsigned char const* buffer, int len,
                                         int* x, int* y, int* channels_in_file,
                                         int desired_channels);
    void stbi_image_free(void* retval_from_stbi_load);
}

// ─── Load from raw file bytes ────────────────────────────────────
// Accepts PNG, JPG, BMP, GIF, TGA, PSD, HDR, PIC, PNM via stb_image.
// Decodes to RGBA8, creates a GPU texture, returns handle.
template<RHI_Backend B>
Expected<TextureInfo, RHIError> texture_load_from_memory(
    B& backend, const void* data, size_t size, bool srgb = true) noexcept
{
    int w, h, channels;
    unsigned char* pixels = stbi_load_from_memory(
        static_cast<const unsigned char*>(data),
        static_cast<int>(size),
        &w, &h, &channels, 4);

    if (!pixels) return make_unexpected(RHIError::BackendError);

    TextureDesc desc{};
    desc.type        = TextureType::Tex2D;
    desc.format      = srgb ? PixelFormat::R8G8B8A8_SRGB : PixelFormat::R8G8B8A8_UNORM;
    desc.width       = static_cast<uint16_t>(w);
    desc.height      = static_cast<uint16_t>(h);
    desc.depth       = 1;
    desc.mip_levels  = 1;
    desc.array_layers = 1;

    auto tex_result = backend.create_texture(desc);
    if (!tex_result) {
        stbi_image_free(pixels);
        return make_unexpected(tex_result.error());
    }

    TextureHandle handle = *tex_result;

    auto update_result = backend.update_texture(handle, pixels, 0, 0,
                                                desc.width, desc.height, 0, 0);

    stbi_image_free(pixels);

    if (!update_result) {
        backend.destroy_texture(handle);
        return make_unexpected(update_result.error());
    }

    return TextureInfo{handle, desc.width, desc.height, 4};
}

// ─── Load from bundle file via VFS ───────────────────────────────
template<RHI_Backend B>
Expected<TextureInfo, RHIError> texture_load_from_file(
    B& backend, Vfs& vfs, const char* path, bool srgb = true) noexcept
{
    VfsBlob blob = vfs.read_bundle(path);
    if (!blob.valid()) return make_unexpected(RHIError::BackendError);

    auto result = texture_load_from_memory(backend, blob.data,
                                           static_cast<size_t>(blob.size), srgb);
    blob.free();
    return result;
}

#include "port/TpkTexture.h"
#include "port/TpkMetadata.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>

vita2d_texture *LoadTpkArgb32BaseLevel(const char *path,
                                       const TpkMetadata &meta,
                                       std::size_t texture_index) {
    if (!meta.valid || texture_index >= meta.displayed_textures)
        return nullptr;

    const TpkTextureMetadata &t = meta.textures[texture_index];

    if (t.format != 0x15u || t.width == 0 || t.height == 0)
        return nullptr;

    const std::uint64_t expected =
        static_cast<std::uint64_t>(t.width) *
        static_cast<std::uint64_t>(t.height) * 4u;

    if (expected != t.base_size ||
        static_cast<std::uint64_t>(t.data_offset) + t.base_size > meta.data_blob_size) {
        return nullptr;
    }

    FILE *f = std::fopen(path, "rb");
    if (!f)
        return nullptr;

    const std::uint32_t absolute_offset = meta.data_blob_offset + t.data_offset;
    if (std::fseek(f, static_cast<long>(absolute_offset), SEEK_SET) != 0) {
        std::fclose(f);
        return nullptr;
    }

    std::uint8_t *src =
        static_cast<std::uint8_t *>(std::malloc(t.base_size));
    if (!src) {
        std::fclose(f);
        return nullptr;
    }

    if (std::fread(src, 1, t.base_size, f) != t.base_size) {
        std::free(src);
        std::fclose(f);
        return nullptr;
    }
    std::fclose(f);

    vita2d_texture *texture =
        vita2d_create_empty_texture(t.width, t.height);
    if (!texture) {
        std::free(src);
        return nullptr;
    }

    auto *dst = static_cast<std::uint8_t *>(vita2d_texture_get_datap(texture));
    const unsigned int stride = vita2d_texture_get_stride(texture);

    // NFSMW D3DFMT_A8R8G8B8 is stored as BGRA bytes on little-endian PC.
    // libvita2d's default A8B8G8R8 texture receives RGBA byte order.
    for (std::uint32_t y = 0; y < t.height; ++y) {
        const std::uint8_t *srow =
            src + static_cast<std::size_t>(y) * t.width * 4u;
        std::uint8_t *drow =
            dst + static_cast<std::size_t>(y) * stride;

        for (std::uint32_t x = 0; x < t.width; ++x) {
            const std::uint8_t b = srow[x * 4u + 0u];
            const std::uint8_t g = srow[x * 4u + 1u];
            const std::uint8_t r = srow[x * 4u + 2u];
            const std::uint8_t a = srow[x * 4u + 3u];

            drow[x * 4u + 0u] = r;
            drow[x * 4u + 1u] = g;
            drow[x * 4u + 2u] = b;
            drow[x * 4u + 3u] = a;
        }
    }

    std::free(src);
    return texture;
}

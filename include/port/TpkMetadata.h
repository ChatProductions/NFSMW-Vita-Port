#pragma once

#include <cstddef>
#include <cstdint>

struct TpkTextureMetadata {
    char name[25];
    std::uint32_t key;
    std::uint16_t width;
    std::uint16_t height;
    std::uint32_t format;
};

struct TpkMetadata {
    static constexpr std::size_t kMaxTextures = 16;

    bool valid;
    std::uint32_t version;
    char pack_name[29];
    char source_path[65];
    std::uint32_t texture_count;
    TpkTextureMetadata textures[kMaxTextures];
    std::size_t displayed_textures;
};

TpkMetadata ReadTpkMetadata(const char *path);
void WriteTpkMetadataLog(const char *path, const TpkMetadata &meta);
const char *DescribeTpkFormat(std::uint32_t format, char *buffer, std::size_t buffer_size);

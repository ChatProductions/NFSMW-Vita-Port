#pragma once

#include <cstddef>
#include <cstdint>
#include <vita2d.h>

struct TpkMetadata;

enum class TpkTextureLoadResult {
    Ok,
    InvalidMetadata,
    UnsupportedFormat,
    InvalidSize,
    IoError,
    OutOfMemory
};

vita2d_texture *LoadTpkTextureBaseLevel(const char *path,
                                        const TpkMetadata &meta,
                                        std::size_t texture_index,
                                        TpkTextureLoadResult *result = nullptr);

vita2d_texture *LoadTpkTextureBaseLevelMemory(const void *data,
                                              std::uint32_t data_size,
                                              const TpkMetadata &meta,
                                              std::size_t texture_index,
                                              TpkTextureLoadResult *result = nullptr);

const char *DescribeTpkTextureLoadResult(TpkTextureLoadResult result);

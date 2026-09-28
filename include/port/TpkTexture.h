#pragma once

#include <cstddef>
#include <vita2d.h>

struct TpkMetadata;

vita2d_texture *LoadTpkArgb32BaseLevel(const char *path,
                                       const TpkMetadata &meta,
                                       std::size_t texture_index);

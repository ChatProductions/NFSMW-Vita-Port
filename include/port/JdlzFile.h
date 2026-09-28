#pragma once

#include <cstdint>

struct JdlzMemoryResult {
    bool found;
    bool valid;
    std::uint8_t version;
    std::uint8_t header_size;
    std::uint32_t compressed_size;
    std::uint32_t decompressed_size;
    std::uint8_t *data;
    const char *error;
};

JdlzMemoryResult DecompressJdlzFileToMemory(const char *input_path);
void FreeJdlzMemory(JdlzMemoryResult &result);

#pragma once

#include <cstdint>

struct JdlzFileResult {
    bool found;
    bool valid;
    std::uint8_t version;
    std::uint8_t header_size;
    std::uint32_t compressed_size;
    std::uint32_t decompressed_size;
    std::uint32_t bytes_written;
    const char *error;
};

JdlzFileResult DecompressJdlzFile(const char *input_path,
                                  const char *output_path);

// SPDX-License-Identifier: MIT
// JDLZ algorithm adapted from TsyVM/MWSDK (MIT), itself grounded in
// retail NFSMW data and the documented dual-flag JDLZ stream format.

#include "port/JdlzFile.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace {

constexpr std::size_t kHeaderSize = 16;
constexpr std::uint8_t kVersion = 0x02;
constexpr std::uint32_t kMaxDecompressedSize = 64u * 1024u * 1024u;

std::uint32_t ReadLe32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

JdlzFileResult DecompressJdlzFile(const char *input_path,
                                  const char *output_path) {
    JdlzFileResult result{};
    result.error = "input not found";

    FILE *f = std::fopen(input_path, "rb");
    if (!f)
        return result;

    result.found = true;

    if (std::fseek(f, 0, SEEK_END) != 0) {
        result.error = "seek failed";
        std::fclose(f);
        return result;
    }

    const long file_size_long = std::ftell(f);
    if (file_size_long < static_cast<long>(kHeaderSize) ||
        static_cast<unsigned long>(file_size_long) > 0xFFFFFFFFul) {
        result.error = "invalid file size";
        std::fclose(f);
        return result;
    }

    const std::uint32_t file_size =
        static_cast<std::uint32_t>(file_size_long);

    if (std::fseek(f, 0, SEEK_SET) != 0) {
        result.error = "seek failed";
        std::fclose(f);
        return result;
    }

    auto *input =
        static_cast<std::uint8_t *>(std::malloc(file_size));
    if (!input) {
        result.error = "compressed buffer allocation failed";
        std::fclose(f);
        return result;
    }

    if (std::fread(input, 1, file_size, f) != file_size) {
        result.error = "read failed";
        std::free(input);
        std::fclose(f);
        return result;
    }
    std::fclose(f);

    if (input[0] != 'J' || input[1] != 'D' ||
        input[2] != 'L' || input[3] != 'Z') {
        result.error = "not JDLZ";
        std::free(input);
        return result;
    }

    result.version = input[4];
    result.header_size = input[5];
    result.decompressed_size = ReadLe32(input + 8);
    result.compressed_size = ReadLe32(input + 12);

    if (result.version != kVersion ||
        result.header_size != kHeaderSize) {
        result.error = "unsupported JDLZ header";
        std::free(input);
        return result;
    }

    if (result.compressed_size != file_size) {
        result.error = "compressed size mismatch";
        std::free(input);
        return result;
    }

    if (result.decompressed_size == 0 ||
        result.decompressed_size > kMaxDecompressedSize) {
        result.error = "decompressed size rejected";
        std::free(input);
        return result;
    }

    auto *output =
        static_cast<std::uint8_t *>(std::malloc(result.decompressed_size));
    if (!output) {
        result.error = "output buffer allocation failed";
        std::free(input);
        return result;
    }

    std::size_t pos = kHeaderSize;
    std::size_t op = 0;
    std::uint32_t f1 = 1;
    std::uint32_t f2 = 1;
    result.error = "decompression incomplete";

    while (pos < file_size && op < result.decompressed_size) {
        if (f1 == 1) {
            if (pos >= file_size) {
                result.error = "truncated flag stream";
                break;
            }
            f1 = static_cast<std::uint32_t>(input[pos++]) | 0x100u;
        }

        if (f2 == 1) {
            if (pos >= file_size) {
                result.error = "truncated match flags";
                break;
            }
            f2 = static_cast<std::uint32_t>(input[pos++]) | 0x100u;
        }

        if (f1 & 1u) {
            if (pos + 2 > file_size) {
                result.error = "truncated match";
                break;
            }

            const std::uint32_t b0 = input[pos + 0];
            const std::uint32_t b1 = input[pos + 1];
            pos += 2;

            std::size_t length = 0;
            std::size_t distance = 0;

            if (f2 & 1u) {
                length = (((b0 & 0xF0u) << 4) | b1) + 3u;
                distance = (b0 & 0x0Fu) + 1u;
            } else {
                length = (b0 & 0x1Fu) + 3u;
                distance = (((b0 & 0xE0u) << 3) | b1) + 17u;
            }

            if (distance > op) {
                result.error = "invalid back-reference";
                break;
            }

            for (std::size_t i = 0;
                 i < length && op < result.decompressed_size;
                 ++i) {
                output[op] = output[op - distance];
                ++op;
            }

            f2 >>= 1;
        } else {
            if (pos >= file_size) {
                result.error = "truncated literal";
                break;
            }
            output[op++] = input[pos++];
        }

        f1 >>= 1;
    }

    std::free(input);

    if (op != result.decompressed_size) {
        if (result.error == nullptr)
            result.error = "decompressed size mismatch";
        std::free(output);
        return result;
    }

    FILE *out = std::fopen(output_path, "wb");
    if (!out) {
        result.error = "cannot create cache file";
        std::free(output);
        return result;
    }

    const std::size_t written =
        std::fwrite(output, 1, result.decompressed_size, out);
    std::fclose(out);
    std::free(output);

    if (written != result.decompressed_size) {
        result.error = "cache write failed";
        return result;
    }

    result.bytes_written = static_cast<std::uint32_t>(written);
    result.valid = true;
    result.error = "OK";
    return result;
}

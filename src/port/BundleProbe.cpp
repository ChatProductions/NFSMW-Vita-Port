#include "port/BundleProbe.h"
#include "decomp/bChunk.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr std::uint32_t kMaxVisitedChunks = 4096;

void RecordChunk(const bChunk &chunk,
                 std::uint32_t pos,
                 std::uint8_t depth,
                 BundleProbeResult &out) {
    if (out.displayed_chunks >= BundleProbeResult::kMaxDisplayedChunks)
        return;

    BundleChunkInfo &info = out.chunks[out.displayed_chunks++];
    info.id = chunk.GetID();
    info.size = chunk.GetSize();
    info.offset = pos;
    info.depth = depth;
    info.nested = chunk.IsNestedChunk();
}

bool ValidateChunk(const bChunk &chunk,
                   std::uint32_t pos,
                   std::uint32_t end,
                   std::uint64_t &payload_begin,
                   std::uint64_t &next,
                   BundleProbeResult &out) {
    if (chunk.GetSize() < 0) {
        out.invalid_offset = pos;
        return false;
    }

    payload_begin =
        static_cast<std::uint64_t>(pos) + sizeof(bChunk);
    next =
        payload_begin + static_cast<std::uint32_t>(chunk.GetSize());

    if (next > end || next <= pos) {
        out.invalid_offset = pos;
        return false;
    }

    return true;
}

bool ReadChunk(FILE *f, std::uint32_t offset, bChunk &chunk) {
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0)
        return false;
    return std::fread(&chunk, sizeof(chunk), 1, f) == 1;
}

bool ParseRegionFile(FILE *f,
                     std::uint32_t begin,
                     std::uint32_t end,
                     std::uint8_t depth,
                     BundleProbeResult &out,
                     std::uint32_t &visited) {
    std::uint32_t pos = begin;

    while (pos < end) {
        if (visited >= kMaxVisitedChunks || end - pos < sizeof(bChunk)) {
            out.invalid_offset = pos;
            return false;
        }

        bChunk chunk{};
        if (!ReadChunk(f, pos, chunk)) {
            out.invalid_offset = pos;
            return false;
        }

        std::uint64_t payload_begin = 0;
        std::uint64_t next = 0;
        if (!ValidateChunk(chunk, pos, end, payload_begin, next, out))
            return false;

        RecordChunk(chunk, pos, depth, out);
        ++visited;
        ++out.parsed_chunks;

        if (chunk.IsNestedChunk() &&
            chunk.GetSize() >= static_cast<int>(sizeof(bChunk))) {
            if (!ParseRegionFile(f,
                                 static_cast<std::uint32_t>(payload_begin),
                                 static_cast<std::uint32_t>(next),
                                 static_cast<std::uint8_t>(depth + 1),
                                 out,
                                 visited)) {
                return false;
            }
        }

        pos = static_cast<std::uint32_t>(next);
    }

    return pos == end;
}

bool ParseRegionMemory(const std::uint8_t *data,
                       std::uint32_t begin,
                       std::uint32_t end,
                       std::uint8_t depth,
                       BundleProbeResult &out,
                       std::uint32_t &visited) {
    std::uint32_t pos = begin;

    while (pos < end) {
        if (visited >= kMaxVisitedChunks || end - pos < sizeof(bChunk)) {
            out.invalid_offset = pos;
            return false;
        }

        bChunk chunk{};
        std::memcpy(&chunk, data + pos, sizeof(chunk));

        std::uint64_t payload_begin = 0;
        std::uint64_t next = 0;
        if (!ValidateChunk(chunk, pos, end, payload_begin, next, out))
            return false;

        RecordChunk(chunk, pos, depth, out);
        ++visited;
        ++out.parsed_chunks;

        if (chunk.IsNestedChunk() &&
            chunk.GetSize() >= static_cast<int>(sizeof(bChunk))) {
            if (!ParseRegionMemory(data,
                                   static_cast<std::uint32_t>(payload_begin),
                                   static_cast<std::uint32_t>(next),
                                   static_cast<std::uint8_t>(depth + 1),
                                   out,
                                   visited)) {
                return false;
            }
        }

        pos = static_cast<std::uint32_t>(next);
    }

    return pos == end;
}

} // namespace

BundleProbeResult ProbeNfsmwBundle(const char *path) {
    BundleProbeResult out{};

    FILE *f = std::fopen(path, "rb");
    if (!f)
        return out;

    out.found = true;

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return out;
    }

    const long file_size = std::ftell(f);
    if (file_size <= 0 || static_cast<unsigned long>(file_size) > 0xFFFFFFFFul) {
        std::fclose(f);
        return out;
    }

    out.file_size = static_cast<std::uint32_t>(file_size);

    std::uint32_t visited = 0;
    out.valid = ParseRegionFile(f, 0, out.file_size, 0, out, visited);

    std::fclose(f);
    return out;
}

BundleProbeResult ProbeNfsmwBundleMemory(const void *data, std::uint32_t size) {
    BundleProbeResult out{};
    if (!data || size == 0)
        return out;

    out.found = true;
    out.file_size = size;

    std::uint32_t visited = 0;
    out.valid = ParseRegionMemory(
        static_cast<const std::uint8_t *>(data),
        0,
        size,
        0,
        out,
        visited);

    return out;
}

void WriteBundleProbeLog(const char *path, const BundleProbeResult &result) {
    FILE *f = std::fopen("ux0:data/nfsmw-vita-port-m1.log", "w");
    if (!f)
        return;

    std::fprintf(f, "NFSMW Vita Port - Milestone 1\n");
    std::fprintf(f, "Bundle: %s\n", path);
    std::fprintf(f, "Found: %s\n", result.found ? "yes" : "no");

    if (result.found) {
        std::fprintf(f, "File size: %u\n", result.file_size);
        std::fprintf(f, "Parse: %s\n", result.valid ? "VALID" : "INVALID");
        std::fprintf(f, "Chunks visited: %u\n", result.parsed_chunks);
        if (!result.valid)
            std::fprintf(f, "Invalid offset: 0x%08X\n", result.invalid_offset);

        for (std::size_t i = 0; i < result.displayed_chunks; ++i) {
            const BundleChunkInfo &c = result.chunks[i];
            std::fprintf(f,
                         "%02u depth=%u off=0x%08X id=0x%08X size=%d %s\n",
                         static_cast<unsigned>(i),
                         static_cast<unsigned>(c.depth),
                         c.offset,
                         c.id,
                         c.size,
                         c.nested ? "nested" : "data");
        }
    }

    std::fclose(f);
}

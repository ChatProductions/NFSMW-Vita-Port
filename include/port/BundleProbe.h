#pragma once

#include <cstddef>
#include <cstdint>

struct BundleChunkInfo {
    std::uint32_t id;
    std::int32_t size;
    std::uint32_t offset;
    std::uint8_t depth;
    bool nested;
};

struct BundleProbeResult {
    static constexpr std::size_t kMaxDisplayedChunks = 12;

    bool found;
    bool valid;
    std::uint32_t file_size;
    std::uint32_t parsed_chunks;
    std::uint32_t invalid_offset;
    BundleChunkInfo chunks[kMaxDisplayedChunks];
    std::size_t displayed_chunks;
};

BundleProbeResult ProbeNfsmwBundle(const char *path);
BundleProbeResult ProbeNfsmwBundleMemory(const void *data, std::uint32_t size);
void WriteBundleProbeLog(const char *path, const BundleProbeResult &result);

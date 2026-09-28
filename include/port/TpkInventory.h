#pragma once

#include <cstddef>
#include <cstdint>

struct TpkPackSummary {
    char name[29];
    std::uint32_t version;
    std::uint32_t texture_count;
    std::uint32_t container_offset;
    std::uint32_t container_size;
    bool compressed_entries;
};

struct TpkInventory {
    static constexpr std::size_t kMaxPacks = 32;

    bool valid;
    std::uint32_t pack_count_total;
    TpkPackSummary packs[kMaxPacks];
    std::size_t displayed_packs;
    const char *error;
};

TpkInventory ScanTpkPacksMemory(const void *data, std::uint32_t size);

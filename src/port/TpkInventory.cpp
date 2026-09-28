#include "port/TpkInventory.h"

#include <cstring>

namespace {

constexpr std::uint32_t kTpkContainer = 0xB3300000u;
constexpr std::uint32_t kTpkHeader = 0x33310001u;
constexpr std::uint32_t kTpkEntries = 0x33310004u;
constexpr std::uint32_t kTpkCompressedEntries = 0x33310003u;
constexpr std::uint32_t kMaxChunksVisited = 8192u;

struct ChunkHeader {
    std::uint32_t id;
    std::uint32_t size;
};

struct PackFields {
    bool header_found = false;
    bool entries_found = false;
    bool compressed_entries = false;
    std::uint32_t version = 0;
    char name[29]{};
    char source_path[65]{};
    std::uint32_t texture_count = 0;
};

bool ReadHeader(const std::uint8_t *data,
                std::uint32_t size,
                std::uint32_t offset,
                ChunkHeader &out) {
    if (!data || offset > size || size - offset < sizeof(ChunkHeader))
        return false;
    std::memcpy(&out, data + offset, sizeof(out));
    return true;
}

template <std::size_t N>
void CopyText(char (&dst)[N],
              const std::uint8_t *src,
              std::size_t src_size) {
    const std::size_t n =
        src_size < (N - 1) ? src_size : (N - 1);

    std::memcpy(dst, src, n);
    dst[n] = '\0';

    for (std::size_t i = 0; i < n; ++i) {
        if (dst[i] == '\0')
            break;
        const unsigned char ch =
            static_cast<unsigned char>(dst[i]);
        if (ch < 0x20 || ch > 0x7E)
            dst[i] = '?';
    }
}

bool ScanPackFields(const std::uint8_t *data,
                    std::uint32_t begin,
                    std::uint32_t end,
                    PackFields &fields,
                    std::uint32_t &visited,
                    std::uint32_t depth = 0) {
    if (depth > 16)
        return false;

    std::uint32_t pos = begin;

    while (pos < end) {
        if (++visited > kMaxChunksVisited)
            return false;

        ChunkHeader h{};
        if (!ReadHeader(data, end, pos, h))
            return false;

        const std::uint64_t payload =
            static_cast<std::uint64_t>(pos) + sizeof(ChunkHeader);
        const std::uint64_t next = payload + h.size;

        if (next > end || next <= pos)
            return false;

        if (h.id == kTpkHeader) {
            if (h.size < 32)
                return false;

            fields.header_found = true;
            std::memcpy(&fields.version, data + payload, sizeof(fields.version));
            CopyText(fields.name, data + payload + 4, 28);
            if (h.size >= 96)
                CopyText(fields.source_path, data + payload + 32, 64);
        } else if (h.id == kTpkEntries) {
            if ((h.size % 124u) != 0u)
                return false;
            fields.entries_found = true;
            fields.compressed_entries = false;
            fields.texture_count = h.size / 124u;
        } else if (h.id == kTpkCompressedEntries) {
            if ((h.size % 24u) != 0u)
                return false;
            fields.entries_found = true;
            fields.compressed_entries = true;
            fields.texture_count = h.size / 24u;
        }

        if ((h.id & 0x80000000u) != 0u && h.size >= sizeof(ChunkHeader)) {
            if (!ScanPackFields(data,
                                static_cast<std::uint32_t>(payload),
                                static_cast<std::uint32_t>(next),
                                fields,
                                visited,
                                depth + 1)) {
                return false;
            }
        }

        pos = static_cast<std::uint32_t>(next);
    }

    return pos == end;
}

bool ScanRegion(const std::uint8_t *data,
                std::uint32_t begin,
                std::uint32_t end,
                TpkInventory &out,
                std::uint32_t &visited,
                std::uint32_t depth = 0) {
    if (depth > 16)
        return false;

    std::uint32_t pos = begin;

    while (pos < end) {
        if (++visited > kMaxChunksVisited)
            return false;

        ChunkHeader h{};
        if (!ReadHeader(data, end, pos, h))
            return false;

        const std::uint64_t payload =
            static_cast<std::uint64_t>(pos) + sizeof(ChunkHeader);
        const std::uint64_t next = payload + h.size;

        if (next > end || next <= pos)
            return false;

        if (h.id == kTpkContainer) {
            PackFields fields{};
            std::uint32_t pack_visited = 0;

            if (!ScanPackFields(data,
                                static_cast<std::uint32_t>(payload),
                                static_cast<std::uint32_t>(next),
                                fields,
                                pack_visited)) {
                out.error = "malformed TPK container";
                return false;
            }

            ++out.pack_count_total;

            if (out.displayed_packs < TpkInventory::kMaxPacks) {
                TpkPackSummary &pack = out.packs[out.displayed_packs++];
                std::memcpy(pack.name, fields.name, sizeof(pack.name));
                std::memcpy(pack.source_path,
                            fields.source_path,
                            sizeof(pack.source_path));
                pack.version = fields.version;
                pack.texture_count = fields.entries_found
                    ? fields.texture_count
                    : 0u;
                pack.container_offset = pos;
                pack.container_size = h.size + sizeof(ChunkHeader);
                pack.compressed_entries = fields.compressed_entries;
            }
        } else if ((h.id & 0x80000000u) != 0u &&
                   h.size >= sizeof(ChunkHeader)) {
            if (!ScanRegion(data,
                            static_cast<std::uint32_t>(payload),
                            static_cast<std::uint32_t>(next),
                            out,
                            visited,
                            depth + 1)) {
                return false;
            }
        }

        pos = static_cast<std::uint32_t>(next);
    }

    return pos == end;
}

} // namespace

TpkInventory ScanTpkPacksMemory(const void *data, std::uint32_t size) {
    TpkInventory out{};
    out.error = "invalid input";

    if (!data || size < sizeof(ChunkHeader))
        return out;

    std::uint32_t visited = 0;
    if (!ScanRegion(static_cast<const std::uint8_t *>(data),
                    0,
                    size,
                    out,
                    visited)) {
        if (!out.error || std::strcmp(out.error, "invalid input") == 0)
            out.error = "invalid EAGL chunk structure";
        return out;
    }

    out.valid = true;
    out.error = "OK";
    return out;
}

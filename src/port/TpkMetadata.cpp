#include "port/TpkMetadata.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr std::uint32_t kInfoHeader = 0x33310001u;
constexpr std::uint32_t kHashTable  = 0x33310002u;
constexpr std::uint32_t kEntries    = 0x33310004u;
constexpr std::uint32_t kCompInfo   = 0x33310005u;
constexpr std::uint32_t kDataRaw    = 0x33320002u;

struct ChunkHeader {
    std::uint32_t id;
    std::uint32_t size;
};

struct FoundChunk {
    bool found = false;
    std::uint32_t payload_offset = 0;
    std::uint32_t size = 0;
};

bool ReadAt(FILE *f, std::uint32_t offset, void *dst, std::size_t size) {
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0)
        return false;
    return std::fread(dst, 1, size, f) == size;
}

bool FindChunks(FILE *f,
                std::uint32_t begin,
                std::uint32_t end,
                FoundChunk &info,
                FoundChunk &hashes,
                FoundChunk &entries,
                FoundChunk &comp,
                FoundChunk &data_raw,
                std::uint32_t depth = 0) {
    if (depth > 16)
        return false;

    std::uint32_t pos = begin;
    while (pos < end) {
        if (end - pos < sizeof(ChunkHeader))
            return false;

        ChunkHeader h{};
        if (!ReadAt(f, pos, &h, sizeof(h)))
            return false;

        const std::uint64_t payload = static_cast<std::uint64_t>(pos) + sizeof(ChunkHeader);
        const std::uint64_t next = payload + h.size;
        if (next > end || next <= pos)
            return false;

        FoundChunk *target = nullptr;
        if (h.id == kInfoHeader) target = &info;
        else if (h.id == kHashTable) target = &hashes;
        else if (h.id == kEntries) target = &entries;
        else if (h.id == kCompInfo) target = &comp;
        else if (h.id == kDataRaw) target = &data_raw;

        if (target && !target->found) {
            target->found = true;
            target->payload_offset = static_cast<std::uint32_t>(payload);
            target->size = h.size;
        }

        if ((h.id & 0x80000000u) != 0 && h.size >= sizeof(ChunkHeader)) {
            if (!FindChunks(f,
                            static_cast<std::uint32_t>(payload),
                            static_cast<std::uint32_t>(next),
                            info, hashes, entries, comp, data_raw,
                            depth + 1)) {
                return false;
            }
        }

        pos = static_cast<std::uint32_t>(next);
    }

    return pos == end;
}

void CopyFixedString(char *dst, std::size_t dst_size,
                     const char *src, std::size_t src_size) {
    if (dst_size == 0)
        return;
    const std::size_t n = src_size < (dst_size - 1) ? src_size : (dst_size - 1);
    std::memcpy(dst, src, n);
    dst[n] = '\0';
    for (std::size_t i = 0; i < n; ++i) {
        if (dst[i] == '\0')
            break;
        if (static_cast<unsigned char>(dst[i]) < 0x20 ||
            static_cast<unsigned char>(dst[i]) > 0x7E) {
            dst[i] = '?';
        }
    }
}

} // namespace

const char *DescribeTpkFormat(std::uint32_t format, char *buffer, std::size_t buffer_size) {
    if (buffer_size == 0)
        return "";

    const char a = static_cast<char>(format & 0xFF);
    const char b = static_cast<char>((format >> 8) & 0xFF);
    const char c = static_cast<char>((format >> 16) & 0xFF);
    const char d = static_cast<char>((format >> 24) & 0xFF);

    if (a >= 0x20 && a <= 0x7E &&
        b >= 0x20 && b <= 0x7E &&
        c >= 0x20 && c <= 0x7E &&
        d >= 0x20 && d <= 0x7E) {
        std::snprintf(buffer, buffer_size, "%c%c%c%c", a, b, c, d);
    } else {
        std::snprintf(buffer, buffer_size, "0x%08X", format);
    }
    return buffer;
}

TpkMetadata ReadTpkMetadata(const char *path) {
    TpkMetadata out{};

    FILE *f = std::fopen(path, "rb");
    if (!f)
        return out;

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return out;
    }
    const long file_size = std::ftell(f);
    if (file_size <= 0 || static_cast<unsigned long>(file_size) > 0xFFFFFFFFul) {
        std::fclose(f);
        return out;
    }

    FoundChunk info{}, hashes{}, entries{}, comp{}, data_raw{};
    if (!FindChunks(f, 0, static_cast<std::uint32_t>(file_size),
                    info, hashes, entries, comp, data_raw)) {
        std::fclose(f);
        return out;
    }

    if (!info.found || !hashes.found || !entries.found || !comp.found || !data_raw.found ||
        info.size < 124 || (hashes.size % 8) != 0 ||
        (entries.size % 124) != 0 || (comp.size % 32) != 0) {
        std::fclose(f);
        return out;
    }

    const std::uint32_t count_hash = hashes.size / 8;
    const std::uint32_t count_entry = entries.size / 124;
    const std::uint32_t count_comp = comp.size / 32;

    if (count_hash == 0 || count_hash != count_entry || count_hash != count_comp) {
        std::fclose(f);
        return out;
    }

    std::uint8_t header[124]{};
    if (!ReadAt(f, info.payload_offset, header, sizeof(header))) {
        std::fclose(f);
        return out;
    }

    std::memcpy(&out.version, header + 0x00, sizeof(out.version));
    CopyFixedString(out.pack_name, sizeof(out.pack_name),
                    reinterpret_cast<const char *>(header + 0x04), 28);
    CopyFixedString(out.source_path, sizeof(out.source_path),
                    reinterpret_cast<const char *>(header + 0x20), 64);

    out.texture_count = count_hash;
    out.data_blob_offset = data_raw.payload_offset;
    out.data_blob_size = data_raw.size;
    out.displayed_textures =
        count_hash < TpkMetadata::kMaxTextures ? count_hash : TpkMetadata::kMaxTextures;

    std::uint64_t previous_end = 0;
    for (std::size_t i = 0; i < out.displayed_textures; ++i) {
        std::uint8_t entry[124]{};
        std::uint8_t ci[32]{};

        if (!ReadAt(f, entries.payload_offset + static_cast<std::uint32_t>(i * 124),
                    entry, sizeof(entry)) ||
            !ReadAt(f, comp.payload_offset + static_cast<std::uint32_t>(i * 32),
                    ci, sizeof(ci))) {
            std::fclose(f);
            return TpkMetadata{};
        }

        TpkTextureMetadata &t = out.textures[i];
        CopyFixedString(t.name, sizeof(t.name),
                        reinterpret_cast<const char *>(entry + 0x0C), 24);
        std::memcpy(&t.key, entry + 0x24, sizeof(t.key));
        std::memcpy(&t.data_offset, entry + 0x30, sizeof(t.data_offset));
        std::memcpy(&t.total_size, entry + 0x38, sizeof(t.total_size));
        std::memcpy(&t.base_size, entry + 0x40, sizeof(t.base_size));
        std::memcpy(&t.width, entry + 0x44, sizeof(t.width));
        std::memcpy(&t.height, entry + 0x46, sizeof(t.height));
        std::memcpy(&t.mip_count, entry + 0x4E, sizeof(t.mip_count));
        std::memcpy(&t.format, ci + 0x14, sizeof(t.format));

        const std::uint64_t end =
            static_cast<std::uint64_t>(t.data_offset) + t.total_size;
        if (end > out.data_blob_size) {
            std::fclose(f);
            return TpkMetadata{};
        }
        if (i > 0 && t.data_offset < previous_end) {
            std::fclose(f);
            return TpkMetadata{};
        }
        previous_end = end;
    }

    out.valid = true;
    std::fclose(f);
    return out;
}

void WriteTpkMetadataLog(const char *path, const TpkMetadata &meta) {
    FILE *f = std::fopen("ux0:data/nfsmw-vita-port-m3.log", "w");
    if (!f)
        return;

    std::fprintf(f, "NFSMW Vita Port - Milestone 3\n");
    std::fprintf(f, "Bundle: %s\n", path);
    std::fprintf(f, "Metadata: %s\n", meta.valid ? "VALID" : "INVALID");

    if (meta.valid) {
        std::fprintf(f, "Version: %u\n", meta.version);
        std::fprintf(f, "Pack: %s\n", meta.pack_name);
        std::fprintf(f, "Source: %s\n", meta.source_path);
        std::fprintf(f, "Textures: %u\n", meta.texture_count);
        std::fprintf(f, "Pixel blob: off=0x%08X size=%u\n",
                     meta.data_blob_offset, meta.data_blob_size);

        for (std::size_t i = 0; i < meta.displayed_textures; ++i) {
            char fmt[16];
            const TpkTextureMetadata &t = meta.textures[i];
            std::fprintf(f,
                         "%02u %-24s %ux%u key=%08X fmt=%s off=%u total=%u base=%u mips=%u\n",
                         static_cast<unsigned>(i),
                         t.name,
                         static_cast<unsigned>(t.width),
                         static_cast<unsigned>(t.height),
                         t.key,
                         DescribeTpkFormat(t.format, fmt, sizeof(fmt)),
                         t.data_offset,
                         t.total_size,
                         t.base_size,
                         static_cast<unsigned>(t.mip_count));
        }
    }

    std::fclose(f);
}

#include "port/GeometryFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kGeometryContainer = 0x80134000u;
constexpr std::uint32_t kGeometryInfo = 0x80134001u;
constexpr std::uint32_t kGeometryInfoHeader = 0x00134002u;
constexpr std::uint32_t kGeometryDirectory = 0x00134004u;
constexpr std::uint32_t kGeometryObject = 0x80134010u;
constexpr std::uint32_t kObjectHeader = 0x00134011u;
constexpr std::uint32_t kMeshVertices = 0x00134B01u;
constexpr std::uint32_t kMeshGroups = 0x00134B02u;
constexpr std::uint32_t kMeshIndices = 0x00134B03u;

constexpr std::size_t kGroupSize = 104;
constexpr std::size_t kGroupTextureRef = 56;
constexpr std::size_t kGroupVertexCount = 60;
constexpr std::size_t kGroupTriCount = 64;

struct ChunkHeader {
    std::uint32_t id;
    std::uint32_t size;
};

struct FoundChunk {
    bool found = false;
    std::uint32_t payload_offset = 0;
    std::uint32_t size = 0;
};

struct GroupInfo {
    std::uint32_t texture_ref = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t tri_count = 0;
};

bool ReadAt(FILE *f, std::uint32_t offset, void *dst, std::size_t size) {
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0)
        return false;
    return std::fread(dst, 1, size, f) == size;
}

std::uint32_t ReadU32(const std::uint8_t *p) {
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

float ReadF32(const std::uint8_t *p) {
    float v = 0.0f;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

void CopyCString(char *dst,
                 std::size_t dst_size,
                 const std::uint8_t *src,
                 std::size_t src_size) {
    if (!dst || dst_size == 0)
        return;

    std::size_t n = 0;
    while (n + 1 < dst_size &&
           n < src_size &&
           src[n] != 0) {
        const unsigned char c = src[n];
        dst[n] = (c >= 0x20 && c <= 0x7E)
                     ? static_cast<char>(c)
                     : '?';
        ++n;
    }

    dst[n] = '\0';
}

std::size_t StripLeading11(const std::uint8_t *data,
                           std::size_t size) {
    std::size_t n = 0;
    while (n < size && data[n] == 0x11u)
        ++n;
    return n;
}

std::size_t SkipEightByteMarker(const std::uint8_t *data,
                                std::size_t size) {
    if (size < 8)
        return 0;

    bool all_11 = true;
    for (std::size_t i = 0; i < 8; ++i)
        all_11 = all_11 && data[i] == 0x11u;

    return all_11 ? 8u : 0u;
}

bool FindChunkMemory(const std::uint8_t *data,
                     std::uint32_t data_size,
                     std::uint32_t begin,
                     std::uint32_t end,
                     std::uint32_t wanted,
                     FoundChunk &out,
                     unsigned depth = 0) {
    if (!data ||
        depth > 24 ||
        begin > end ||
        end > data_size)
        return false;

    std::uint32_t pos = begin;

    while (pos < end) {
        if (end - pos < sizeof(ChunkHeader))
            return false;

        ChunkHeader h{};
        std::memcpy(&h, data + pos, sizeof(h));

        const std::uint64_t payload =
            static_cast<std::uint64_t>(pos) + sizeof(ChunkHeader);
        const std::uint64_t next = payload + h.size;

        if (next > end || next <= pos)
            return false;

        if (h.id == wanted) {
            out.found = true;
            out.payload_offset = static_cast<std::uint32_t>(payload);
            out.size = h.size;
            return true;
        }

        if ((h.id & 0x80000000u) != 0 &&
            h.size >= sizeof(ChunkHeader)) {
            if (FindChunkMemory(
                    data,
                    data_size,
                    static_cast<std::uint32_t>(payload),
                    static_cast<std::uint32_t>(next),
                    wanted,
                    out,
                    depth + 1)) {
                return true;
            }
        }

        pos = static_cast<std::uint32_t>(next);
    }

    return false;
}

bool LoadTopLevelChild(FILE *f,
                       std::uint32_t file_size,
                       std::uint32_t wanted,
                       std::vector<std::uint8_t> &out) {
    ChunkHeader root{};
    if (!ReadAt(f, 0, &root, sizeof(root)) ||
        root.id != kGeometryContainer)
        return false;

    const std::uint64_t root_end =
        static_cast<std::uint64_t>(sizeof(root)) + root.size;

    if (root_end > file_size)
        return false;

    std::uint32_t pos = sizeof(root);

    while (pos < root_end) {
        ChunkHeader h{};
        if (!ReadAt(f, pos, &h, sizeof(h)))
            return false;

        const std::uint64_t total =
            static_cast<std::uint64_t>(sizeof(h)) + h.size;
        const std::uint64_t next =
            static_cast<std::uint64_t>(pos) + total;

        if (next > root_end || next > file_size || next <= pos)
            return false;

        if (h.id == wanted) {
            out.resize(static_cast<std::size_t>(total));
            return ReadAt(f, pos, out.data(), out.size());
        }

        pos = static_cast<std::uint32_t>(next);
    }

    return false;
}


bool IsSaneBox(const float minv[3], const float maxv[3]) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(minv[i]) ||
            !std::isfinite(maxv[i]) ||
            minv[i] > maxv[i] ||
            std::fabs(minv[i]) > 100000.0f ||
            std::fabs(maxv[i]) > 100000.0f)
            return false;
    }
    return true;
}

float ScoreSolidLayout(
    const std::uint8_t *vertex_data,
    std::size_t vertex_bytes,
    std::uint32_t vertex_count,
    std::uint32_t stride,
    const float bbox_min[3],
    const float bbox_max[3]) {

    if (!vertex_data ||
        vertex_count == 0 ||
        stride < 12 ||
        static_cast<std::uint64_t>(vertex_count) * stride >
            vertex_bytes)
        return -1.0e30f;

    float tolerance[3]{};
    for (int axis = 0; axis < 3; ++axis) {
        const float extent = bbox_max[axis] - bbox_min[axis];
        tolerance[axis] = std::max(0.25f, extent * 0.75f);
    }

    const std::uint32_t samples =
        std::min<std::uint32_t>(vertex_count, 32u);
    const std::uint32_t step =
        std::max<std::uint32_t>(1u, vertex_count / samples);

    float score = 0.0f;
    std::uint32_t seen = 0;

    for (std::uint32_t i = 0;
         i < vertex_count && seen < samples;
         i += step, ++seen) {
        const std::size_t at =
            static_cast<std::size_t>(i) * stride;

        if (at + 12 > vertex_bytes)
            return -1.0e30f;

        const float x = ReadF32(vertex_data + at + 0);
        const float y = ReadF32(vertex_data + at + 4);
        const float z = ReadF32(vertex_data + at + 8);

        if (!std::isfinite(x) ||
            !std::isfinite(y) ||
            !std::isfinite(z) ||
            std::fabs(x) > 100000.0f ||
            std::fabs(y) > 100000.0f ||
            std::fabs(z) > 100000.0f) {
            return -1.0e30f;
        }

        const float p[3]{x, y, z};

        score += 1.0f;
        for (int axis = 0; axis < 3; ++axis) {
            if (p[axis] >= bbox_min[axis] - tolerance[axis] &&
                p[axis] <= bbox_max[axis] + tolerance[axis]) {
                score += 2.0f;
            }
        }

        // 36/60-byte layouts carry an unpacked normal at +12.
        if ((stride == 36 || stride == 60) &&
            at + 24 <= vertex_bytes) {
            const float nx = ReadF32(vertex_data + at + 12);
            const float ny = ReadF32(vertex_data + at + 16);
            const float nz = ReadF32(vertex_data + at + 20);

            if (std::isfinite(nx) &&
                std::isfinite(ny) &&
                std::isfinite(nz)) {
                const float len2 = nx * nx + ny * ny + nz * nz;
                if (len2 > 0.75f && len2 < 1.25f)
                    score += 3.0f;
            }
        }
    }

    return seen ? score : -1.0e30f;
}

bool DetectSolidVertexLayout(
    const std::uint8_t *payload,
    std::size_t payload_size,
    std::uint32_t vertex_count,
    const float bbox_min[3],
    const float bbox_max[3],
    std::uint32_t &out_start,
    std::uint32_t &out_stride) {

    static constexpr std::uint32_t kStrides[] = {36, 60, 24};

    float best_score = -1.0e30f;
    std::uint32_t best_start = 0;
    std::uint32_t best_stride = 0;

    for (std::uint32_t stride : kStrides) {
        const std::uint64_t data_bytes =
            static_cast<std::uint64_t>(vertex_count) * stride;

        if (data_bytes > payload_size)
            continue;

        const std::uint64_t start64 = payload_size - data_bytes;

        // Retail MW vertex chunks use a short marker/preamble. Do not
        // silently accept a layout that would require hundreds of bytes of
        // unexplained leading data.
        if (start64 > 128u)
            continue;

        const std::uint32_t start =
            static_cast<std::uint32_t>(start64);

        const float score =
            ScoreSolidLayout(
                payload + start,
                payload_size - start,
                vertex_count,
                stride,
                bbox_min,
                bbox_max);

        if (score > best_score) {
            best_score = score;
            best_start = start;
            best_stride = stride;
        }
    }

    if (best_stride == 0 || best_score <= 0.0f)
        return false;

    out_start = best_start;
    out_stride = best_stride;
    return true;
}

} // namespace

GeometryIndex ReadGeometryIndex(const char *path) {
    GeometryIndex out{};

    FILE *f = std::fopen(path, "rb");
    if (!f) {
        std::snprintf(out.error, sizeof(out.error), "file not found");
        return out;
    }

    out.found = true;

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::snprintf(out.error, sizeof(out.error), "seek failed");
        std::fclose(f);
        return out;
    }

    const long file_size = std::ftell(f);
    if (file_size <= 0 ||
        static_cast<unsigned long>(file_size) > 0xFFFFFFFFul) {
        std::snprintf(out.error, sizeof(out.error), "invalid file size");
        std::fclose(f);
        return out;
    }

    out.file_size = static_cast<std::uint32_t>(file_size);

    std::vector<std::uint8_t> info_block;
    if (!LoadTopLevelChild(
            f,
            out.file_size,
            kGeometryInfo,
            info_block)) {
        std::snprintf(out.error, sizeof(out.error), "geometry info block missing");
        std::fclose(f);
        return out;
    }

    std::fclose(f);

    FoundChunk list_header{};
    FoundChunk directory{};

    if (!FindChunkMemory(
            info_block.data(),
            static_cast<std::uint32_t>(info_block.size()),
            sizeof(ChunkHeader),
            static_cast<std::uint32_t>(info_block.size()),
            kGeometryInfoHeader,
            list_header) ||
        !FindChunkMemory(
            info_block.data(),
            static_cast<std::uint32_t>(info_block.size()),
            sizeof(ChunkHeader),
            static_cast<std::uint32_t>(info_block.size()),
            kGeometryDirectory,
            directory)) {
        std::snprintf(out.error, sizeof(out.error), "geometry directory tables missing");
        return out;
    }

    if (list_header.size < 0x10 ||
        directory.size < 24) {
        std::snprintf(out.error, sizeof(out.error), "geometry tables too small");
        return out;
    }

    const std::uint8_t *header =
        info_block.data() + list_header.payload_offset;

    const std::size_t header_skip =
        StripLeading11(header, list_header.size);

    if (header_skip + 0x10 > list_header.size) {
        std::snprintf(out.error, sizeof(out.error), "list header padding invalid");
        return out;
    }

    header += header_skip;
    const std::size_t header_size = list_header.size - header_skip;

    if (header_size < 0x10) {
        std::snprintf(out.error, sizeof(out.error), "list header invalid");
        return out;
    }

    out.object_count = ReadU32(header + 0x0C);

    if (header_size > 0x10) {
        CopyCString(
            out.source_name,
            sizeof(out.source_name),
            header + 0x10,
            header_size - 0x10);
    }

    if (out.object_count == 0 ||
        directory.size / 24u < out.object_count) {
        std::snprintf(out.error, sizeof(out.error), "object count mismatch");
        return out;
    }

    out.displayed_objects =
        std::min<std::size_t>(
            out.object_count,
            GeometryIndex::kMaxObjects);

    const std::uint8_t *dir =
        info_block.data() + directory.payload_offset;

    for (std::size_t i = 0; i < out.displayed_objects; ++i) {
        const std::uint8_t *row = dir + i * 24u;

        GeometryDirectoryEntry &e = out.objects[i];
        e.name_hash = ReadU32(row + 0x00);
        e.file_offset = ReadU32(row + 0x04);
        e.stored_size = ReadU32(row + 0x08);

        if (e.file_offset > out.file_size ||
            out.file_size - e.file_offset < sizeof(ChunkHeader)) {
            std::snprintf(out.error, sizeof(out.error),
                          "object %u offset out of range",
                          static_cast<unsigned>(i));
            return out;
        }
    }

    out.valid = true;
    return out;
}

GeometryMesh LoadGeometryObject(
    const char *path,
    const GeometryIndex &index,
    std::size_t object_index) {

    GeometryMesh out{};

    if (!index.valid ||
        object_index >= index.displayed_objects) {
        std::snprintf(out.error, sizeof(out.error), "invalid object index");
        return out;
    }

    FILE *f = std::fopen(path, "rb");
    if (!f) {
        std::snprintf(out.error, sizeof(out.error), "geometry file open failed");
        return out;
    }

    const GeometryDirectoryEntry &entry =
        index.objects[object_index];

    ChunkHeader object_header{};
    if (!ReadAt(
            f,
            entry.file_offset,
            &object_header,
            sizeof(object_header)) ||
        object_header.id != kGeometryObject) {
        std::snprintf(out.error, sizeof(out.error), "directory target is not a solid");
        std::fclose(f);
        return out;
    }

    const std::uint64_t object_total =
        static_cast<std::uint64_t>(sizeof(ChunkHeader)) +
        object_header.size;

    if (object_total > index.file_size - entry.file_offset) {
        std::snprintf(out.error, sizeof(out.error), "solid size out of range");
        std::fclose(f);
        return out;
    }

    std::vector<std::uint8_t> object(
        static_cast<std::size_t>(object_total));

    if (!ReadAt(
            f,
            entry.file_offset,
            object.data(),
            object.size())) {
        std::snprintf(out.error, sizeof(out.error), "solid read failed");
        std::fclose(f);
        return out;
    }

    std::fclose(f);

    FoundChunk header_chunk{};
    FoundChunk vertex_chunk{};
    FoundChunk group_chunk{};
    FoundChunk index_chunk{};

    const std::uint32_t object_size =
        static_cast<std::uint32_t>(object.size());

    if (!FindChunkMemory(
            object.data(), object_size,
            sizeof(ChunkHeader), object_size,
            kObjectHeader, header_chunk) ||
        !FindChunkMemory(
            object.data(), object_size,
            sizeof(ChunkHeader), object_size,
            kMeshVertices, vertex_chunk) ||
        !FindChunkMemory(
            object.data(), object_size,
            sizeof(ChunkHeader), object_size,
            kMeshGroups, group_chunk) ||
        !FindChunkMemory(
            object.data(), object_size,
            sizeof(ChunkHeader), object_size,
            kMeshIndices, index_chunk)) {
        std::snprintf(out.error, sizeof(out.error), "solid mesh chunks missing");
        return out;
    }

    const std::uint8_t *hdr =
        object.data() + header_chunk.payload_offset;

    const std::size_t hdr_skip =
        StripLeading11(hdr, header_chunk.size);

    if (hdr_skip + 0xA0 >= header_chunk.size) {
        std::snprintf(out.error, sizeof(out.error), "solid header too small");
        return out;
    }

    hdr += hdr_skip;
    const std::size_t hdr_size = header_chunk.size - hdr_skip;

    out.flags = ReadU32(hdr + 0x18);
    out.name_hash = ReadU32(hdr + 0x10);
    out.num_tris = ReadU32(hdr + 0x14);

    for (int i = 0; i < 3; ++i) {
        out.bbox_min[i] = ReadF32(hdr + 0x20 + i * 4);
        out.bbox_max[i] = ReadF32(hdr + 0x30 + i * 4);
    }

    CopyCString(
        out.name,
        sizeof(out.name),
        hdr + 0xA0,
        hdr_size - 0xA0);

    if (!IsSaneBox(out.bbox_min, out.bbox_max)) {
        std::snprintf(out.error, sizeof(out.error), "solid bounding box invalid");
        return out;
    }

    const std::uint8_t *groups =
        object.data() + group_chunk.payload_offset;

    std::size_t groups_size = group_chunk.size;
    std::size_t group_start = 0;

    if (groups_size >= 8 &&
        (groups_size - 8u) % kGroupSize == 0) {
        group_start = 8;
    } else if (groups_size % kGroupSize != 0) {
        std::snprintf(out.error, sizeof(out.error), "shading group table size invalid");
        return out;
    }

    out.group_count =
        static_cast<std::uint32_t>(
            (groups_size - group_start) / kGroupSize);

    if (out.group_count == 0 || out.group_count > 256) {
        std::snprintf(out.error, sizeof(out.error), "unsupported shading group count");
        return out;
    }

    std::vector<GroupInfo> parsed_groups(out.group_count);

    std::uint64_t total_vertices = 0;
    std::uint64_t total_tris = 0;

    for (std::size_t i = 0; i < parsed_groups.size(); ++i) {
        const std::uint8_t *g =
            groups + group_start + i * kGroupSize;

        GroupInfo &pg = parsed_groups[i];
        pg.texture_ref = ReadU32(g + kGroupTextureRef);
        pg.vertex_count = ReadU32(g + kGroupVertexCount);
        pg.tri_count = ReadU32(g + kGroupTriCount);

        total_vertices += pg.vertex_count;
        total_tris += pg.tri_count;

        out.group_texture_ref[i] = pg.texture_ref;
        out.group_vertex_count[i] = pg.vertex_count;
        out.group_tri_count[i] = pg.tri_count;
    }

    if (total_tris != out.num_tris) {
        std::snprintf(out.error, sizeof(out.error),
                      "group triangles %u != header %u",
                      static_cast<unsigned>(total_tris),
                      out.num_tris);
        return out;
    }

    const std::uint8_t *vertex_payload =
        object.data() + vertex_chunk.payload_offset;

    if (total_vertices == 0 || total_vertices > 200000u) {
        std::snprintf(out.error, sizeof(out.error), "invalid vertex count");
        return out;
    }

    std::uint32_t vertex_start = 0;
    std::uint32_t vertex_stride = 0;

    if (!DetectSolidVertexLayout(
            vertex_payload,
            vertex_chunk.size,
            static_cast<std::uint32_t>(total_vertices),
            out.bbox_min,
            out.bbox_max,
            vertex_start,
            vertex_stride)) {
        std::snprintf(
            out.error,
            sizeof(out.error),
            "could not detect vertex start/stride");
        return out;
    }

    out.vertex_data_offset = vertex_start;
    out.vertex_stride = vertex_stride;
    out.supported_group_count = out.group_count;

    const std::uint8_t *vertex_data =
        vertex_payload + vertex_start;
    const std::size_t vertex_size =
        vertex_chunk.size - vertex_start;

    out.vertices.reserve(static_cast<std::size_t>(total_vertices));

    for (std::uint32_t v = 0;
         v < static_cast<std::uint32_t>(total_vertices);
         ++v) {
        const std::size_t at =
            static_cast<std::size_t>(v) * vertex_stride;

        if (at + 12 > vertex_size) {
            std::snprintf(out.error, sizeof(out.error), "vertex read overrun");
            return out;
        }

        GeometryVertex gv{
            ReadF32(vertex_data + at + 0),
            ReadF32(vertex_data + at + 4),
            ReadF32(vertex_data + at + 8)
        };

        if (!std::isfinite(gv.x) ||
            !std::isfinite(gv.y) ||
            !std::isfinite(gv.z)) {
            std::snprintf(out.error, sizeof(out.error), "non-finite vertex");
            return out;
        }

        out.vertices.push_back(gv);
    }

    const std::uint8_t *index_payload =
        object.data() + index_chunk.payload_offset;
    const std::size_t index_marker =
        SkipEightByteMarker(index_payload, index_chunk.size);

    const std::uint8_t *index_data =
        index_payload + index_marker;
    const std::size_t index_size =
        index_chunk.size - index_marker;

    const std::uint64_t required_index_bytes =
        total_tris * 3u * sizeof(std::uint16_t);

    if (required_index_bytes > index_size ||
        total_tris > 100000u) {
        std::snprintf(out.error, sizeof(out.error), "index buffer size invalid");
        return out;
    }

    out.indices.reserve(
        static_cast<std::size_t>(total_tris) * 3u);

    const std::size_t index_count =
        static_cast<std::size_t>(total_tris) * 3u;

    for (std::size_t i = 0; i < index_count; ++i) {
        const std::size_t at = i * sizeof(std::uint16_t);

        if (at + sizeof(std::uint16_t) > index_size) {
            std::snprintf(out.error, sizeof(out.error), "index read overrun");
            return out;
        }

        std::uint16_t absolute_index = 0;
        std::memcpy(
            &absolute_index,
            index_data + at,
            sizeof(absolute_index));

        if (absolute_index >= total_vertices) {
            std::snprintf(
                out.error,
                sizeof(out.error),
                "absolute index %u >= vertex count %u",
                static_cast<unsigned>(absolute_index),
                static_cast<unsigned>(total_vertices));
            return out;
        }

        out.indices.push_back(absolute_index);
    }

    if (out.indices.size() !=
        static_cast<std::size_t>(out.num_tris) * 3u) {
        std::snprintf(out.error, sizeof(out.error), "triangle assembly mismatch");
        return out;
    }

    out.valid = true;
    return out;
}

void WriteGeometryLog(
    const char *path,
    const GeometryIndex &index,
    std::size_t object_index,
    const GeometryMesh &mesh) {

    FILE *f =
        std::fopen("ux0:data/nfsmw/logs/geometry-m9.log", "w");

    if (!f)
        return;

    std::fprintf(f, "NFSMW Vita Port - Milestone 9 geometry\n");
    std::fprintf(f, "File: %s\n", path);
    std::fprintf(f, "Index: %s\n", index.valid ? "VALID" : "INVALID");
    std::fprintf(f, "File size: %u\n", index.file_size);
    std::fprintf(f, "Source: %s\n", index.source_name);
    std::fprintf(f, "Objects: %u\n", index.object_count);

    if (!index.valid) {
        std::fprintf(f, "Index error: %s\n", index.error);
        std::fclose(f);
        return;
    }

    std::fprintf(f, "Selected object: %u\n",
                 static_cast<unsigned>(object_index));

    if (object_index < index.displayed_objects) {
        const GeometryDirectoryEntry &e =
            index.objects[object_index];

        std::fprintf(f,
                     "Directory: hash=0x%08X off=0x%08X size=%u\n",
                     e.name_hash,
                     e.file_offset,
                     e.stored_size);
    }

    std::fprintf(f, "Mesh: %s\n", mesh.valid ? "VALID" : "INVALID");
    std::fprintf(f, "Name: %s\n", mesh.name);
    std::fprintf(f, "Hash: 0x%08X\n", mesh.name_hash);
    std::fprintf(f, "Flags: 0x%08X\n", mesh.flags);
    std::fprintf(f, "Triangles: %u\n", mesh.num_tris);
    std::fprintf(f, "Vertices: %u\n",
                 static_cast<unsigned>(mesh.vertices.size()));
    std::fprintf(f, "Groups: %u/%u supported\n",
                 mesh.supported_group_count,
                 mesh.group_count);
    std::fprintf(f,
                 "BBox: (%f,%f,%f) -> (%f,%f,%f)\n",
                 mesh.bbox_min[0],
                 mesh.bbox_min[1],
                 mesh.bbox_min[2],
                 mesh.bbox_max[0],
                 mesh.bbox_max[1],
                 mesh.bbox_max[2]);

    std::fprintf(
        f,
        "Vertex layout: start=%u stride=%u\n",
        mesh.vertex_data_offset,
        mesh.vertex_stride);

    for (std::size_t i = 0;
         i < mesh.group_count &&
         i < GeometryMesh::kMaxGroups;
         ++i) {
        std::fprintf(
            f,
            "Group %u: texref=0x%08X vertices=%u tris=%u\n",
            static_cast<unsigned>(i),
            mesh.group_texture_ref[i],
            mesh.group_vertex_count[i],
            mesh.group_tri_count[i]);
    }

    if (!mesh.valid)
        std::fprintf(f, "Mesh error: %s\n", mesh.error);

    std::fclose(f);
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct GeometryDirectoryEntry {
    std::uint32_t name_hash = 0;
    std::uint32_t file_offset = 0;
    std::uint32_t stored_size = 0;
};

struct GeometryIndex {
    static constexpr std::size_t kMaxObjects = 512;

    bool found = false;
    bool valid = false;
    std::uint32_t file_size = 0;
    std::uint32_t object_count = 0;
    std::size_t displayed_objects = 0;
    char source_name[64]{};
    char error[96]{};
    GeometryDirectoryEntry objects[kMaxObjects]{};
};

struct GeometryVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct GeometryMesh {
    static constexpr std::size_t kMaxGroups = 256;

    bool valid = false;
    std::uint32_t name_hash = 0;
    std::uint32_t flags = 0;
    std::uint32_t num_tris = 0;
    std::uint32_t group_count = 0;
    std::uint32_t supported_group_count = 0;
    float bbox_min[3]{};
    float bbox_max[3]{};
    char name[64]{};
    char error[128]{};

    std::uint32_t group_fvf[kMaxGroups]{};
    std::uint32_t group_vertex_count[kMaxGroups]{};
    std::uint32_t group_tri_count[kMaxGroups]{};
    std::uint32_t group_stride[kMaxGroups]{};
    bool group_stride_inferred[kMaxGroups]{};

    std::vector<GeometryVertex> vertices;
    std::vector<std::uint32_t> indices;
};

GeometryIndex ReadGeometryIndex(const char *path);

GeometryMesh LoadGeometryObject(
    const char *path,
    const GeometryIndex &index,
    std::size_t object_index);

void WriteGeometryLog(
    const char *path,
    const GeometryIndex &index,
    std::size_t object_index,
    const GeometryMesh &mesh);

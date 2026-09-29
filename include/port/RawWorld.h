#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct M11Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct M11Triangle {
    M11Vec3 p[3];
    std::uint32_t color = 0xffffffffu;
};

struct M11World {
    bool valid = false;
    std::string error;
    M11Vec3 origin{};
    std::vector<M11Triangle> world;
    std::vector<M11Triangle> road;
    std::size_t object_count = 0;
    std::size_t instance_count = 0;
};

M11World LoadM11RockportSection(const char *stream_path, int section_id);
bool M11Ground(const M11World &world, float x, float y, float previous_z, float *z);

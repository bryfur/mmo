#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace mmo::engine::render::lighting {

// Std140-compatible POD layouts mirrored exactly in clusters.hlsli.
// Sizes are static_asserted in light_cluster.cpp.

struct PointLight {
    glm::vec3 position;
    float radius;
    glm::vec3 color;
    float intensity;
};

struct SpotLight {
    glm::vec3 position;
    float radius;
    glm::vec3 direction;
    float inner_cos;
    glm::vec3 color;
    float outer_cos;
    float intensity;
    float _pad[3];
};

// 0 = point, 1 = spot. payload_index is the index into the corresponding
// per-type array uploaded to the GPU.
struct LightHeader {
    uint32_t type;
    uint32_t payload_index;
};

inline constexpr uint32_t LIGHT_TYPE_POINT = 0;
inline constexpr uint32_t LIGHT_TYPE_SPOT = 1;

// Cluster grid configuration. Must match clusters.hlsli.
inline constexpr uint32_t CLUSTER_DIM_X = 16;
inline constexpr uint32_t CLUSTER_DIM_Y = 9;
inline constexpr uint32_t CLUSTER_DIM_Z = 24;
inline constexpr uint32_t CLUSTER_COUNT = CLUSTER_DIM_X * CLUSTER_DIM_Y * CLUSTER_DIM_Z;

// v1 limits.
inline constexpr uint32_t MAX_LIGHTS = 256;
inline constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 128;

} // namespace mmo::engine::render::lighting

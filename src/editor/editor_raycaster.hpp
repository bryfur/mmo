#pragma once

#include <glm/glm.hpp>
#include <functional>
#include "engine/scene/camera_state.hpp"

namespace mmo::editor {

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction; // normalized
};

// Callback that returns terrain height at (x, z)
using TerrainHeightFn = std::function<float(float, float)>;

class EditorRaycaster {
public:
    // Unproject screen pixel into a world-space ray
    Ray screen_to_ray(float mx, float my, int screen_w, int screen_h,
                      const engine::scene::CameraState& camera) const;

    // March ray against heightmap. Returns true if hit, sets hit_pos.
    bool intersect_terrain(const Ray& ray, glm::vec3& hit_pos,
                           const TerrainHeightFn& get_height,
                           float step_size = 25.0f,
                           float max_distance = 10000.0f) const;

    // Ray-AABB intersection (slab method). Returns distance along ray, or -1 on miss.
    static float intersect_aabb(const Ray& ray,
                                const glm::vec3& aabb_min,
                                const glm::vec3& aabb_max);
};

} // namespace mmo::editor

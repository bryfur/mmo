#pragma once

#include <glm/glm.hpp>
#include <array>

namespace mmo::engine::scene {

struct Plane {
    glm::vec3 normal;
    float distance;

    void normalize() {
        float len = glm::length(normal);
        if (len > 0.0f) {
            normal /= len;
            distance /= len;
        }
    }

    float distance_to_point(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

class Frustum {
public:
    Frustum() = default;

    void extract_from_matrix(const glm::mat4& vp);

    bool intersects_aabb(const glm::vec3& min_point, const glm::vec3& max_point) const;
    bool intersects_sphere(const glm::vec3& center, float radius) const;

private:
    std::array<Plane, 6> planes_;
};

} // namespace mmo::engine::scene

#include "frustum.hpp"
#include <glm/gtc/matrix_access.hpp>

namespace mmo::engine::scene {

void Frustum::extract_from_matrix(const glm::mat4& vp) {
    // Gribb-Hartmann method using transposed VP for clean row access
    // Reference: https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
    // GLM is column-major, so transpose to get rows as vec4s
    glm::mat4 vpt = glm::transpose(vp);

    auto set_plane = [](Plane& p, const glm::vec4& v) {
        p.normal = glm::vec3(v);
        p.distance = v.w;
        p.normalize();
    };

    set_plane(planes_[0], vpt[3] + vpt[0]); // Left
    set_plane(planes_[1], vpt[3] - vpt[0]); // Right
    set_plane(planes_[2], vpt[3] + vpt[1]); // Bottom
    set_plane(planes_[3], vpt[3] - vpt[1]); // Top
    set_plane(planes_[4], vpt[2]);           // Near (depth [0,1] with GLM_FORCE_DEPTH_ZERO_TO_ONE)
    set_plane(planes_[5], vpt[3] - vpt[2]); // Far
}

bool Frustum::intersects_aabb(const glm::vec3& min_point, const glm::vec3& max_point) const {
    for (const auto& plane : planes_) {
        // Find the positive vertex (farthest along plane normal)
        glm::vec3 p;
        p.x = (plane.normal.x >= 0.0f) ? max_point.x : min_point.x;
        p.y = (plane.normal.y >= 0.0f) ? max_point.y : min_point.y;
        p.z = (plane.normal.z >= 0.0f) ? max_point.z : min_point.z;

        if (plane.distance_to_point(p) < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects_sphere(const glm::vec3& center, float radius) const {
    for (const auto& plane : planes_) {
        if (plane.distance_to_point(center) < -radius) {
            return false;
        }
    }
    return true;
}

} // namespace mmo::engine::scene

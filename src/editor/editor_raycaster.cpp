#include "editor_raycaster.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace mmo::editor {

Ray EditorRaycaster::screen_to_ray(float mx, float my, int screen_w, int screen_h,
                                    const engine::scene::CameraState& camera) const {
    // Convert screen coords to NDC [-1, 1]
    float ndc_x = (2.0f * mx / screen_w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * my / screen_h); // flip Y (screen Y is down)

    glm::mat4 inv_vp = glm::inverse(camera.view_projection);

    // GLM_FORCE_DEPTH_ZERO_TO_ONE is defined (Vulkan clip space), so near=0, far=1
    glm::vec4 near_ndc(ndc_x, ndc_y, 0.0f, 1.0f);
    glm::vec4 far_ndc(ndc_x, ndc_y, 1.0f, 1.0f);

    glm::vec4 near_world = inv_vp * near_ndc;
    near_world /= near_world.w;
    glm::vec4 far_world = inv_vp * far_ndc;
    far_world /= far_world.w;

    Ray ray;
    ray.origin = glm::vec3(near_world);
    ray.direction = glm::normalize(glm::vec3(far_world) - glm::vec3(near_world));
    return ray;
}

bool EditorRaycaster::intersect_terrain(const Ray& ray, glm::vec3& hit_pos,
                                         const TerrainHeightFn& get_height,
                                         float step_size, float max_distance) const {
    // Coarse ray march
    for (float t = 0.0f; t < max_distance; t += step_size) {
        glm::vec3 p = ray.origin + ray.direction * t;
        float terrain_h = get_height(p.x, p.z);
        if (p.y < terrain_h) {
            // Binary refinement between t-step_size and t
            float lo = std::max(0.0f, t - step_size);
            float hi = t;
            for (int i = 0; i < 12; ++i) {
                float mid = (lo + hi) * 0.5f;
                glm::vec3 mp = ray.origin + ray.direction * mid;
                float mh = get_height(mp.x, mp.z);
                if (mp.y < mh)
                    hi = mid;
                else
                    lo = mid;
            }
            hit_pos = ray.origin + ray.direction * ((lo + hi) * 0.5f);
            hit_pos.y = get_height(hit_pos.x, hit_pos.z);
            return true;
        }
    }
    return false;
}

float EditorRaycaster::intersect_aabb(const Ray& ray,
                                       const glm::vec3& aabb_min,
                                       const glm::vec3& aabb_max) {
    float tmin = -1e30f;
    float tmax = 1e30f;

    for (int i = 0; i < 3; ++i) {
        float inv_d = 1.0f / ray.direction[i];
        float t0 = (aabb_min[i] - ray.origin[i]) * inv_d;
        float t1 = (aabb_max[i] - ray.origin[i]) * inv_d;
        if (inv_d < 0.0f) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmax < tmin) return -1.0f;
    }

    if (tmin < 0.0f) return -1.0f; // behind camera
    return tmin;
}

} // namespace mmo::editor

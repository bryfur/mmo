#include "world_projection.hpp"

#include <algorithm>

namespace mmo::client::hud {

std::optional<ScreenPoint> world_to_screen(const glm::mat4& view_projection, const glm::vec3& world_pos,
                                           float screen_width, float screen_height) {
    const glm::vec4 clip = view_projection * glm::vec4(world_pos, 1.0f);
    if (clip.w <= 0.0f) {
        return std::nullopt;
    }

    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    return ScreenPoint{
        (ndc_x * 0.5f + 0.5f) * screen_width,
        (1.0f - (ndc_y * 0.5f + 0.5f)) * screen_height,
    };
}

float distance_scale(float distance, float falloff, float min_scale) {
    if (falloff <= 0.0f) {
        return min_scale;
    }
    return std::max(min_scale, 1.0f - distance / falloff);
}

} // namespace mmo::client::hud

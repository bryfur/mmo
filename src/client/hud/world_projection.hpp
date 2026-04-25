#pragma once

#include <glm/glm.hpp>
#include <optional>

namespace mmo::client::hud {

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// Project a world-space point through `view_projection` into screen pixels.
// Returns nullopt for points behind the camera (w <= 0). Pure math, no
// rendering or globals — testable directly with synthetic matrices.
std::optional<ScreenPoint> world_to_screen(const glm::mat4& view_projection, const glm::vec3& world_pos,
                                           float screen_width, float screen_height);

// Distance-based scaling factor for floating world widgets (quest markers,
// nameplates). Linearly shrinks with distance, clamped to a sensible floor.
float distance_scale(float distance, float falloff = 1000.0f, float min_scale = 0.6f);

} // namespace mmo::client::hud

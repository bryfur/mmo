#pragma once

#include <glm/glm.hpp>

namespace mmo::engine {

struct Model;  // forward declare

// Build a standard model-to-world transform matrix.
// Applies: translate(position) * rotate(yaw) * scale(uniform) * center(-pivot)
// where pivot.x/z = bounding box center, pivot.y = min_y.
// Optional attack_tilt rotates around X axis (lean forward) after yaw.
glm::mat4 build_model_transform(const Model& model, const glm::vec3& position,
                                float yaw_radians, float target_size,
                                float attack_tilt = 0.0f);

} // namespace mmo::engine

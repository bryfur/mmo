#pragma once

#include <glm/glm.hpp>

namespace mmo::engine::scene {

/**
 * Camera state passed from game logic to renderer each frame.
 */
struct CameraState {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 view_projection;
    glm::vec3 position;
};

} // namespace mmo::engine::scene

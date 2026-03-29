#pragma once

#include <glm/glm.hpp>

namespace mmo::engine {

// ============================================================================
// Fog settings
// ============================================================================

namespace fog {
    // Default fog (entities, terrain)
    constexpr glm::vec3 COLOR = {0.35f, 0.45f, 0.6f};
    constexpr float START = 3000.0f;
    constexpr float END = 12000.0f;

    // Distant objects (mountains)
    constexpr glm::vec3 DISTANT_COLOR = {0.55f, 0.55f, 0.6f};
    constexpr float DISTANT_START = 8000.0f;
    constexpr float DISTANT_END = 32000.0f;
}

// ============================================================================
// Lighting defaults
// ============================================================================

namespace lighting {
    constexpr glm::vec3 LIGHT_COLOR = {1.0f, 0.95f, 0.9f};
    constexpr glm::vec3 AMBIENT_COLOR = {0.4f, 0.4f, 0.5f};
    constexpr glm::vec3 AMBIENT_COLOR_NO_FOG = {0.5f, 0.5f, 0.55f};
}

} // namespace mmo::engine

#pragma once

#include <string>

namespace mmo::engine {

/**
 * Engine-side effect instance for rendering.
 * Game code converts from its own effect types to this.
 */
struct EffectInstance {
    std::string effect_type;    // "melee_swing", "projectile", "orbit", "arrow"
    float x = 0.0f;
    float y = 0.0f;
    float direction_x = 0.0f;
    float direction_y = 1.0f;
    float timer = 0.0f;
    float duration = 0.3f;
    float range = 1.0f;
};

} // namespace mmo::engine

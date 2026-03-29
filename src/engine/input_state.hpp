#pragma once

#include <cmath>

namespace mmo::engine {

/**
 * Engine-level input state. Contains movement and action state
 * without any network serialization or game-specific protocol knowledge.
 */
struct InputState {
    // Legacy boolean movement flags
    bool move_up = false;
    bool move_down = false;
    bool move_left = false;
    bool move_right = false;

    // Continuous movement direction (normalized, camera-relative)
    float move_dir_x = 0.0f;
    float move_dir_y = 0.0f;

    // Action state
    bool attacking = false;
    bool sprinting = false;
    float attack_dir_x = 0.0f;
    float attack_dir_y = 1.0f;
};

/**
 * Clamp a movement direction vector so its magnitude never exceeds 1.0.
 * Sub-unit magnitudes (analog stick partial tilt) are preserved.
 * Returns the resulting magnitude.
 */
inline float normalize_move_dir(float& x, float& y) {
    float len = std::sqrt(x * x + y * y);
    if (len > 1.0f) {
        x /= len;
        y /= len;
        return 1.0f;
    }
    return len;
}

} // namespace mmo::engine

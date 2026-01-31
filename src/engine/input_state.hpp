#pragma once

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

    // Attack state
    bool attacking = false;
    float attack_dir_x = 0.0f;
    float attack_dir_y = 1.0f;
};

} // namespace mmo::engine

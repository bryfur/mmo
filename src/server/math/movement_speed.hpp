#pragma once

// MovementSpeed: pure speed math, satisfies the Formula<T> contract.
// movement_system calls compute(Inputs{...}) once per player per tick.

#include "server/math/formula_concept.hpp"

#include <cmath>

namespace mmo::server::math {

class MovementSpeed {
public:
    /// Multiplier applied when sprint is active.
    static constexpr float SPRINT_MULTIPLIER = 1.6f;

    /// All modifiers affecting a character's effective speed.
    struct Inputs {
        float class_base_speed = 0.0f;     // from ClassConfig.speed
        float equipment_bonus = 0.0f;      // additive from Equipment.speed_bonus
        float talent_speed_mult = 1.0f;    // TalentPassiveState.speed_mult
        float buff_speed_mult = 1.0f;      // BuffState.get_speed_multiplier
        bool  is_sprinting = false;
        bool  is_rooted = false;
        bool  is_stunned = false;
    };

    using Output = float;

    /// Final XZ movement speed. Formula:
    /// (class_base + equipment) * talent * buff * sprint. Rooted / stunned
    /// always returns 0.
    [[nodiscard]] static constexpr Output compute(const Inputs& in) noexcept {
        if (in.is_rooted || in.is_stunned) return 0.0f;
        float s = in.class_base_speed + in.equipment_bonus;
        s *= in.talent_speed_mult;
        s *= in.buff_speed_mult;
        if (in.is_sprinting) s *= SPRINT_MULTIPLIER;
        return (s < 0.0f) ? 0.0f : s;
    }

    /// 2D velocity result from (speed, direction).
    struct Velocity { float vx = 0.0f, vz = 0.0f; };

    /// Turn a speed + direction into a per-tick velocity. Direction is
    /// normalized internally so diagonals don't boost.
    [[nodiscard]] static Velocity compute_velocity(
        float speed, float dir_x, float dir_z) noexcept {
        float len = std::sqrt(dir_x * dir_x + dir_z * dir_z);
        if (len < 1e-4f) return {0.0f, 0.0f};
        return { dir_x / len * speed, dir_z / len * speed };
    }
};
static_assert(Formula<MovementSpeed>);

} // namespace mmo::server::math

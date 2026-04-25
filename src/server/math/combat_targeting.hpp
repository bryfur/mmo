#pragma once

// CombatTargeting: pure "is this target reachable?" math used by
// skill_system, combat_system, and ai_system. Decomposed into range + cone
// predicates so each branch is testable without an ECS.

#include <cmath>

namespace mmo::server::math {

class CombatTargeting {
public:
    /// Squared XZ distance (Y is vertical in this game; targeting ignores it).
    [[nodiscard]] static constexpr float distance_sq_xz(
        float ax, float az, float bx, float bz) noexcept {
        float dx = bx - ax;
        float dz = bz - az;
        return dx * dx + dz * dz;
    }

    /// XZ distance. Prefer distance_sq_xz for range checks (no sqrt).
    [[nodiscard]] static float distance_xz(float ax, float az, float bx, float bz) noexcept {
        return std::sqrt(distance_sq_xz(ax, az, bx, bz));
    }

    /// Is `target` within `range` on the XZ plane?
    /// Zero or negative range always returns false.
    [[nodiscard]] static constexpr bool in_range(
        float ax, float az, float bx, float bz, float range) noexcept {
        if (range <= 0.0f) return false;
        return distance_sq_xz(ax, az, bx, bz) <= range * range;
    }

    /// Is `target` within the attacker's attack cone?
    ///
    /// `cone_angle` is the half-width of the cone in radians.
    /// `(dir_x, dir_z)` is a unit vector pointing where the attacker is
    /// facing. Cone_angle <= 0 means "no filter" (returns true).
    /// Cone_angle >= pi is omnidirectional.
    /// A zero-distance target always returns true (no angle to compare).
    [[nodiscard]] static bool in_cone(
        float ax, float az, float bx, float bz,
        float dir_x, float dir_z, float cone_angle) noexcept {
        if (cone_angle <= 0.0f) return true;
        if (cone_angle >= 3.14159265f) return true;  // covers exact-behind case
        float dx = bx - ax;
        float dz = bz - az;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d < 1e-4f) return true;
        float nx = dx / d;
        float nz = dz / d;
        float dot = nx * dir_x + nz * dir_z;
        return dot >= std::cos(cone_angle);
    }

    /// Combined range + cone predicate used by most skill/attack loops.
    [[nodiscard]] static bool can_target(
        float ax, float az, float bx, float bz,
        float dir_x, float dir_z,
        float range, float cone_angle) noexcept {
        return in_range(ax, az, bx, bz, range)
            && in_cone(ax, az, bx, bz, dir_x, dir_z, cone_angle);
    }

    /// Given a skill's cone_angle + spread_angle + piercing flag, produce
    /// the effective cone half-width to use in the targeting loop. Matches
    /// the rules documented in skill_system.cpp:
    ///   - explicit cone_angle wins
    ///   - else spread_angle / 2 (multi-shot semantics)
    ///   - else piercing fallback (narrow forward line ~20 deg)
    ///   - else 0 (no filter)
    [[nodiscard]] static constexpr float effective_cone_half_angle(
        float cone_angle, float spread_angle, bool piercing) noexcept {
        if (cone_angle > 0.0f) return cone_angle;
        if (spread_angle > 0.0f) return spread_angle * 0.5f;
        if (piercing) return 0.35f;
        return 0.0f;
    }
};

} // namespace mmo::server::math

#pragma once

// XPMath: pure leveling/XP calculations. Not a Rule - no yes/no decision,
// just arithmetic helpers. Grouped under a class so call sites read
// XPMath::level_floor(...) rather than free-function-soup.

#include <vector>

namespace mmo::server::math {

class XPMath {
public:
    /// The XP thresholds for each level. curve[0] is the floor for level 1.
    using Curve = std::vector<int>;

    /// XP threshold a character of `level` has already passed (their
    /// current level's floor). Clamped for out-of-range levels.
    [[nodiscard]] static int level_floor(const Curve& curve, int level) noexcept {
        if (curve.empty()) return 0;
        int idx = level - 1;
        if (idx < 0) idx = 0;
        if (idx >= static_cast<int>(curve.size())) idx = static_cast<int>(curve.size()) - 1;
        return curve[idx];
    }

    /// XP required to reach level+1. At the cap returns curve.back().
    [[nodiscard]] static int xp_needed_for_next_level(const Curve& curve, int level) noexcept {
        if (curve.empty()) return 0;
        if (level <= 0) return curve.front();
        int idx = level;
        if (idx >= static_cast<int>(curve.size())) return curve.back();
        return curve[idx];
    }

    /// Given current XP and level, compute the new level (handles multi-
    /// level jumps). Clamped to max_level.
    [[nodiscard]] static int compute_new_level(const Curve& curve, int current_level,
                                                 int current_xp, int max_level) noexcept {
        int lvl = current_level < 1 ? 1 : current_level;
        while (lvl < max_level) {
            int next = xp_needed_for_next_level(curve, lvl);
            if (current_xp < next) break;
            ++lvl;
        }
        return lvl;
    }

    /// XP lost on death. Matches existing server behavior: lose `loss_pct`%
    /// of the distance between current level's floor and next threshold.
    /// XP never drops below the current level's floor (no de-leveling).
    /// Returns the XP actually deducted.
    [[nodiscard]] static int compute_death_xp_loss(const Curve& curve, int current_level,
                                                     int current_xp, float loss_pct) noexcept {
        int floor = level_floor(curve, current_level);
        int next = xp_needed_for_next_level(curve, current_level);
        int bracket = next - floor;
        if (bracket < 0) bracket = 0;
        int loss = static_cast<int>(bracket * loss_pct / 100.0f);
        int new_xp = current_xp - loss;
        if (new_xp < floor) new_xp = floor;
        return current_xp - new_xp;
    }

    /// Kill-XP modifier based on level difference between attacker and
    /// target. Clamped monotonically so XP farming low-level mobs trivially
    /// becomes diminishing returns.
    [[nodiscard]] static constexpr float level_diff_xp_modifier(
        int player_level, int target_level) noexcept {
        int diff = target_level - player_level;
        if (diff <= -6) return 0.10f;
        if (diff <= -4) return 0.25f;
        if (diff <= -2) return 0.60f;
        if (diff <=  2) return 1.00f;
        if (diff <=  4) return 1.50f;
        if (diff <=  6) return 2.00f;
        return 2.50f;
    }
};

} // namespace mmo::server::math

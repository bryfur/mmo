#pragma once

// Shared types used by quest-objective rules (KillObjective,
// LocationObjective). Contains the static definition of an objective
// (type, target, location, radius) and the runtime progress state.
//
// The helpers `kill_target_matches` and `in_objective_area` are utilities
// both KillObjective and LocationObjective use.

#include <string_view>

namespace mmo::server::rules {

/// Static view of a single objective definition. Mirrors the shape of
/// QuestObjectiveConfig in game_config.hpp but holds non-owning views so
/// tests don't need to load JSON.
struct ObjectiveDef {
    std::string_view type;   // "kill" / "kill_in_area" / "visit" / "explore"
    std::string_view target; // monster id / location name / "monster" / "npc_enemy"
    int required = 1;
    float location_x = 0.0f;
    float location_z = 0.0f;
    float radius = 0.0f;
};

struct ObjectiveState {
    int current = 0;
    bool complete = false;
};

/// Does the killed monster match the objective's target string? Handles
/// the "monster" / "npc_enemy" wildcards used by kill_in_area quests.
[[nodiscard]] constexpr bool kill_target_matches(std::string_view objective_target,
                                                 std::string_view killed_monster_id) noexcept {
    return objective_target == killed_monster_id || objective_target == "monster" || objective_target == "npc_enemy";
}

/// Is (x, z) inside the objective's circle? Boundary is inclusive.
[[nodiscard]] constexpr bool in_objective_area(const ObjectiveDef& obj, float x, float z) noexcept {
    float dx = x - obj.location_x;
    float dz = z - obj.location_z;
    return dx * dx + dz * dz <= obj.radius * obj.radius;
}

} // namespace mmo::server::rules

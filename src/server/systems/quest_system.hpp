#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

/// Describes a quest objective change (for sending to clients)
struct QuestChange {
    std::string quest_id;
    uint8_t objective_index = 0;
    int current = 0;
    int required = 0;
    bool objective_complete = false;
    bool quest_complete = false;  // all objectives done
    std::string quest_name;       // filled when quest_complete
};

/// Check if a player can accept a specific quest
bool can_accept_quest(entt::registry& registry, entt::entity player, const QuestConfig& quest);

/// Accept a quest for a player
bool accept_quest(entt::registry& registry, entt::entity player, const std::string& quest_id, const GameConfig& config);

/// Update kill objectives for a player when they kill a monster. The
/// monster's world position (kill_x, kill_z) is used to match kill_in_area
/// objectives. Returns list of changes for network notification.
std::vector<QuestChange> on_monster_killed(entt::registry& registry, entt::entity player,
                                            const std::string& monster_type_id, const GameConfig& config,
                                            float kill_x = 0.0f, float kill_z = 0.0f);

/// Update visit objectives for a player based on their position.
/// Returns list of changes for network notification.
std::vector<QuestChange> update_visit_objectives(entt::registry& registry, entt::entity player, const GameConfig& config);

/// Turn in a completed quest and give rewards
bool turn_in_quest(entt::registry& registry, entt::entity player, const std::string& quest_id, const GameConfig& config);

/// Get available quests from an NPC type for a specific player
std::vector<const QuestConfig*> get_available_quests(entt::registry& registry, entt::entity player,
                                                      const std::string& npc_type, const GameConfig& config);

/// Update all quest-related checks for all players (called each tick).
/// Returns list of changes for network notification.
std::vector<std::pair<entt::entity, QuestChange>> update_quests(entt::registry& registry, float dt, const GameConfig& config);

} // namespace mmo::server::systems

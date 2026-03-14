#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

/// Award XP to a player for killing a monster. Handles level-up and stat scaling.
void award_kill_xp(entt::registry& registry, entt::entity player, entt::entity monster, const GameConfig& config);

/// Apply death XP penalty to a player
void apply_death_penalty(entt::registry& registry, entt::entity player, const GameConfig& config);

/// Update mana regeneration for all players
void update_mana_regen(entt::registry& registry, float dt);

/// Check if a player should level up and apply stat gains
/// Returns true if player leveled up
bool check_level_up(entt::registry& registry, entt::entity player, const GameConfig& config);

/// Get the class name string for a player class index (0=warrior, 1=mage, 2=paladin, 3=archer)
const char* class_name_for_index(int index);

} // namespace mmo::server::systems

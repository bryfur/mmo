#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

/// Update talent passive effects each tick:
/// - Low HP regen, stationary heal, periodic shields
/// - Fury state, panic freeze triggers
/// - Passive damage aura, nearby enemy debuffs
/// - Combo stack decay, cheat death cooldowns
void update_talent_passives(entt::registry& registry, float dt, const GameConfig& config);

} // namespace mmo::server::systems

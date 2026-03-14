#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>
#include <vector>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

/// Describes a combat hit that occurred during the tick
struct CombatHit {
    entt::entity attacker = entt::null;
    entt::entity target = entt::null;
    float damage = 0;
    bool target_died = false;
};

/// Update combat and return all hits that occurred this tick
std::vector<CombatHit> update_combat(entt::registry& registry, float dt, const GameConfig& config);

} // namespace mmo::server::systems

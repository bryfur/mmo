#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

void update_combat(entt::registry& registry, float dt, const GameConfig& config);

} // namespace mmo::server::systems

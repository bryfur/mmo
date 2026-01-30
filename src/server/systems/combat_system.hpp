#pragma once

#include "common/ecs/components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>

namespace mmo::systems {

void update_combat(entt::registry& registry, float dt, const GameConfig& config);

} // namespace mmo::systems

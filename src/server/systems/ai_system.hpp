#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>
#include <glm/vec2.hpp>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

void update_ai(entt::registry& registry, float dt, const GameConfig& config, const glm::vec2& town_center);

} // namespace mmo::server::systems

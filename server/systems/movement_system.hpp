#pragma once

#include "common/ecs/components.hpp"
#include <entt/entt.hpp>

namespace mmo::systems {

void update_movement(entt::registry& registry, float dt);

} // namespace mmo::systems

#pragma once

#include "common/ecs/components.hpp"
#include <entt/entt.hpp>

namespace mmo::systems {

void update_combat(entt::registry& registry, float dt);

} // namespace mmo::systems

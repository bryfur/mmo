#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

/// Update all status effects - tick durations, apply DoT/HoT, remove expired
void update_buffs(entt::registry& registry, float dt);

/// Apply a status effect to a target entity
void apply_effect(entt::registry& registry, entt::entity target, ecs::StatusEffect effect);

/// Remove all effects of a type from target
void remove_effect(entt::registry& registry, entt::entity target, ecs::StatusEffect::Type type);

/// Clear all effects (on death, etc.)
void clear_effects(entt::registry& registry, entt::entity target);

} // namespace mmo::server::systems

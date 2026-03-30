#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include "server/world.hpp"
#include "server/spatial_grid.hpp"
#include "server/systems/zone_system.hpp"
#include "server/systems/physics_system.hpp"
#include <entt/entt.hpp>
#include <random>
#include <vector>
#include <functional>
#include <glm/vec2.hpp>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

/// Handle dead monster cleanup: award XP/loot to nearest player, apply kill-based
/// talent effects, process quest objectives, generate gameplay events, then respawn.
void handle_monster_deaths(
    entt::registry& registry,
    const GameConfig& config,
    std::vector<World::GameplayEvent>& pending_events,
    SpatialGrid& spatial_grid,
    ZoneSystem& zone_system,
    PhysicsSystem& physics,
    std::mt19937& rng,
    std::function<float(float, float)> get_terrain_height);

/// Handle player deaths: apply death penalty, reset health/mana, teleport to town.
void handle_player_deaths(
    entt::registry& registry,
    const GameConfig& config,
    const glm::vec2& town_center,
    PhysicsSystem& physics,
    std::mt19937& rng,
    std::function<float(float, float)> get_terrain_height);

} // namespace mmo::server::systems

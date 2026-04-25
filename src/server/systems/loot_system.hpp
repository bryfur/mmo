#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>
#include <random>
#include <string>
#include <vector>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

struct LootResult {
    int gold = 0;
    std::vector<std::pair<std::string, int>> items; // item_id, count
};

/// Roll loot for a killed monster based on its type and the loot tables.
/// The process-global RNG overload is intended for production use; tests
/// should prefer the seeded overload for deterministic outcomes.
LootResult roll_loot(const std::string& monster_type_id, const GameConfig& config);
LootResult roll_loot(const std::string& monster_type_id, const GameConfig& config, std::mt19937& rng);

/// Give loot to a player (gold + items). Returns items that overflowed the
/// inventory so callers can surface an "Inventory Full" notification
/// instead of silently losing items.
std::vector<std::pair<std::string, int>> give_loot(entt::registry& registry, entt::entity player,
                                                   const LootResult& loot);

/// Equip an item from inventory
bool equip_item(entt::registry& registry, entt::entity player, const std::string& item_id, const GameConfig& config);

/// Unequip an item back to inventory
bool unequip_item(entt::registry& registry, entt::entity player, const std::string& slot, const GameConfig& config);

/// Use a consumable item from inventory
bool use_consumable(entt::registry& registry, entt::entity player, const std::string& item_id,
                    const GameConfig& config);

/// Recalculate equipment bonuses
void recalc_equipment(entt::registry& registry, entt::entity player, const GameConfig& config);

} // namespace mmo::server::systems

#include "loot_system.hpp"
#include "buff_system.hpp"
#include "leveling_system.hpp"
#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <algorithm>
#include <random>
#include <string>

namespace mmo::server::systems {

namespace {

std::mt19937& rng() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

} // anonymous namespace

LootResult roll_loot(const std::string& monster_type_id, const GameConfig& config) {
    LootResult result;

    const LootTableConfig* table = config.find_loot_table(monster_type_id);
    if (!table) return result;

    // Roll gold
    if (table->gold_max > table->gold_min) {
        std::uniform_int_distribution<int> gold_dist(table->gold_min, table->gold_max);
        result.gold = gold_dist(rng());
    } else {
        result.gold = table->gold_min;
    }

    // Roll each drop
    std::uniform_real_distribution<float> chance_dist(0.0f, 1.0f);
    for (const auto& drop : table->drops) {
        if (chance_dist(rng()) < drop.chance) {
            int count = drop.count_min;
            if (drop.count_max > drop.count_min) {
                std::uniform_int_distribution<int> count_dist(drop.count_min, drop.count_max);
                count = count_dist(rng());
            }
            result.items.emplace_back(drop.item_id, count);
        }
    }

    return result;
}

void give_loot(entt::registry& registry, entt::entity player, const LootResult& loot) {
    // Add gold
    if (auto* level = registry.try_get<ecs::PlayerLevel>(player)) {
        level->gold += loot.gold;
    }

    // Add items to inventory
    if (auto* inv = registry.try_get<ecs::Inventory>(player)) {
        for (const auto& [item_id, count] : loot.items) {
            inv->add_item(item_id, count);
        }
    }
}

bool equip_item(entt::registry& registry, entt::entity player, const std::string& item_id, const GameConfig& config) {
    auto* inv = registry.try_get<ecs::Inventory>(player);
    auto* equip = registry.try_get<ecs::Equipment>(player);
    auto* level = registry.try_get<ecs::PlayerLevel>(player);
    if (!inv || !equip || !level) return false;

    // Check item exists in inventory
    if (inv->count_item(item_id) <= 0) return false;

    // Find item config
    const ItemConfig* item = config.find_item(item_id);
    if (!item) return false;

    // Check level requirement
    if (level->level < item->level_req) return false;

    // Fix #5: Check class restriction
    if (!item->classes.empty()) {
        const auto* info = registry.try_get<ecs::EntityInfo>(player);
        if (!info) return false;
        const char* player_class_name = class_name_for_index(info->player_class);
        bool class_allowed = false;
        for (const auto& cls : item->classes) {
            if (cls == player_class_name) {
                class_allowed = true;
                break;
            }
        }
        if (!class_allowed) return false;
    }

    // Determine slot
    if (item->type == "weapon") {
        // Fix #7: Check if old weapon can be returned to inventory before swapping
        if (!equip->weapon_id.empty()) {
            if (inv->used_slots >= ecs::Inventory::MAX_SLOTS) {
                return false; // Inventory full, can't swap
            }
            inv->add_item(equip->weapon_id, 1, 1);
        }
        equip->weapon_id = item_id;
    } else if (item->type == "armor") {
        // Fix #7: Check if old armor can be returned to inventory before swapping
        if (!equip->armor_id.empty()) {
            if (inv->used_slots >= ecs::Inventory::MAX_SLOTS) {
                return false; // Inventory full, can't swap
            }
            inv->add_item(equip->armor_id, 1, 1);
        }
        equip->armor_id = item_id;
    } else {
        return false; // Not equippable
    }

    // Remove from inventory
    inv->remove_item(item_id);

    // Recalculate bonuses
    recalc_equipment(registry, player, config);
    return true;
}

bool unequip_item(entt::registry& registry, entt::entity player, const std::string& slot, const GameConfig& config) {
    auto* inv = registry.try_get<ecs::Inventory>(player);
    auto* equip = registry.try_get<ecs::Equipment>(player);
    if (!inv || !equip) return false;

    if (slot == "weapon") {
        if (equip->weapon_id.empty()) return false;
        if (!inv->add_item(equip->weapon_id)) return false; // Inventory full
        equip->weapon_id.clear();
    } else if (slot == "armor") {
        if (equip->armor_id.empty()) return false;
        if (!inv->add_item(equip->armor_id)) return false; // Inventory full
        equip->armor_id.clear();
    } else {
        return false; // Unknown slot
    }

    recalc_equipment(registry, player, config);
    return true;
}

bool use_consumable(entt::registry& registry, entt::entity player, const std::string& item_id, const GameConfig& config) {
    auto* inv = registry.try_get<ecs::Inventory>(player);
    if (!inv) return false;

    // Check item exists in inventory
    if (inv->count_item(item_id) <= 0) return false;

    // Find item config
    const ItemConfig* item = config.find_item(item_id);
    if (!item) return false;

    // Must be consumable
    if (item->type != "consumable") return false;

    // Apply heal
    if (item->stats.heal_amount > 0.0f) {
        if (auto* health = registry.try_get<ecs::Health>(player)) {
            health->current = std::min(health->current + item->stats.heal_amount, health->max);
        }
    }

    // Fix #6: Apply buff effects from consumable potions
    if (item->stats.buff_duration > 0.0f && item->stats.buff_multiplier != 0.0f) {
        ecs::StatusEffect buff;
        buff.duration = item->stats.buff_duration;
        buff.tick_timer = 0.0f;
        buff.tick_interval = 0.0f;
        buff.value = item->stats.buff_multiplier;
        buff.source_id = 0;

        // Determine buff type from item subtype
        if (item->subtype == "speed_potion" || item->subtype == "speed") {
            buff.type = ecs::StatusEffect::Type::SpeedBoost;
        } else if (item->subtype == "damage_potion" || item->subtype == "damage") {
            buff.type = ecs::StatusEffect::Type::DamageBoost;
        } else if (item->subtype == "defense_potion" || item->subtype == "defense") {
            buff.type = ecs::StatusEffect::Type::DefenseBoost;
        } else {
            // Default to damage boost for unrecognized buff potions
            buff.type = ecs::StatusEffect::Type::DamageBoost;
        }

        apply_effect(registry, player, buff);
    }

    // Remove 1 from inventory
    inv->remove_item(item_id, 1);
    return true;
}

void recalc_equipment(entt::registry& registry, entt::entity player, const GameConfig& config) {
    auto* equip = registry.try_get<ecs::Equipment>(player);
    if (!equip) return;

    // Reset bonuses
    equip->damage_bonus = 0.0f;
    equip->health_bonus = 0.0f;
    equip->speed_bonus = 0.0f;
    equip->defense = 0.0f;

    // Add weapon stats
    if (!equip->weapon_id.empty()) {
        const ItemConfig* weapon = config.find_item(equip->weapon_id);
        if (weapon) {
            equip->damage_bonus += weapon->stats.damage_bonus;
            equip->health_bonus += weapon->stats.health_bonus;
            equip->speed_bonus += weapon->stats.speed_bonus;
            equip->defense += weapon->stats.defense;
        }
    }

    // Add armor stats
    if (!equip->armor_id.empty()) {
        const ItemConfig* armor = config.find_item(equip->armor_id);
        if (armor) {
            equip->damage_bonus += armor->stats.damage_bonus;
            equip->health_bonus += armor->stats.health_bonus;
            equip->speed_bonus += armor->stats.speed_bonus;
            equip->defense += armor->stats.defense;
        }
    }
}

} // namespace mmo::server::systems

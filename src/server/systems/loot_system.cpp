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

// Seeded overload - tests inject a std::mt19937 to get deterministic rolls.
LootResult roll_loot(const std::string& monster_type_id, const GameConfig& config,
                     std::mt19937& rng_) {
    LootResult result;

    const LootTableConfig* table = config.find_loot_table(monster_type_id);
    if (!table) return result;

    // Roll gold
    if (table->gold_max > table->gold_min) {
        std::uniform_int_distribution<int> gold_dist(table->gold_min, table->gold_max);
        result.gold = gold_dist(rng_);
    } else {
        result.gold = table->gold_min;
    }

    // Roll each drop
    std::uniform_real_distribution<float> chance_dist(0.0f, 1.0f);
    for (const auto& drop : table->drops) {
        if (chance_dist(rng_) < drop.chance) {
            int count = drop.count_min;
            if (drop.count_max > drop.count_min) {
                std::uniform_int_distribution<int> count_dist(drop.count_min, drop.count_max);
                count = count_dist(rng_);
            }
            result.items.emplace_back(drop.item_id, count);
        }
    }

    return result;
}

LootResult roll_loot(const std::string& monster_type_id, const GameConfig& config) {
    return roll_loot(monster_type_id, config, rng());
}

std::vector<std::pair<std::string, int>>
give_loot(entt::registry& registry, entt::entity player, const LootResult& loot) {
    std::vector<std::pair<std::string, int>> overflow;

    // Add gold (no inventory cap on gold in this design).
    if (auto* level = registry.try_get<ecs::PlayerLevel>(player)) {
        level->gold += loot.gold;
    }

    // Add items to inventory; surface any that didn't fit so the caller can
    // show a "Inventory Full" notification or spawn a ground drop.
    if (auto* inv = registry.try_get<ecs::Inventory>(player)) {
        for (const auto& [item_id, count] : loot.items) {
            int before = inv->count_item(item_id);
            if (!inv->add_item(item_id, count)) {
                int after = inv->count_item(item_id);
                int added = after - before;
                int lost = count - added;
                if (lost > 0) overflow.emplace_back(item_id, lost);
            }
        }
    }
    return overflow;
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

    // Remove the new item from inventory FIRST so the slot it occupied is
    // available for the old item being unequipped. This avoids the edge
    // case where inventory is exactly full and the swap would otherwise
    // fail despite there being a natural swap slot.
    inv->remove_item(item_id);

    // Determine slot and put old item back (if any).
    if (item->type == "weapon") {
        if (!equip->weapon_id.empty()) {
            if (!inv->add_item(equip->weapon_id, 1, 1)) {
                // Truly no room (old item needed a slot and inventory was
                // already full with other items). Restore state and fail.
                inv->add_item(item_id, 1, item->stack_size);
                return false;
            }
        }
        equip->weapon_id = item_id;
    } else if (item->type == "armor") {
        if (!equip->armor_id.empty()) {
            if (!inv->add_item(equip->armor_id, 1, 1)) {
                inv->add_item(item_id, 1, item->stack_size);
                return false;
            }
        }
        equip->armor_id = item_id;
    } else {
        // Not equippable: restore and fail.
        inv->add_item(item_id, 1, item->stack_size);
        return false;
    }

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

    // Per-item cooldown (potion spam guard). Heal potions share a 6s
    // cooldown, mana potions 6s, stat elixirs 60s, scrolls 15s.
    auto& cd = registry.get_or_emplace<ecs::ConsumableCooldowns>(player);
    if (cd.remaining(item_id) > 0.0f) return false;
    float cooldown_seconds = 6.0f;
    if (item->subtype == "elixir" || item->stats.buff_duration > 20.0f) cooldown_seconds = 60.0f;
    else if (item->subtype == "scroll") cooldown_seconds = 15.0f;
    cd.set(item_id, cooldown_seconds);

    // Apply heal
    if (item->stats.heal_amount > 0.0f) {
        if (auto* health = registry.try_get<ecs::Health>(player)) {
            health->current = std::min(health->current + item->stats.heal_amount, health->max);
        }
    }

    // Apply buff effects from consumable potions
    if (item->stats.buff_duration > 0.0f && item->stats.buff_multiplier != 0.0f) {
        // Determine buff type from item subtype
        ecs::StatusEffect::Type buff_type = ecs::StatusEffect::Type::DamageBoost;
        if (item->subtype == "speed_potion" || item->subtype == "speed") {
            buff_type = ecs::StatusEffect::Type::SpeedBoost;
        } else if (item->subtype == "defense_potion" || item->subtype == "defense") {
            buff_type = ecs::StatusEffect::Type::DefenseBoost;
        }

        apply_effect(registry, player,
            ecs::make_status_effect(buff_type, item->stats.buff_duration, item->stats.buff_multiplier));
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

    // Apply the health_bonus delta to the player's current Health.max so
    // stat bonuses are actually reflected in combat numbers. Track what we
    // applied so future recalc calls only shift by the difference.
    if (auto* hp = registry.try_get<ecs::Health>(player)) {
        float delta = equip->health_bonus - equip->applied_health_bonus;
        if (delta != 0.0f) {
            hp->max = std::max(1.0f, hp->max + delta);
            // Don't over-heal: clamp current, but extend if we grew.
            if (hp->current > hp->max) hp->current = hp->max;
            else if (delta > 0.0f) hp->current = std::min(hp->max, hp->current + delta);
        }
        equip->applied_health_bonus = equip->health_bonus;
    }
}

} // namespace mmo::server::systems

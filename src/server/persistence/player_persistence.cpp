#include "player_persistence.hpp"
#include "../ecs/game_components.hpp"

#include <nlohmann/json.hpp>

namespace mmo::server::persistence {

using nlohmann::json;

namespace {

std::string serialize_objectives(const std::vector<ecs::QuestObjectiveProgress>& objs) {
    json arr = json::array();
    for (const auto& o : objs) {
        arr.push_back({
            {"type", o.type},
            {"target", o.target},
            {"current", o.current},
            {"required", o.required},
            {"complete", o.complete},
        });
    }
    return arr.dump();
}

std::vector<ecs::QuestObjectiveProgress> parse_objectives(const std::string& s) {
    std::vector<ecs::QuestObjectiveProgress> out;
    if (s.empty()) {
        return out;
    }
    try {
        json arr = json::parse(s);
        if (!arr.is_array()) {
            return out;
        }
        for (const auto& o : arr) {
            ecs::QuestObjectiveProgress p;
            p.type = o.value("type", std::string{});
            p.target = o.value("target", std::string{});
            p.current = o.value("current", 0);
            p.required = o.value("required", 0);
            p.complete = o.value("complete", false);
            out.push_back(std::move(p));
        }
    } catch (...) { // NOLINT(bugprone-empty-catch): malformed JSON — return what we have, server treats as fresh quest
    }
    return out;
}

} // namespace

PlayerSnapshot snapshot_from_entity(const entt::registry& registry, entt::entity entity, const std::string& name) {
    PlayerSnapshot snap;
    snap.name = name;

    if (const auto* info = registry.try_get<ecs::EntityInfo>(entity)) {
        snap.player_class = info->player_class;
    }
    if (const auto* tx = registry.try_get<ecs::Transform>(entity)) {
        snap.pos_x = tx->x;
        snap.pos_y = tx->y;
        snap.pos_z = tx->z;
        snap.rotation = tx->rotation;
    }
    if (const auto* hp = registry.try_get<ecs::Health>(entity)) {
        snap.health = hp->current;
        snap.max_health = hp->max;
        snap.was_dead = !hp->is_alive();
    }
    if (const auto* lvl = registry.try_get<ecs::PlayerLevel>(entity)) {
        snap.level = lvl->level;
        snap.xp = lvl->xp;
        snap.gold = lvl->gold;
        snap.mana = lvl->mana;
        snap.max_mana = lvl->max_mana;
        snap.mana_regen = lvl->mana_regen;
    }
    if (const auto* inv = registry.try_get<ecs::Inventory>(entity)) {
        snap.inventory.reserve(inv->used_slots);
        for (int i = 0; i < inv->used_slots; ++i) {
            snap.inventory.push_back({inv->slots[i].item_id, inv->slots[i].count});
        }
    }
    if (const auto* eq = registry.try_get<ecs::Equipment>(entity)) {
        snap.equipped_weapon = eq->weapon_id;
        snap.equipped_armor = eq->armor_id;
    }
    if (const auto* ts = registry.try_get<ecs::TalentState>(entity)) {
        snap.talent_points = ts->talent_points;
        snap.unlocked_talents = ts->unlocked_talents;
    }
    if (const auto* qs = registry.try_get<ecs::QuestState>(entity)) {
        snap.completed_quests = qs->completed_quests;
        snap.active_quests.reserve(qs->active_quests.size());
        for (const auto& aq : qs->active_quests) {
            snap.active_quests.push_back({
                aq.quest_id,
                serialize_objectives(aq.objectives),
            });
        }
    }
    return snap;
}

void apply_snapshot_to_entity(entt::registry& registry, entt::entity entity, const PlayerSnapshot& snap) {
    if (auto* tx = registry.try_get<ecs::Transform>(entity)) {
        tx->x = snap.pos_x;
        tx->y = snap.pos_y;
        tx->z = snap.pos_z;
        tx->rotation = snap.rotation;
    }
    if (auto* hp = registry.try_get<ecs::Health>(entity)) {
        hp->max = snap.max_health;
        // If they logged out dead, revive at full HP — Server forces a
        // teleport to town so the player doesn't return to where they died.
        // Otherwise clamp to [1, max] so corrupt rows can't spawn a corpse.
        if (snap.was_dead) {
            hp->current = snap.max_health;
        } else {
            hp->current = std::clamp(snap.health, 1.0f, snap.max_health);
        }
    }
    if (auto* lvl = registry.try_get<ecs::PlayerLevel>(entity)) {
        lvl->level = snap.level;
        lvl->xp = snap.xp;
        lvl->gold = snap.gold;
        lvl->mana = snap.mana;
        lvl->max_mana = snap.max_mana;
        lvl->mana_regen = snap.mana_regen;
    }
    if (auto* inv = registry.try_get<ecs::Inventory>(entity)) {
        inv->used_slots = 0;
        for (auto& slot : inv->slots) slot = {};
        for (size_t i = 0; i < snap.inventory.size() && i < ecs::Inventory::MAX_SLOTS; ++i) {
            if (snap.inventory[i].item_id.empty()) {
                continue;
            }
            inv->slots[inv->used_slots].item_id = snap.inventory[i].item_id;
            inv->slots[inv->used_slots].count = snap.inventory[i].count;
            ++inv->used_slots;
        }
    }
    if (auto* eq = registry.try_get<ecs::Equipment>(entity)) {
        eq->weapon_id = snap.equipped_weapon;
        eq->armor_id = snap.equipped_armor;
    }
    if (auto* ts = registry.try_get<ecs::TalentState>(entity)) {
        ts->talent_points = snap.talent_points;
        ts->unlocked_talents = snap.unlocked_talents;
    }
    if (auto* qs = registry.try_get<ecs::QuestState>(entity)) {
        qs->completed_quests = snap.completed_quests;
        qs->active_quests.clear();
        qs->active_quests.reserve(snap.active_quests.size());
        for (const auto& aq : snap.active_quests) {
            ecs::ActiveQuest active;
            active.quest_id = aq.quest_id;
            active.objectives = parse_objectives(aq.objectives_json);
            active.all_complete = !active.objectives.empty();
            for (const auto& o : active.objectives) {
                if (!o.complete) {
                    active.all_complete = false;
                    break;
                }
            }
            qs->active_quests.push_back(std::move(active));
        }
    }
}

} // namespace mmo::server::persistence

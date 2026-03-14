#include "quest_system.hpp"
#include <cmath>

namespace mmo::server::systems {

namespace {

float distance_xz(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1;
    float dz = z2 - z1;
    return std::sqrt(dx * dx + dz * dz);
}

void check_all_complete(ecs::ActiveQuest& quest) {
    quest.all_complete = true;
    for (const auto& obj : quest.objectives) {
        if (!obj.complete) {
            quest.all_complete = false;
            return;
        }
    }
}

} // anonymous namespace

bool can_accept_quest(entt::registry& registry, entt::entity player, const QuestConfig& quest) {
    // Must have QuestState
    auto* quest_state = registry.try_get<ecs::QuestState>(player);
    if (!quest_state) return false;

    // Check level requirement
    auto* player_level = registry.try_get<ecs::PlayerLevel>(player);
    if (!player_level) return false;
    if (player_level->level < quest.min_level) return false;

    // Check prerequisite
    if (!quest.prerequisite_quest.empty()) {
        if (!quest_state->has_completed(quest.prerequisite_quest)) return false;
    }

    // Check not already active
    if (quest_state->has_active(quest.id)) return false;

    // If not repeatable, check not already completed
    if (!quest.repeatable && quest_state->has_completed(quest.id)) return false;

    return true;
}

bool accept_quest(entt::registry& registry, entt::entity player, const std::string& quest_id, const GameConfig& config) {
    const QuestConfig* quest = config.find_quest(quest_id);
    if (!quest) return false;

    if (!can_accept_quest(registry, player, *quest)) return false;

    auto& quest_state = registry.get<ecs::QuestState>(player);

    ecs::ActiveQuest active;
    active.quest_id = quest_id;
    active.objectives.reserve(quest->objectives.size());

    for (const auto& obj_config : quest->objectives) {
        ecs::QuestObjectiveProgress progress;
        progress.type = obj_config.type;
        progress.target = obj_config.target;
        progress.required = obj_config.count;
        progress.current = 0;
        progress.complete = false;
        active.objectives.push_back(progress);
    }

    active.all_complete = false;
    quest_state.active_quests.push_back(std::move(active));
    return true;
}

std::vector<QuestChange> on_monster_killed(entt::registry& registry, entt::entity player,
                                            const std::string& monster_type_id, const GameConfig& config) {
    std::vector<QuestChange> changes;
    auto* quest_state = registry.try_get<ecs::QuestState>(player);
    if (!quest_state) return changes;

    for (auto& active : quest_state->active_quests) {
        if (active.all_complete) continue;

        bool was_complete_before = active.all_complete;
        for (size_t idx = 0; idx < active.objectives.size(); ++idx) {
            auto& obj = active.objectives[idx];
            if (obj.complete) continue;
            if (obj.type != "kill") continue;

            // Match specific monster type, or "monster" for any kill
            if (obj.target == monster_type_id || obj.target == "monster") {
                ++obj.current;
                if (obj.current >= obj.required) {
                    obj.complete = true;
                }

                QuestChange change;
                change.quest_id = active.quest_id;
                change.objective_index = static_cast<uint8_t>(idx);
                change.current = obj.current;
                change.required = obj.required;
                change.objective_complete = obj.complete;
                changes.push_back(change);
            }
        }

        check_all_complete(active);

        // If quest just became complete, mark it
        if (active.all_complete && !was_complete_before) {
            const QuestConfig* qcfg = config.find_quest(active.quest_id);
            QuestChange complete_change;
            complete_change.quest_id = active.quest_id;
            complete_change.quest_complete = true;
            complete_change.quest_name = qcfg ? qcfg->name : active.quest_id;
            changes.push_back(complete_change);
        }
    }

    return changes;
}

std::vector<QuestChange> update_visit_objectives(entt::registry& registry, entt::entity player, const GameConfig& config) {
    std::vector<QuestChange> changes;
    auto* quest_state = registry.try_get<ecs::QuestState>(player);
    if (!quest_state) return changes;

    auto* transform = registry.try_get<ecs::Transform>(player);
    if (!transform) return changes;

    for (auto& active : quest_state->active_quests) {
        if (active.all_complete) continue;

        const QuestConfig* quest = config.find_quest(active.quest_id);
        if (!quest) continue;

        bool was_complete_before = active.all_complete;
        for (size_t i = 0; i < active.objectives.size() && i < quest->objectives.size(); ++i) {
            auto& obj = active.objectives[i];
            if (obj.complete) continue;
            if (obj.type != "visit") continue;

            const auto& obj_config = quest->objectives[i];
            float dist = distance_xz(transform->x, transform->z, obj_config.location_x, obj_config.location_z);
            if (dist < obj_config.radius) {
                obj.current = obj.required;
                obj.complete = true;

                QuestChange change;
                change.quest_id = active.quest_id;
                change.objective_index = static_cast<uint8_t>(i);
                change.current = obj.current;
                change.required = obj.required;
                change.objective_complete = true;
                changes.push_back(change);
            }
        }

        check_all_complete(active);

        if (active.all_complete && !was_complete_before) {
            QuestChange complete_change;
            complete_change.quest_id = active.quest_id;
            complete_change.quest_complete = true;
            complete_change.quest_name = quest->name;
            changes.push_back(complete_change);
        }
    }

    return changes;
}

bool turn_in_quest(entt::registry& registry, entt::entity player, const std::string& quest_id, const GameConfig& config) {
    auto* quest_state = registry.try_get<ecs::QuestState>(player);
    if (!quest_state) return false;

    ecs::ActiveQuest* active = quest_state->get_active(quest_id);
    if (!active) return false;
    if (!active->all_complete) return false;

    const QuestConfig* quest = config.find_quest(quest_id);
    if (!quest) return false;

    // Award rewards
    auto* player_level = registry.try_get<ecs::PlayerLevel>(player);
    if (player_level) {
        player_level->xp += quest->rewards.xp;
        player_level->gold += quest->rewards.gold;
    }

    // Award item reward
    if (!quest->rewards.item_reward.empty()) {
        auto* inventory = registry.try_get<ecs::Inventory>(player);
        if (inventory) {
            inventory->add_item(quest->rewards.item_reward);
        }
    }

    // Move to completed
    quest_state->completed_quests.push_back(quest_id);

    // Remove from active
    auto& actives = quest_state->active_quests;
    for (auto it = actives.begin(); it != actives.end(); ++it) {
        if (it->quest_id == quest_id) {
            actives.erase(it);
            break;
        }
    }

    return true;
}

std::vector<const QuestConfig*> get_available_quests(entt::registry& registry, entt::entity player,
                                                      const std::string& npc_type, const GameConfig& config) {
    auto npc_quests = config.quests_for_npc(npc_type);

    std::vector<const QuestConfig*> available;
    for (const auto* quest : npc_quests) {
        if (can_accept_quest(registry, player, *quest)) {
            available.push_back(quest);
        }
    }
    return available;
}

std::vector<std::pair<entt::entity, QuestChange>> update_quests(entt::registry& registry, float /*dt*/, const GameConfig& config) {
    std::vector<std::pair<entt::entity, QuestChange>> all_changes;
    auto view = registry.view<ecs::PlayerTag, ecs::QuestState, ecs::Transform>();
    for (auto entity : view) {
        auto changes = update_visit_objectives(registry, entity, config);
        for (auto& change : changes) {
            all_changes.emplace_back(entity, std::move(change));
        }
    }
    return all_changes;
}

} // namespace mmo::server::systems

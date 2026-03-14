#include "skill_system.hpp"
#include "leveling_system.hpp"
#include "protocol/protocol.hpp"
#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <cmath>
#include <algorithm>
#include <string>

namespace mmo::server::systems {

using namespace mmo::protocol;

namespace {

float distance_xz(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1;
    float dz = z2 - z1;
    return std::sqrt(dx * dx + dz * dz);
}

} // anonymous namespace

bool use_skill(entt::registry& registry, entt::entity player, const std::string& skill_id,
               float dir_x, float dir_z, const GameConfig& config) {
    // Find skill config
    const SkillConfig* skill = config.find_skill(skill_id);
    if (!skill) return false;

    // Verify player has required components
    if (!registry.all_of<ecs::SkillState, ecs::PlayerLevel, ecs::Combat, ecs::Health, ecs::EntityInfo, ecs::Transform>(player))
        return false;

    auto& skill_state = registry.get<ecs::SkillState>(player);
    auto& player_level = registry.get<ecs::PlayerLevel>(player);
    auto& combat = registry.get<ecs::Combat>(player);
    auto& health = registry.get<ecs::Health>(player);
    const auto& info = registry.get<ecs::EntityInfo>(player);
    const auto& transform = registry.get<ecs::Transform>(player);

    if (!health.is_alive()) return false;

    // Check skill class matches player class
    const char* player_class_name = class_name_for_index(info.player_class);
    if (skill->class_name != player_class_name) return false;

    // Check player level
    if (player_level.level < skill->unlock_level) return false;

    // Check cooldown
    if (skill_state.get_cooldown(skill_id) > 0.0f) return false;

    // Check mana
    if (player_level.mana < skill->mana_cost) return false;

    // Deduct mana
    player_level.mana -= skill->mana_cost;

    // Set cooldown
    skill_state.set_cooldown(skill_id, skill->cooldown);

    // Apply skill effects

    // Damage effect: find nearby enemies in range and deal damage
    if (skill->damage_multiplier > 0.0f) {
        float total_damage = combat.damage * skill->damage_multiplier;

        auto view = registry.view<ecs::Transform, ecs::Health, ecs::EntityInfo>();
        for (auto entity : view) {
            if (entity == player) continue;

            const auto& target_info = view.get<ecs::EntityInfo>(entity);
            if (target_info.type != EntityType::NPC) continue;

            auto& target_health = view.get<ecs::Health>(entity);
            if (!target_health.is_alive()) continue;

            const auto& target_transform = view.get<ecs::Transform>(entity);
            float dist = distance_xz(transform.x, transform.z,
                                      target_transform.x, target_transform.z);
            if (dist > skill->range) continue;

            // Check cone direction if cone_angle is set
            if (skill->cone_angle > 0.0f && dist > 0.001f) {
                float dx = target_transform.x - transform.x;
                float dz = target_transform.z - transform.z;
                float nx = dx / dist;
                float nz = dz / dist;
                float dot = nx * dir_x + nz * dir_z;
                if (dot < std::cos(skill->cone_angle)) continue;
            }

            target_health.current = std::max(0.0f, target_health.current - total_damage);
        }
    }

    // Heal effect
    if (skill->heal_percent > 0.0f) {
        float heal_amount = health.max * skill->heal_percent;
        health.current = std::min(health.max, health.current + heal_amount);
    }

    // Damage reduction buff (logged for now, future buff system will handle)
    // if (skill->damage_reduction > 0.0f) { /* TODO: buff system */ }

    // Set attacking flag for visual feedback
    combat.is_attacking = true;

    return true;
}

void update_skill_cooldowns(entt::registry& registry, float dt) {
    auto view = registry.view<ecs::SkillState>();
    for (auto entity : view) {
        auto& skill_state = view.get<ecs::SkillState>(entity);
        skill_state.update(dt);
    }
}

std::vector<const SkillConfig*> get_unlocked_skills(entt::registry& registry, entt::entity player,
                                                     const GameConfig& config) {
    std::vector<const SkillConfig*> result;

    if (!registry.all_of<ecs::EntityInfo, ecs::PlayerLevel>(player))
        return result;

    const auto& info = registry.get<ecs::EntityInfo>(player);
    const auto& player_level = registry.get<ecs::PlayerLevel>(player);

    const char* player_class_name = class_name_for_index(info.player_class);
    auto class_skills = config.skills_for_class(player_class_name);

    for (const auto* skill : class_skills) {
        if (skill->unlock_level <= player_level.level) {
            result.push_back(skill);
        }
    }

    return result;
}

bool unlock_talent(entt::registry& registry, entt::entity player, const std::string& talent_id,
                   const GameConfig& config) {
    if (!registry.all_of<ecs::TalentState, ecs::EntityInfo>(player))
        return false;

    auto& talent_state = registry.get<ecs::TalentState>(player);

    // Check talent points available
    if (talent_state.talent_points <= 0) return false;

    // Find talent config
    const TalentConfig* talent = config.find_talent(talent_id);
    if (!talent) return false;

    // Check talent's class matches player class
    const auto& info = registry.get<ecs::EntityInfo>(player);
    const char* player_class_name = class_name_for_index(info.player_class);

    // Find which talent tree this talent belongs to
    bool class_matches = false;
    for (const auto& tree : config.talent_trees()) {
        if (tree.class_name != player_class_name) continue;
        for (const auto& branch : tree.branches) {
            for (const auto& t : branch.talents) {
                if (t.id == talent_id) {
                    class_matches = true;
                    break;
                }
            }
            if (class_matches) break;
        }
        if (class_matches) break;
    }
    if (!class_matches) return false;

    // Check prerequisite
    if (!talent->prerequisite.empty() && !talent_state.has_talent(talent->prerequisite))
        return false;

    // Check not already unlocked
    if (talent_state.has_talent(talent_id)) return false;

    // Deduct talent point and unlock
    talent_state.talent_points--;
    talent_state.unlocked_talents.push_back(talent_id);

    // Apply talent effects
    apply_talent_effects(registry, player, config);

    return true;
}

TalentEffect compute_talent_effects(entt::registry& registry, entt::entity player, const GameConfig& config) {
    TalentEffect aggregate;
    // Defaults are already 1.0 for multipliers, 0.0 for additive from struct definition

    if (!registry.all_of<ecs::TalentState>(player))
        return aggregate;

    const auto& talent_state = registry.get<ecs::TalentState>(player);

    for (const auto& talent_id : talent_state.unlocked_talents) {
        const TalentConfig* talent = config.find_talent(talent_id);
        if (!talent) continue;

        const auto& e = talent->effect;
        aggregate.damage_mult *= e.damage_mult;
        aggregate.speed_mult *= e.speed_mult;
        aggregate.health_mult *= e.health_mult;
        aggregate.defense_mult *= e.defense_mult;
        aggregate.mana_mult *= e.mana_mult;
        aggregate.cooldown_mult *= e.cooldown_mult;

        // Additive effects
        aggregate.crit_chance += e.crit_chance;
        aggregate.kill_heal_pct += e.kill_heal_pct;
    }

    return aggregate;
}

void apply_talent_effects(entt::registry& registry, entt::entity player, const GameConfig& config) {
    // Compute aggregate effects - the combat system reads these values
    // when applying damage/stats. For now, ensure TalentState is up to date.
    // The actual stat modifications happen through the combat system
    // checking talent effects at damage/heal time.
    (void)compute_talent_effects(registry, player, config);
}

} // namespace mmo::server::systems

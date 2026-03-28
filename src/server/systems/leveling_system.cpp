#include "leveling_system.hpp"
#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <algorithm>
#include <cstdio>

namespace mmo::server::systems {

namespace {

float level_diff_xp_modifier(int player_level, int monster_level) {
    int diff = monster_level - player_level;

    if (diff == 0) return 1.0f;

    if (diff < 0) {
        // Monster is lower level
        switch (diff) {
            case -1: return 0.9f;
            case -2: return 0.75f;
            case -3: return 0.5f;
            case -4: return 0.25f;
            default: return 0.1f;
        }
    } else {
        // Monster is higher level
        switch (diff) {
            case 1: return 1.1f;
            case 2: return 1.2f;
            case 3: return 1.3f;
            case 4: return 1.4f;
            default: return 1.5f;
        }
    }
}

} // anonymous namespace

void award_kill_xp(entt::registry& registry, entt::entity player, entt::entity monster, const GameConfig& config) {
    if (!registry.valid(player) || !registry.valid(monster)) return;

    auto* monster_type = registry.try_get<ecs::MonsterTypeId>(monster);
    auto* player_level = registry.try_get<ecs::PlayerLevel>(player);
    if (!monster_type || !player_level) return;

    float modifier = level_diff_xp_modifier(player_level->level, monster_type->level);
    int xp_gained = static_cast<int>(monster_type->xp_reward * modifier);
    if (xp_gained < 1) xp_gained = 1;

    player_level->xp += xp_gained;
    player_level->gold += monster_type->gold_reward;

    check_level_up(registry, player, config);
}

bool check_level_up(entt::registry& registry, entt::entity player, const GameConfig& config) {
    auto* player_level = registry.try_get<ecs::PlayerLevel>(player);
    if (!player_level) return false;

    const auto& leveling = config.leveling();
    const auto& xp_curve = leveling.xp_curve;
    bool leveled = false;

    while (player_level->level < leveling.max_level &&
           player_level->level < static_cast<int>(xp_curve.size()) &&
           player_level->xp >= xp_curve[player_level->level]) {

        player_level->level++;
        leveled = true;

        // Apply class growth stats
        auto* info = registry.try_get<ecs::EntityInfo>(player);
        int class_index = info ? static_cast<int>(info->player_class) : 0;

        if (class_index >= 0 && class_index < static_cast<int>(leveling.class_growth.size())) {
            const auto& growth = leveling.class_growth[class_index];

            auto* health = registry.try_get<ecs::Health>(player);
            if (health) {
                health->max += growth.health;
                health->current = health->max; // Heal to full on level up
            }

            auto* combat = registry.try_get<ecs::Combat>(player);
            if (combat) {
                combat->damage += growth.damage;
                combat->attack_range += growth.attack_range;
                combat->attack_cooldown = std::max(0.3f, combat->attack_cooldown - growth.attack_cooldown_reduction);
            }
        }

        // Award talent point (respecting config thresholds)
        auto* talents = registry.try_get<ecs::TalentState>(player);
        const auto& talent_cfg = config.talent_config();
        if (talents && player_level->level >= talent_cfg.first_talent_point_level
            && talents->talent_points < talent_cfg.max_talent_points) {
            talents->talent_points++;
        }

        // Get player name for log message
        auto* name = registry.try_get<ecs::Name>(player);
        const char* player_name = name ? name->value.c_str() : "Unknown";
        std::printf("[Leveling] %s reached level %d!\n", player_name, player_level->level);
    }

    return leveled;
}

void apply_death_penalty(entt::registry& registry, entt::entity player, const GameConfig& config) {
    auto* player_level = registry.try_get<ecs::PlayerLevel>(player);
    if (!player_level) return;

    const auto& leveling = config.leveling();
    const auto& xp_curve = leveling.xp_curve;

    // Calculate XP needed for current level
    int current_level_threshold = 0;
    if (player_level->level > 0 && player_level->level < static_cast<int>(xp_curve.size())) {
        current_level_threshold = xp_curve[player_level->level];
    }

    // Previous level threshold (floor)
    int prev_threshold = 0;
    if (player_level->level - 1 > 0 && player_level->level - 1 < static_cast<int>(xp_curve.size())) {
        prev_threshold = xp_curve[player_level->level - 1];
    }

    int xp_for_level = current_level_threshold - prev_threshold;
    int xp_loss = static_cast<int>(xp_for_level * leveling.death_xp_loss_percent / 100.0f);

    player_level->xp -= xp_loss;

    // Never go below the threshold for the current level (no deleveling)
    if (player_level->xp < prev_threshold) {
        player_level->xp = prev_threshold;
    }
}

void update_mana_regen(entt::registry& registry, float dt) {
    auto view = registry.view<ecs::PlayerLevel>();
    for (auto entity : view) {
        auto& pl = view.get<ecs::PlayerLevel>(entity);
        pl.mana = std::min(pl.mana + pl.mana_regen * dt, pl.max_mana);
    }
}

const char* class_name_for_index(int index) {
    switch (index) {
        case 0: return "warrior";
        case 1: return "mage";
        case 2: return "paladin";
        case 3: return "archer";
        default: return "warrior";
    }
}

} // namespace mmo::server::systems

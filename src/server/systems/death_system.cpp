#include "death_system.hpp"
#include "buff_system.hpp"
#include "leveling_system.hpp"
#include "loot_system.hpp"
#include "quest_system.hpp"
#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <cmath>
#include <algorithm>

namespace mmo::server::systems {

void handle_monster_deaths(
    entt::registry& registry,
    const GameConfig& config,
    std::vector<World::GameplayEvent>& pending_events,
    SpatialGrid& spatial_grid,
    ZoneSystem& zone_system,
    PhysicsSystem& physics,
    std::mt19937& rng,
    std::function<float(float, float)> get_terrain_height)
{
    using GameplayEvent = World::GameplayEvent;

    auto dead_npcs = registry.view<ecs::NPCTag, ecs::Health, ecs::MonsterTypeId>();
    for (auto entity : dead_npcs) {
        auto& health = dead_npcs.get<ecs::Health>(entity);
        if (health.is_alive()) continue;

        auto& monster_info = dead_npcs.get<ecs::MonsterTypeId>(entity);

        // Award XP/loot to nearest player who killed it
        auto& transform = registry.get<ecs::Transform>(entity);
        float best_dist = 1000.0f;
        entt::entity killer = entt::null;
        auto players = registry.view<ecs::PlayerTag, ecs::Transform, ecs::Health>();
        for (auto player : players) {
            auto& ph = players.get<ecs::Health>(player);
            if (!ph.is_alive()) continue;
            auto& pt = players.get<ecs::Transform>(player);
            float dx = pt.x - transform.x;
            float dz = pt.z - transform.z;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (dist < best_dist) {
                best_dist = dist;
                killer = player;
            }
        }

        if (killer != entt::null) {
            // Snapshot XP/level before awarding so we can compute the gain
            auto& killer_level_pre = registry.get<ecs::PlayerLevel>(killer);
            int xp_before = killer_level_pre.xp;
            int level_before = killer_level_pre.level;

            award_kill_xp(registry, killer, entity, config);

            // Apply kill-based talent effects
            if (registry.all_of<ecs::TalentPassiveState>(killer)) {
                auto& tp = registry.get<ecs::TalentPassiveState>(killer);
                const auto& dead_transform = registry.get<ecs::Transform>(entity);
                float dead_max_hp = registry.all_of<ecs::Health>(entity)
                    ? registry.get<ecs::Health>(entity).max : 0.0f;

                // Kill heal (Bloodlust, etc.)
                if (tp.kill_heal_pct > 0.0f && registry.all_of<ecs::Health>(killer)) {
                    auto& kh = registry.get<ecs::Health>(killer);
                    float heal = kh.max * tp.kill_heal_pct;
                    if (tp.healing_received_mult != 1.0f) {
                        heal *= tp.healing_received_mult;
                    }
                    kh.current = std::min(kh.max, kh.current + heal);
                }

                // Kill explosion (Inferno)
                if (tp.kill_explosion_pct > 0.0f && tp.kill_explosion_radius > 0.0f &&
                    dead_max_hp > 0.0f) {
                    float explosion_dmg = dead_max_hp * tp.kill_explosion_pct;
                    auto npc_explode_view = registry.view<ecs::NPCTag, ecs::Health, ecs::Transform>();
                    for (auto npc : npc_explode_view) {
                        if (npc == entity) continue;
                        auto& nh = npc_explode_view.get<ecs::Health>(npc);
                        if (!nh.is_alive()) continue;
                        const auto& nt = npc_explode_view.get<ecs::Transform>(npc);
                        float dx = nt.x - dead_transform.x, dz = nt.z - dead_transform.z;
                        if (std::sqrt(dx * dx + dz * dz) <= tp.kill_explosion_radius) {
                            nh.current = std::max(0.0f, nh.current - explosion_dmg);
                        }
                    }
                }

                // Burn spread on kill (Living Bomb)
                if (tp.burn_spread_radius > 0.0f && registry.all_of<ecs::Combat>(killer)) {
                    float spread_dmg = registry.get<ecs::Combat>(killer).damage * 0.03f;
                    uint32_t killer_net_id2 = registry.all_of<ecs::NetworkId>(killer)
                        ? registry.get<ecs::NetworkId>(killer).id : 0;
                    auto npc_burn_view = registry.view<ecs::NPCTag, ecs::Health, ecs::Transform>();
                    for (auto npc : npc_burn_view) {
                        if (npc == entity) continue;
                        auto& nh = npc_burn_view.get<ecs::Health>(npc);
                        if (!nh.is_alive()) continue;
                        const auto& nt = npc_burn_view.get<ecs::Transform>(npc);
                        float dx = nt.x - dead_transform.x, dz = nt.z - dead_transform.z;
                        if (std::sqrt(dx * dx + dz * dz) <= tp.burn_spread_radius) {
                            apply_effect(registry, npc,
                                ecs::make_status_effect(ecs::StatusEffect::Type::Burn, 4.0f, spread_dmg, killer_net_id2, 1.0f));
                        }
                    }
                }

                // Poison death explosion (Death Zone)
                if (tp.poison_death_explosion_pct > 0.0f && tp.poison_explosion_radius > 0.0f &&
                    dead_max_hp > 0.0f) {
                    float explosion_dmg = dead_max_hp * tp.poison_death_explosion_pct;
                    uint32_t killer_net_id3 = registry.all_of<ecs::NetworkId>(killer)
                        ? registry.get<ecs::NetworkId>(killer).id : 0;
                    auto npc_poison_view = registry.view<ecs::NPCTag, ecs::Health, ecs::Transform>();
                    for (auto npc : npc_poison_view) {
                        if (npc == entity) continue;
                        auto& nh = npc_poison_view.get<ecs::Health>(npc);
                        if (!nh.is_alive()) continue;
                        const auto& nt = npc_poison_view.get<ecs::Transform>(npc);
                        float dx = nt.x - dead_transform.x, dz = nt.z - dead_transform.z;
                        if (std::sqrt(dx * dx + dz * dz) <= tp.poison_explosion_radius) {
                            apply_effect(registry, npc,
                                ecs::make_status_effect(ecs::StatusEffect::Type::Poison, 5.0f, explosion_dmg / 5.0f, killer_net_id3, 1.0f));
                        }
                    }
                }

                // Kill damage/speed buffs (Conqueror)
                if (tp.kill_damage_bonus > 0.0f && tp.kill_damage_dur > 0.0f) {
                    apply_effect(registry, killer,
                        ecs::make_status_effect(ecs::StatusEffect::Type::DamageBoost, tp.kill_damage_dur, tp.kill_damage_bonus));
                }
                if (tp.kill_speed_bonus > 0.0f && tp.kill_speed_dur > 0.0f) {
                    apply_effect(registry, killer,
                        ecs::make_status_effect(ecs::StatusEffect::Type::SpeedBoost, tp.kill_speed_dur, tp.kill_speed_bonus));
                }
            }

            auto loot = roll_loot(monster_info.type_id, config);
            give_loot(registry, killer, loot);

            // Process quest kill objectives and generate events
            auto quest_kill_changes = on_monster_killed(registry, killer, monster_info.type_id, config);
            auto& killer_net_id = registry.get<ecs::NetworkId>(killer);

            for (auto& change : quest_kill_changes) {
                if (change.quest_complete) {
                    GameplayEvent evt;
                    evt.type = GameplayEvent::Type::QuestComplete;
                    evt.player_id = killer_net_id.id;
                    evt.quest_id = change.quest_id;
                    evt.quest_name = change.quest_name;
                    pending_events.push_back(evt);
                } else {
                    GameplayEvent evt;
                    evt.type = GameplayEvent::Type::QuestProgress;
                    evt.player_id = killer_net_id.id;
                    evt.quest_id = change.quest_id;
                    evt.objective_index = change.objective_index;
                    evt.obj_current = change.current;
                    evt.obj_required = change.required;
                    evt.obj_complete = change.objective_complete;
                    pending_events.push_back(evt);
                }
            }

            // Generate gameplay events for the client
            auto& killer_level = registry.get<ecs::PlayerLevel>(killer);

            // XP gain event
            {
                GameplayEvent evt;
                evt.type = GameplayEvent::Type::XPGain;
                evt.player_id = killer_net_id.id;
                evt.xp_gained = killer_level.xp - xp_before;
                evt.total_xp = killer_level.xp;
                // Calculate XP to next level
                const auto& xp_curve = config.leveling().xp_curve;
                int level_idx = killer_level.level;
                evt.xp_to_next = (level_idx < static_cast<int>(xp_curve.size())) ? xp_curve[level_idx] : 99999;
                evt.new_level = killer_level.level;
                pending_events.push_back(evt);
            }

            // Level up event (if level changed)
            if (killer_level.level > level_before) {
                auto& killer_health = registry.get<ecs::Health>(killer);
                auto& killer_combat = registry.get<ecs::Combat>(killer);

                GameplayEvent evt;
                evt.type = GameplayEvent::Type::LevelUp;
                evt.player_id = killer_net_id.id;
                evt.new_level = killer_level.level;
                evt.new_max_health = killer_health.max;
                evt.new_damage = killer_combat.damage;
                evt.total_xp = killer_level.xp;
                const auto& xp_curve = config.leveling().xp_curve;
                int level_idx = killer_level.level;
                evt.xp_to_next = (level_idx < static_cast<int>(xp_curve.size())) ? xp_curve[level_idx] : 99999;
                pending_events.push_back(evt);
            }

            // Loot event
            if (loot.gold > 0 || !loot.items.empty()) {
                GameplayEvent evt;
                evt.type = GameplayEvent::Type::LootDrop;
                evt.player_id = killer_net_id.id;
                evt.loot_gold = loot.gold;
                evt.total_gold = killer_level.gold;
                for (auto& [item_id, count] : loot.items) {
                    const auto* item_cfg = config.find_item(item_id);
                    std::string name = item_cfg ? item_cfg->name : item_id;
                    std::string rarity = item_cfg ? item_cfg->rarity : "common";
                    evt.loot_items.push_back({name, rarity, count});
                }
                pending_events.push_back(evt);
            }

            // Inventory update after loot
            {
                GameplayEvent evt;
                evt.type = GameplayEvent::Type::InventoryUpdate;
                evt.player_id = killer_net_id.id;
                pending_events.push_back(evt);
            }
        }

        // Respawn monster in its zone
        zone_system.respawn_monster(
            registry, entity,
            [&get_terrain_height](float x, float z) { return get_terrain_height(x, z); }
        );

        // Update Jolt body shape to match new collider dimensions after respawn
        if (registry.all_of<ecs::Collider, ecs::PhysicsBody>(entity)) {
            const auto& collider = registry.get<ecs::Collider>(entity);
            physics.update_body_shape(registry, entity, collider.radius, collider.half_height);
        }
    }
}

void handle_player_deaths(
    entt::registry& registry,
    const GameConfig& config,
    const glm::vec2& town_center,
    PhysicsSystem& physics,
    std::mt19937& rng,
    std::function<float(float, float)> get_terrain_height)
{
    std::uniform_real_distribution<float> offset_dist(-50.0f, 50.0f);

    auto death_view = registry.view<ecs::PlayerTag, ecs::Health, ecs::Transform>();
    for (auto entity : death_view) {
        auto& health = death_view.get<ecs::Health>(entity);
        if (health.current > 0.0f) continue;

        // Apply death XP penalty
        apply_death_penalty(registry, entity, config);

        // Clear status effects
        if (auto* buffs = registry.try_get<ecs::BuffState>(entity)) {
            buffs->effects.clear();
        }

        // Reset health to max
        health.current = health.max;

        // Restore mana to full
        if (auto* pl = registry.try_get<ecs::PlayerLevel>(entity)) {
            pl->mana = pl->max_mana;
        }

        // Teleport back to town spawn
        auto& transform = death_view.get<ecs::Transform>(entity);
        transform.x = town_center.x + offset_dist(rng);
        transform.z = town_center.y + offset_dist(rng);
        transform.y = get_terrain_height(transform.x, transform.z);

        // Stop all movement
        if (auto* vel = registry.try_get<ecs::Velocity>(entity)) {
            vel->x = 0.0f;
            vel->y = 0.0f;
            vel->z = 0.0f;
        }

        // Mark physics body for teleport
        if (auto* body = registry.try_get<ecs::PhysicsBody>(entity)) {
            body->needs_teleport = true;
        }
    }
}

} // namespace mmo::server::systems

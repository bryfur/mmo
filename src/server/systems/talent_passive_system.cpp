#include "talent_passive_system.hpp"
#include "buff_system.hpp"
#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <cmath>
#include <algorithm>

namespace mmo::server::systems {

void update_talent_passives(entt::registry& registry, float dt, const GameConfig& config) {
    auto view = registry.view<ecs::PlayerTag, ecs::TalentPassiveState, ecs::Health, ecs::Transform>();

    for (auto entity : view) {
        auto& tp = view.get<ecs::TalentPassiveState>(entity);
        auto& health = view.get<ecs::Health>(entity);

        if (!health.is_alive()) continue;

        // Tick down cooldown timers
        if (tp.cheat_death_timer > 0.0f)  tp.cheat_death_timer  = std::max(0.0f, tp.cheat_death_timer  - dt);
        if (tp.shield_regen_timer > 0.0f) tp.shield_regen_timer = std::max(0.0f, tp.shield_regen_timer - dt);
        if (tp.panic_freeze_timer > 0.0f) tp.panic_freeze_timer = std::max(0.0f, tp.panic_freeze_timer - dt);

        // Combo stack decay
        if (tp.combo_max_stacks > 0 && tp.combo_stacks > 0) {
            tp.combo_decay_timer -= dt;
            if (tp.combo_decay_timer <= 0.0f) {
                tp.combo_stacks = 0;
                tp.combo_decay_timer = 0.0f;
            }
        }

        // Bonus mana regen at low mana (Arcane Surge)
        if (tp.low_mana_threshold > 0.0f && tp.low_mana_regen_mult > 1.0f) {
            if (registry.all_of<ecs::PlayerLevel>(entity)) {
                auto& pl = registry.get<ecs::PlayerLevel>(entity);
                float mana_ratio = pl.max_mana > 0.0f ? pl.mana / pl.max_mana : 1.0f;
                if (mana_ratio < tp.low_mana_threshold) {
                    float bonus = pl.mana_regen * (tp.low_mana_regen_mult - 1.0f) * dt;
                    pl.mana = std::min(pl.max_mana, pl.mana + bonus);
                }
            }
        }

        // Low HP regen (Second Wind)
        if (tp.low_health_regen_pct > 0.0f && tp.low_health_threshold > 0.0f) {
            if (health.ratio() < tp.low_health_threshold) {
                health.current = std::min(health.max, health.current + health.max * tp.low_health_regen_pct * dt);
            }
        }

        // Stationary heal (Holy Ground)
        if (tp.stationary_heal_pct > 0.0f && tp.stationary_timer >= tp.stationary_delay) {
            health.current = std::min(health.max, health.current + health.max * tp.stationary_heal_pct * dt);
        }

        // Periodic shield regeneration (Ice Barrier)
        if (tp.shield_regen_pct > 0.0f && tp.shield_regen_timer <= 0.0f) {
            float shield_value = health.max * tp.shield_regen_pct;
            ecs::StatusEffect shield;
            shield.type = ecs::StatusEffect::Type::Shield;
            shield.duration = tp.shield_regen_cooldown_max + 0.1f;  // lasts until next refresh
            shield.tick_timer = 0.0f;
            shield.tick_interval = 0.0f;
            shield.value = shield_value;
            shield.source_id = 0;
            apply_effect(registry, entity, shield);
            tp.shield_regen_timer = tp.shield_regen_cooldown_max;
        }

        // Fury state: maintain DamageBoost buff when at low HP (Undying Fury)
        if (tp.fury_threshold > 0.0f && tp.fury_damage_mult > 1.0f) {
            if (health.ratio() <= tp.fury_threshold) {
                ecs::StatusEffect fury;
                fury.type = ecs::StatusEffect::Type::DamageBoost;
                fury.duration = dt * 2.0f;  // refreshed each tick while in fury
                fury.tick_timer = 0.0f;
                fury.tick_interval = 0.0f;
                fury.value = tp.fury_damage_mult - 1.0f;
                fury.source_id = 1;  // special fury source id
                apply_effect(registry, entity, fury);
            }
        }

        // Panic freeze at low HP (Frozen Heart)
        if (tp.panic_freeze_radius > 0.0f && tp.panic_freeze_threshold > 0.0f &&
            tp.panic_freeze_timer <= 0.0f && health.ratio() < tp.panic_freeze_threshold) {
            const auto& transform = view.get<ecs::Transform>(entity);
            auto npc_view = registry.view<ecs::NPCTag, ecs::Transform, ecs::Health>();
            for (auto npc : npc_view) {
                auto& nh = npc_view.get<ecs::Health>(npc);
                if (!nh.is_alive()) continue;
                const auto& nt = npc_view.get<ecs::Transform>(npc);
                float dx = nt.x - transform.x, dz = nt.z - transform.z;
                if (std::sqrt(dx * dx + dz * dz) <= tp.panic_freeze_radius) {
                    ecs::StatusEffect freeze;
                    freeze.type = ecs::StatusEffect::Type::Freeze;
                    freeze.duration = tp.panic_freeze_duration;
                    freeze.tick_timer = 0.0f;
                    freeze.tick_interval = 0.0f;
                    freeze.value = 0.0f;
                    freeze.source_id = 0;
                    apply_effect(registry, npc, freeze);
                }
            }
            tp.panic_freeze_timer = tp.panic_freeze_cooldown_max;
        }

        // Passive damage aura (Radiant Aura)
        if (tp.aura_damage_pct > 0.0f && tp.aura_range > 0.0f) {
            tp.aura_tick_timer -= dt;
            if (tp.aura_tick_timer <= 0.0f) {
                tp.aura_tick_timer = 0.5f;  // tick every 0.5 seconds
                const auto& transform = view.get<ecs::Transform>(entity);
                float aura_dmg = 0.0f;
                if (registry.all_of<ecs::Combat>(entity)) {
                    aura_dmg = registry.get<ecs::Combat>(entity).damage * tp.aura_damage_pct * 0.5f;
                }
                if (aura_dmg > 0.0f) {
                    auto npc_view2 = registry.view<ecs::NPCTag, ecs::Transform, ecs::Health>();
                    for (auto npc : npc_view2) {
                        auto& nh = npc_view2.get<ecs::Health>(npc);
                        if (!nh.is_alive()) continue;
                        const auto& nt = npc_view2.get<ecs::Transform>(npc);
                        float dx = nt.x - transform.x, dz = nt.z - transform.z;
                        if (std::sqrt(dx * dx + dz * dz) <= tp.aura_range) {
                            nh.current = std::max(0.0f, nh.current - aura_dmg);
                        }
                    }
                }
            }
        }

        // Nearby enemy debuff aura (Menacing Presence)
        if (tp.nearby_debuff_range > 0.0f && tp.nearby_damage_reduction > 0.0f) {
            tp.debuff_aura_timer -= dt;
            if (tp.debuff_aura_timer <= 0.0f) {
                tp.debuff_aura_timer = 1.0f;  // refresh every second
                const auto& transform = view.get<ecs::Transform>(entity);
                uint32_t player_net_id = registry.all_of<ecs::NetworkId>(entity)
                    ? registry.get<ecs::NetworkId>(entity).id : 0;
                auto npc_view3 = registry.view<ecs::NPCTag, ecs::Transform, ecs::Health>();
                for (auto npc : npc_view3) {
                    auto& nh = npc_view3.get<ecs::Health>(npc);
                    if (!nh.is_alive()) continue;
                    const auto& nt = npc_view3.get<ecs::Transform>(npc);
                    float dx = nt.x - transform.x, dz = nt.z - transform.z;
                    if (std::sqrt(dx * dx + dz * dz) <= tp.nearby_debuff_range) {
                        // Reduce outgoing damage (negative DamageBoost = debuff)
                        ecs::StatusEffect debuff;
                        debuff.type = ecs::StatusEffect::Type::DamageBoost;
                        debuff.duration = 1.1f;  // slightly longer than refresh interval
                        debuff.tick_timer = 0.0f;
                        debuff.tick_interval = 0.0f;
                        debuff.value = -tp.nearby_damage_reduction;
                        debuff.source_id = player_net_id;
                        apply_effect(registry, npc, debuff);
                    }
                }
            }
        }
    }
}

} // namespace mmo::server::systems

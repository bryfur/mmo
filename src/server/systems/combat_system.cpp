#include "combat_system.hpp"
#include "buff_system.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "protocol/protocol.hpp"
#include "server/game_config.hpp"
#include "server/ecs/game_components.hpp"
#include <cmath>
#include <random>
#include <algorithm>
#include <vector>

namespace mmo::server::systems {

using namespace mmo::protocol;

namespace {

float distance(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1;
    float dz = z2 - z1;
    return std::sqrt(dx * dx + dz * dz);
}

entt::entity find_nearest_target(entt::registry& registry, entt::entity attacker,
                                  EntityType target_type, float max_range) {
    const auto& attacker_transform = registry.get<ecs::Transform>(attacker);

    entt::entity nearest = entt::null;
    float nearest_dist = max_range;

    auto view = registry.view<ecs::Transform, ecs::Health, ecs::EntityInfo>();
    for (auto entity : view) {
        if (entity == attacker) continue;

        const auto& info = view.get<ecs::EntityInfo>(entity);
        if (info.type != target_type) continue;

        const auto& health = view.get<ecs::Health>(entity);
        if (!health.is_alive()) continue;

        const auto& transform = view.get<ecs::Transform>(entity);
        float dist = distance(attacker_transform.x, attacker_transform.z,
                             transform.x, transform.z);

        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest = entity;
        }
    }

    return nearest;
}

} // anonymous namespace

bool apply_damage(entt::registry& registry, entt::entity target, float damage,
                   entt::entity attacker) {
    if (target == entt::null) return false;
    if (!registry.all_of<ecs::Health>(target)) return false;

    // Moving dodge check for players
    if (attacker != entt::null && registry.all_of<ecs::TalentStats>(target)) {
        auto& ts = registry.get<ecs::TalentStats>(target);
        auto* tr = registry.try_get<ecs::TalentRuntimeState>(target);
        if (ts.moving_dodge_chance > 0.0f && tr && tr->was_moving_last_tick) {
            static thread_local std::mt19937 rng_dodge{std::random_device{}()};
            static thread_local std::uniform_real_distribution<float> dist_dodge(0.0f, 1.0f);
            if (dist_dodge(rng_dodge) < ts.moving_dodge_chance) {
                return false; // Dodged
            }
        }
    }

    // Stationary damage reduction for target
    if (registry.all_of<ecs::TalentStats>(target)) {
        auto& ts = registry.get<ecs::TalentStats>(target);
        auto* tr = registry.try_get<ecs::TalentRuntimeState>(target);
        if (ts.stationary_damage_reduction > 0.0f && tr && tr->stationary_timer >= ts.stationary_delay) {
            damage *= (1.0f - ts.stationary_damage_reduction);
        }
    }

    // Check target invulnerability and defense buffs
    if (registry.all_of<ecs::BuffState>(target)) {
        auto& target_buffs = registry.get<ecs::BuffState>(target);
        if (target_buffs.is_invulnerable()) {
            return false;
        }
        damage *= target_buffs.get_defense_multiplier();

        // Absorb damage with shield
        float shield = target_buffs.get_shield_value();
        if (shield > 0.0f && damage > 0.0f) {
            float absorbed = std::min(shield, damage);
            damage -= absorbed;
            for (auto& e : target_buffs.effects) {
                if (e.type == ecs::StatusEffect::Type::Shield) {
                    float to_absorb = std::min(e.value, absorbed);
                    e.value -= to_absorb;
                    absorbed -= to_absorb;
                    if (e.value <= 0.0f) e.duration = 0.0f;
                    if (absorbed <= 0.0f) break;
                }
            }
        }
    }

    // Talent defense multiplier
    if (registry.all_of<ecs::TalentPassiveState>(target)) {
        damage *= registry.get<ecs::TalentPassiveState>(target).defense_mult;
    }

    auto& health = registry.get<ecs::Health>(target);
    bool was_alive = health.is_alive();
    health.current = std::max(0.0f, health.current - damage);

    // Cheat death: survive a killing blow
    if (was_alive && !health.is_alive() && registry.all_of<ecs::TalentStats>(target)) {
        auto& ts = registry.get<ecs::TalentStats>(target);
        auto* tr = registry.try_get<ecs::TalentRuntimeState>(target);
        if (ts.has_cheat_death && tr && tr->cheat_death_timer <= 0.0f) {
            health.current = health.max * ts.cheat_death_hp;
            tr->cheat_death_timer = ts.cheat_death_cooldown_max;
            // Grant brief invulnerability
            apply_effect(registry, target,
                ecs::make_status_effect(ecs::StatusEffect::Type::Invulnerable, 2.0f, 0.0f));
        }
    }

    // Apply lifesteal to attacker
    if (attacker != entt::null && damage > 0.0f && registry.all_of<ecs::BuffState>(attacker)) {
        float lifesteal = registry.get<ecs::BuffState>(attacker).get_lifesteal();
        if (lifesteal > 0.0f && registry.all_of<ecs::Health>(attacker)) {
            auto& attacker_health = registry.get<ecs::Health>(attacker);
            attacker_health.current = std::min(attacker_health.max,
                attacker_health.current + damage * lifesteal);
        }
    }

    // Damage reflect: deal a fraction back to attacker
    if (attacker != entt::null && damage > 0.0f && registry.all_of<ecs::TalentPassiveState>(target)) {
        float reflect = registry.get<ecs::TalentPassiveState>(target).reflect_percent;
        if (reflect > 0.0f && registry.all_of<ecs::Health>(attacker)) {
            auto& ah = registry.get<ecs::Health>(attacker);
            ah.current = std::max(0.0f, ah.current - damage * reflect);
        }
    }

    bool died = was_alive && !health.is_alive();

    // Clear all status effects on death
    if (died && registry.all_of<ecs::BuffState>(target)) {
        registry.get<ecs::BuffState>(target).effects.clear();
    }

    return died;
}

// Find targets in a cone/area based on attack direction
std::vector<entt::entity> find_targets_in_direction(entt::registry& registry, entt::entity attacker,
                                                     EntityType target_type, float range,
                                                     float dir_x, float dir_y, float cone_angle) {
    const auto& attacker_transform = registry.get<ecs::Transform>(attacker);
    std::vector<entt::entity> targets;

    auto view = registry.view<ecs::Transform, ecs::Health, ecs::EntityInfo>();
    for (auto entity : view) {
        if (entity == attacker) continue;

        const auto& info = view.get<ecs::EntityInfo>(entity);
        if (info.type != target_type) continue;

        const auto& health = view.get<ecs::Health>(entity);
        if (!health.is_alive()) continue;

        const auto& transform = view.get<ecs::Transform>(entity);
        float dx = transform.x - attacker_transform.x;
        float dz = transform.z - attacker_transform.z;
        float dist = std::sqrt(dx * dx + dz * dz);

        if (dist > range || dist < 0.001f) continue;

        // Check if in cone (dot product check)
        // dir_x/dir_y is a 2D direction on the ground plane (x, z)
        float nx = dx / dist;
        float nz = dz / dist;
        float dot = nx * dir_x + nz * dir_y;

        // cone_angle is half-angle in radians, dot product threshold
        if (dot >= std::cos(cone_angle)) {
            targets.push_back(entity);
        }
    }

    return targets;
}

std::vector<CombatHit> update_combat(entt::registry& registry, float dt, const GameConfig& config) {
    std::vector<CombatHit> hits;

    auto view = registry.view<ecs::Combat, ecs::Health>();

    for (auto entity : view) {
        auto& combat = view.get<ecs::Combat>(entity);
        auto& health = view.get<ecs::Health>(entity);

        if (!health.is_alive()) continue;

        if (combat.current_cooldown > 0) {
            combat.current_cooldown -= dt;
            if (combat.current_cooldown <= 0.0f) {
                combat.current_cooldown = 0.0f;
                combat.is_attacking = false;
            }
        }
    }

    // Process player attacks - use mouse direction for 360-degree attacks
    auto player_view = registry.view<ecs::PlayerTag, ecs::Combat, ecs::InputState, ecs::Health, ecs::EntityInfo>();
    for (auto entity : player_view) {
        auto& combat = player_view.get<ecs::Combat>(entity);
        auto& input_state = player_view.get<ecs::InputState>(entity);
        auto& input = input_state.input;
        const auto& health = player_view.get<ecs::Health>(entity);
        const auto& info = player_view.get<ecs::EntityInfo>(entity);

        // Consume the latched attack flag so it doesn't fire again next tick
        bool wants_attack = input.attacking;
        input.attacking = false;

        if (!health.is_alive() || !wants_attack || !combat.can_attack()) continue;

        // Cannot attack while stunned or frozen
        if (registry.all_of<ecs::BuffState>(entity)) {
            if (registry.get<ecs::BuffState>(entity).is_stunned()) continue;
        }

        // Trigger attack regardless of target - visual effect will play
        combat.is_attacking = true;
        combat.current_cooldown = combat.attack_cooldown;

        // Store attack direction for network sync
        if (!registry.all_of<ecs::AttackDirection>(entity)) {
            registry.emplace<ecs::AttackDirection>(entity);
        }
        auto& attack_dir = registry.get<ecs::AttackDirection>(entity);
        attack_dir.x = input.attack_dir_x;
        attack_dir.y = input.attack_dir_y;

        // Determine attack cone from class config
        float cone_angle = config.get_class(info.player_class).cone_angle;

        // Get talent stats and runtime state for this attacker
        ecs::TalentStats* tp = registry.try_get<ecs::TalentStats>(entity);
        ecs::TalentRuntimeState* tr = registry.try_get<ecs::TalentRuntimeState>(entity);

        static thread_local std::mt19937 rng_combat{std::random_device{}()};
        static thread_local std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

        // Apply damage multiplier from buffs and equipment
        float effective_damage = combat.damage;
        if (registry.all_of<ecs::Equipment>(entity)) {
            effective_damage += registry.get<ecs::Equipment>(entity).damage_bonus;
        }
        if (registry.all_of<ecs::BuffState>(entity)) {
            effective_damage *= registry.get<ecs::BuffState>(entity).get_damage_multiplier();
        }

        // Fury damage bonus is applied via DamageBoost buff from talent_passive_system
        // (do NOT apply fury_damage_mult here to avoid double-dipping)

        // High-mana damage bonus
        if (tp && tp->high_mana_threshold > 0.0f && registry.all_of<ecs::PlayerLevel>(entity)) {
            const auto& pl = registry.get<ecs::PlayerLevel>(entity);
            float mana_ratio = pl.max_mana > 0.0f ? pl.mana / pl.max_mana : 0.0f;
            if (mana_ratio >= tp->high_mana_threshold) {
                effective_damage *= tp->high_mana_damage_mult;
            }
        }

        // Combo stack damage bonus
        if (tp && tp->combo_max_stacks > 0 && tr && tr->combo_stacks > 0) {
            effective_damage *= (1.0f + tp->combo_damage_bonus * static_cast<float>(tr->combo_stacks));
        }

        // Empowered attack tracking
        bool is_empowered = false;
        if (tp && tp->empowered_every > 0 && tr) {
            tr->empowered_counter++;
            if (tr->empowered_counter >= tp->empowered_every) {
                tr->empowered_counter = 0;
                is_empowered = true;
            }
        }

        // Find and damage all targets in attack cone
        auto targets = find_targets_in_direction(registry, entity, EntityType::NPC,
                                                  combat.attack_range,
                                                  input.attack_dir_x, input.attack_dir_y,
                                                  cone_angle);

        uint32_t player_net_id = registry.all_of<ecs::NetworkId>(entity)
            ? registry.get<ecs::NetworkId>(entity).id : 0;

        bool hit_any = false;
        for (auto target : targets) {
            // Factor in target's equipment defense
            float hit_damage = effective_damage;
            if (registry.all_of<ecs::Equipment>(target)) {
                hit_damage = std::max(0.0f, hit_damage - registry.get<ecs::Equipment>(target).defense);
            }

            // Empowered attack damage multiplier
            if (is_empowered && tp) {
                hit_damage *= tp->empowered_damage_mult;
            }

            // Crit check
            if (tp && tp->crit_chance > 0.0f && dist01(rng_combat) < tp->crit_chance) {
                hit_damage *= tp->crit_damage_mult;
            }

            // Stationary damage multiplier
            if (tp && tp->stationary_damage_mult > 1.0f &&
                tr && tr->stationary_timer >= tp->stationary_delay) {
                hit_damage *= tp->stationary_damage_mult;
            }

            // High-HP bonus damage (based on target's HP)
            if (tp && tp->high_hp_bonus_damage > 0.0f && registry.all_of<ecs::Health>(target)) {
                const auto& th = registry.get<ecs::Health>(target);
                if (th.ratio() > tp->high_hp_threshold) {
                    hit_damage *= (1.0f + tp->high_hp_bonus_damage);
                }
            }

            // Max-range damage bonus
            if (tp && tp->max_range_damage_bonus > 0.0f &&
                registry.all_of<ecs::Transform>(entity) && registry.all_of<ecs::Transform>(target)) {
                const auto& at = registry.get<ecs::Transform>(entity);
                const auto& tt = registry.get<ecs::Transform>(target);
                float dx = tt.x - at.x, dz = tt.z - at.z;
                float dist_to_target = std::sqrt(dx * dx + dz * dz);
                if (dist_to_target >= combat.attack_range * 0.75f) {
                    hit_damage *= (1.0f + tp->max_range_damage_bonus);
                }
            }

            // Frozen vulnerability
            if (tp && tp->frozen_vulnerability > 0.0f && registry.all_of<ecs::BuffState>(target)) {
                if (registry.get<ecs::BuffState>(target).has(ecs::StatusEffect::Type::Freeze)) {
                    hit_damage *= (1.0f + tp->frozen_vulnerability);
                }
            }

            bool died = apply_damage(registry, target, hit_damage, entity);

            // On-hit slow
            if (tp && tp->slow_on_hit_value > 0.0f && tp->slow_on_hit_dur > 0.0f) {
                float chance = tp->slow_on_hit_chance > 0.0f ? tp->slow_on_hit_chance : 1.0f;
                if (dist01(rng_combat) < chance) {
                    apply_effect(registry, target,
                        ecs::make_status_effect(ecs::StatusEffect::Type::Slow, tp->slow_on_hit_dur, tp->slow_on_hit_value, player_net_id));
                }
            }

            // On-hit burn
            if (tp && tp->burn_on_hit_pct > 0.0f && tp->burn_on_hit_dur > 0.0f) {
                apply_effect(registry, target,
                    ecs::make_status_effect(ecs::StatusEffect::Type::Burn, tp->burn_on_hit_dur, effective_damage * tp->burn_on_hit_pct, player_net_id, 1.0f));
            }

            // On-hit poison
            if (tp && tp->poison_on_hit_pct > 0.0f && tp->poison_on_hit_dur > 0.0f) {
                apply_effect(registry, target,
                    ecs::make_status_effect(ecs::StatusEffect::Type::Poison, tp->poison_on_hit_dur, effective_damage * tp->poison_on_hit_pct, player_net_id, 1.0f));
            }

            // Empowered stun
            if (is_empowered && tp && tp->empowered_stun_dur > 0.0f) {
                apply_effect(registry, target,
                    ecs::make_status_effect(ecs::StatusEffect::Type::Stun, tp->empowered_stun_dur, 0.0f, player_net_id));
            }

            // Combo stack increment
            if (tp && tp->combo_max_stacks > 0 && tr) {
                tr->combo_stacks = std::min(tp->combo_max_stacks, tr->combo_stacks + 1);
                tr->combo_decay_timer = tp->combo_window;
            }

            CombatHit hit;
            hit.attacker = entity;
            hit.target = target;
            hit.damage = hit_damage;
            hit.target_died = died;
            hits.push_back(hit);
            hit_any = true;
        }

        // Per-attack (not per-target) on-hit effects
        if (hit_any) {
            // Mana on hit
            if (tp && tp->mana_on_hit_pct > 0.0f && registry.all_of<ecs::PlayerLevel>(entity)) {
                auto& pl = registry.get<ecs::PlayerLevel>(entity);
                pl.mana = std::min(pl.max_mana, pl.mana + pl.max_mana * tp->mana_on_hit_pct);
            }

            // Hit speed boost
            if (tp && tp->hit_speed_bonus > 0.0f && tp->hit_speed_dur > 0.0f) {
                apply_effect(registry, entity,
                    ecs::make_status_effect(ecs::StatusEffect::Type::SpeedBoost, tp->hit_speed_dur, tp->hit_speed_bonus, player_net_id));
            }
        }
    }

    // Process NPC attacks
    auto npc_view = registry.view<ecs::NPCTag, ecs::Combat, ecs::AIState, ecs::Health>();
    for (auto entity : npc_view) {
        auto& combat = npc_view.get<ecs::Combat>(entity);
        const auto& ai = npc_view.get<ecs::AIState>(entity);
        const auto& health = npc_view.get<ecs::Health>(entity);

        if (!health.is_alive() || ai.target_id == 0 || !combat.can_attack()) continue;

        // Cannot attack while stunned or frozen
        if (registry.all_of<ecs::BuffState>(entity)) {
            if (registry.get<ecs::BuffState>(entity).is_stunned()) continue;
        }

        auto target = find_nearest_target(registry, entity, EntityType::Player, combat.attack_range);
        if (target != entt::null) {
            combat.is_attacking = true;
            combat.current_cooldown = combat.attack_cooldown;

            float effective_damage = combat.damage;
            if (registry.all_of<ecs::BuffState>(entity)) {
                effective_damage *= registry.get<ecs::BuffState>(entity).get_damage_multiplier();
            }

            // Factor in target's equipment defense
            if (registry.all_of<ecs::Equipment>(target)) {
                effective_damage = std::max(0.0f, effective_damage - registry.get<ecs::Equipment>(target).defense);
            }

            // effective_damage is pre-mitigation; apply_damage handles shields/defense
            bool died = apply_damage(registry, target, effective_damage, entity);
            CombatHit hit;
            hit.attacker = entity;
            hit.target = target;
            hit.damage = effective_damage;  // Report pre-mitigation for consistency
            hit.target_died = died;
            hits.push_back(hit);
        }
    }

    return hits;
}

} // namespace mmo::server::systems

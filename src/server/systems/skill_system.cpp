#include "skill_system.hpp"
#include "buff_system.hpp"
#include "leveling_system.hpp"
#include "protocol/protocol.hpp"
#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <cmath>
#include <algorithm>
#include <random>
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

    // Check mana (apply mana_cost_mult from talents)
    float effective_mana_cost = skill->mana_cost;
    if (registry.all_of<ecs::TalentPassiveState>(player)) {
        effective_mana_cost *= registry.get<ecs::TalentPassiveState>(player).mana_cost_mult;
    }
    if (player_level.mana < effective_mana_cost) return false;

    // Deduct mana
    player_level.mana -= effective_mana_cost;

    // Set cooldown (apply global_cdr and cooldown_mult from talents)
    {
        float effective_cooldown = skill->cooldown;
        if (registry.all_of<ecs::TalentPassiveState>(player)) {
            const auto& tp = registry.get<ecs::TalentPassiveState>(player);
            effective_cooldown = std::max(0.5f,
                effective_cooldown * tp.cooldown_mult * (1.0f - tp.global_cdr));
        }
        skill_state.set_cooldown(skill_id, effective_cooldown);
    }

    // Get player's network ID for status effect source tracking
    uint32_t player_net_id = 0;
    if (registry.all_of<ecs::NetworkId>(player)) {
        player_net_id = registry.get<ecs::NetworkId>(player).id;
    }

    // Apply skill effects - damage and status effects on enemies in range
    if (skill->damage_multiplier > 0.0f || skill->stun_duration > 0.0f ||
        skill->slow_duration > 0.0f || skill->freeze_duration > 0.0f) {

        float total_damage = combat.damage * skill->damage_multiplier;

        // Apply attacker's damage boost from buffs
        if (registry.all_of<ecs::BuffState>(player)) {
            total_damage *= registry.get<ecs::BuffState>(player).get_damage_multiplier();
        }

        // Apply skill_damage_mult from talents
        if (registry.all_of<ecs::TalentPassiveState>(player)) {
            total_damage *= registry.get<ecs::TalentPassiveState>(player).skill_damage_mult;
        }

        // Spell echo RNG (set up before the loop)
        bool do_spell_echo = false;
        {
            static thread_local std::mt19937 rng_echo{std::random_device{}()};
            static thread_local std::uniform_real_distribution<float> dist_echo(0.0f, 1.0f);
            if (registry.all_of<ecs::TalentPassiveState>(player)) {
                float echo_chance = registry.get<ecs::TalentPassiveState>(player).spell_echo_chance;
                if (echo_chance > 0.0f && dist_echo(rng_echo) < echo_chance) {
                    do_spell_echo = true;
                }
            }
        }

        // Get frozen_vulnerability for this cast
        float frozen_vuln = 0.0f;
        if (registry.all_of<ecs::TalentPassiveState>(player)) {
            frozen_vuln = registry.get<ecs::TalentPassiveState>(player).frozen_vulnerability;
        }

        auto view = registry.view<ecs::Transform, ecs::Health, ecs::EntityInfo>();
        int num_passes = do_spell_echo ? 2 : 1;
        for (int echo_pass = 0; echo_pass < num_passes; ++echo_pass) {
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

            // Apply damage (respect invulnerability and defense)
            float actual_damage = total_damage;

            // Frozen vulnerability bonus
            if (frozen_vuln > 0.0f && registry.all_of<ecs::BuffState>(entity)) {
                if (registry.get<ecs::BuffState>(entity).has(ecs::StatusEffect::Type::Freeze)) {
                    actual_damage *= (1.0f + frozen_vuln);
                }
            }

            if (registry.all_of<ecs::BuffState>(entity)) {
                auto& target_buffs = registry.get<ecs::BuffState>(entity);
                if (target_buffs.is_invulnerable()) {
                    actual_damage = 0.0f;
                } else {
                    actual_damage *= target_buffs.get_defense_multiplier();

                    // Absorb damage with shield
                    float shield = target_buffs.get_shield_value();
                    if (shield > 0.0f && actual_damage > 0.0f) {
                        float absorbed = std::min(shield, actual_damage);
                        actual_damage -= absorbed;
                        // Reduce shield value in the first shield effect found
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
            }

            target_health.current = std::max(0.0f, target_health.current - actual_damage);

            // Lifesteal: heal attacker
            if (actual_damage > 0.0f && registry.all_of<ecs::BuffState>(player)) {
                float lifesteal = registry.get<ecs::BuffState>(player).get_lifesteal();
                if (lifesteal > 0.0f) {
                    health.current = std::min(health.max, health.current + actual_damage * lifesteal);
                }
            }

            // Apply stun
            if (skill->stun_duration > 0.0f) {
                apply_effect(registry, entity,
                    ecs::make_status_effect(ecs::StatusEffect::Type::Stun, skill->stun_duration, 0.0f, player_net_id));
            }

            // Apply slow
            if (skill->slow_duration > 0.0f && skill->slow_percent > 0.0f) {
                apply_effect(registry, entity,
                    ecs::make_status_effect(ecs::StatusEffect::Type::Slow, skill->slow_duration, skill->slow_percent, player_net_id));
            }

            // Apply freeze
            if (skill->freeze_duration > 0.0f) {
                apply_effect(registry, entity,
                    ecs::make_status_effect(ecs::StatusEffect::Type::Freeze, skill->freeze_duration, 0.0f, player_net_id));
            }

            // Apply burn (DoT from fire skills)
            if (skill->burn_duration > 0.0f && skill->burn_damage > 0.0f) {
                apply_effect(registry, entity,
                    ecs::make_status_effect(ecs::StatusEffect::Type::Burn, skill->burn_duration, skill->burn_damage, player_net_id, 1.0f));
            }

            // Apply root
            if (skill->root_duration > 0.0f) {
                apply_effect(registry, entity,
                    ecs::make_status_effect(ecs::StatusEffect::Type::Root, skill->root_duration, 0.0f, player_net_id));
            }

            // Apply enemy outgoing damage reduction debuff.
            // Use DamageBoost with a negative value so their outgoing damage is reduced.
            // (DefenseBoost would incorrectly reduce their incoming damage instead.)
            if (skill->debuff_duration > 0.0f && skill->enemy_damage_reduction > 0.0f) {
                apply_effect(registry, entity,
                    ecs::make_status_effect(ecs::StatusEffect::Type::DamageBoost, skill->debuff_duration, -skill->enemy_damage_reduction, player_net_id));
            }
        } // end entity loop
        } // end echo_pass loop
    } // end if damage_multiplier block

    // Heal effect (self)
    if (skill->heal_percent > 0.0f) {
        float heal_amount = health.max * skill->heal_percent;
        health.current = std::min(health.max, health.current + heal_amount);
    }

    // Self-buff: damage reduction
    if (skill->buff_duration > 0.0f && skill->damage_reduction > 0.0f) {
        apply_effect(registry, player,
            ecs::make_status_effect(ecs::StatusEffect::Type::DefenseBoost, skill->buff_duration, skill->damage_reduction, player_net_id));
    }

    // Self-buff: invulnerability
    if (skill->invulnerable_duration > 0.0f) {
        apply_effect(registry, player,
            ecs::make_status_effect(ecs::StatusEffect::Type::Invulnerable, skill->invulnerable_duration, 0.0f, player_net_id));
    }

    // Self-buff: speed boost
    if (skill->speed_boost_duration > 0.0f && skill->speed_boost > 0.0f) {
        apply_effect(registry, player,
            ecs::make_status_effect(ecs::StatusEffect::Type::SpeedBoost, skill->speed_boost_duration, skill->speed_boost, player_net_id));
    }

    // Self-buff: lifesteal
    if (skill->lifesteal_percent > 0.0f) {
        // Lifesteal lasts for the skill's buff_duration or a default of 5s
        float ls_duration = skill->buff_duration > 0.0f ? skill->buff_duration : 5.0f;
        apply_effect(registry, player,
            ecs::make_status_effect(ecs::StatusEffect::Type::Lifesteal, ls_duration, skill->lifesteal_percent, player_net_id));
    }

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
    // Defaults: multipliers=1.0, additive=0.0, booleans=false

    if (!registry.all_of<ecs::TalentState>(player))
        return aggregate;

    const auto& talent_state = registry.get<ecs::TalentState>(player);

    // For "min" fields (cooldowns/delays), only update if talent actually uses them
    float best_cheat_death_cooldown = 9999.0f;
    bool any_cheat_death = false;
    float best_stationary_delay = 9999.0f;
    bool any_stationary = false;

    for (const auto& talent_id : talent_state.unlocked_talents) {
        const TalentConfig* talent = config.find_talent(talent_id);
        if (!talent) continue;

        const auto& e = talent->effect;

        // Multiplicative
        aggregate.damage_mult *= e.damage_mult;
        aggregate.speed_mult *= e.speed_mult;
        aggregate.health_mult *= e.health_mult;
        aggregate.defense_mult *= e.defense_mult;
        aggregate.mana_mult *= e.mana_mult;
        aggregate.cooldown_mult *= e.cooldown_mult;
        aggregate.attack_speed_mult *= e.attack_speed_mult;
        aggregate.crit_damage_mult *= e.crit_damage_mult;
        aggregate.mana_cost_mult *= e.mana_cost_mult;
        aggregate.skill_damage_mult *= e.skill_damage_mult;
        aggregate.attack_range_mult *= e.attack_range_mult;
        aggregate.healing_received_mult *= e.healing_received_mult;
        aggregate.fury_damage_mult *= e.fury_damage_mult;
        aggregate.fury_attack_speed_mult *= e.fury_attack_speed_mult;
        aggregate.empowered_damage_mult *= e.empowered_damage_mult;
        aggregate.high_mana_damage_mult *= e.high_mana_damage_mult;
        aggregate.low_mana_regen_mult *= e.low_mana_regen_mult;
        aggregate.avenge_damage_mult *= e.avenge_damage_mult;
        aggregate.avenge_attack_speed_mult *= e.avenge_attack_speed_mult;
        aggregate.trap_lifetime_mult *= e.trap_lifetime_mult;
        aggregate.trap_radius_mult *= e.trap_radius_mult;

        // Additive
        aggregate.crit_chance += e.crit_chance;
        aggregate.kill_heal_pct += e.kill_heal_pct;
        aggregate.global_cdr += e.global_cdr;
        aggregate.attack_range_bonus += e.attack_range_bonus;
        aggregate.slow_on_hit_chance += e.slow_on_hit_chance;
        aggregate.slow_on_hit_value += e.slow_on_hit_value;
        aggregate.burn_on_hit_pct += e.burn_on_hit_pct;
        aggregate.poison_on_hit_pct += e.poison_on_hit_pct;
        aggregate.mana_on_hit_pct += e.mana_on_hit_pct;
        aggregate.hit_speed_bonus += e.hit_speed_bonus;
        aggregate.kill_explosion_pct += e.kill_explosion_pct;
        aggregate.kill_damage_bonus += e.kill_damage_bonus;
        aggregate.kill_speed_bonus += e.kill_speed_bonus;
        aggregate.burn_spread_radius += e.burn_spread_radius;
        aggregate.reflect_percent += e.reflect_percent;
        aggregate.stationary_damage_reduction += e.stationary_damage_reduction;
        aggregate.stationary_heal_pct += e.stationary_heal_pct;
        aggregate.low_health_regen_pct += e.low_health_regen_pct;
        aggregate.combo_damage_bonus += e.combo_damage_bonus;
        aggregate.aura_damage_pct += e.aura_damage_pct;
        aggregate.nearby_damage_reduction += e.nearby_damage_reduction;
        aggregate.spell_echo_chance += e.spell_echo_chance;
        aggregate.frozen_vulnerability += e.frozen_vulnerability;
        aggregate.high_hp_bonus_damage += e.high_hp_bonus_damage;
        aggregate.max_range_damage_bonus += e.max_range_damage_bonus;
        aggregate.damage_share_percent += e.damage_share_percent;
        aggregate.moving_dodge_chance += e.moving_dodge_chance;
        aggregate.trap_vulnerability += e.trap_vulnerability;
        aggregate.trap_cdr += e.trap_cdr;
        aggregate.trap_cloud_damage += e.trap_cloud_damage;
        aggregate.poison_death_explosion_pct += e.poison_death_explosion_pct;
        aggregate.shield_regen_pct += e.shield_regen_pct;

        // Max for durations/ranges
        aggregate.slow_on_hit_dur = std::max(aggregate.slow_on_hit_dur, e.slow_on_hit_dur);
        aggregate.burn_on_hit_dur = std::max(aggregate.burn_on_hit_dur, e.burn_on_hit_dur);
        aggregate.poison_on_hit_dur = std::max(aggregate.poison_on_hit_dur, e.poison_on_hit_dur);
        aggregate.hit_speed_dur = std::max(aggregate.hit_speed_dur, e.hit_speed_dur);
        aggregate.kill_explosion_radius = std::max(aggregate.kill_explosion_radius, e.kill_explosion_radius);
        aggregate.kill_damage_dur = std::max(aggregate.kill_damage_dur, e.kill_damage_dur);
        aggregate.kill_speed_dur = std::max(aggregate.kill_speed_dur, e.kill_speed_dur);
        aggregate.aura_range = std::max(aggregate.aura_range, e.aura_range);
        aggregate.nearby_debuff_range = std::max(aggregate.nearby_debuff_range, e.nearby_debuff_range);
        aggregate.share_radius = std::max(aggregate.share_radius, e.share_radius);
        aggregate.avenge_duration = std::max(aggregate.avenge_duration, e.avenge_duration);
        aggregate.trap_vulnerability_dur = std::max(aggregate.trap_vulnerability_dur, e.trap_vulnerability_dur);
        aggregate.trap_cloud_duration = std::max(aggregate.trap_cloud_duration, e.trap_cloud_duration);
        aggregate.trap_cloud_radius = std::max(aggregate.trap_cloud_radius, e.trap_cloud_radius);
        aggregate.poison_explosion_radius = std::max(aggregate.poison_explosion_radius, e.poison_explosion_radius);
        aggregate.panic_freeze_radius = std::max(aggregate.panic_freeze_radius, e.panic_freeze_radius);
        aggregate.panic_freeze_duration = std::max(aggregate.panic_freeze_duration, e.panic_freeze_duration);
        aggregate.panic_freeze_threshold = std::max(aggregate.panic_freeze_threshold, e.panic_freeze_threshold);
        aggregate.fury_threshold = std::max(aggregate.fury_threshold, e.fury_threshold);
        aggregate.combo_window = std::max(aggregate.combo_window, e.combo_window);
        aggregate.high_mana_threshold = std::max(aggregate.high_mana_threshold, e.high_mana_threshold);
        aggregate.low_mana_threshold = std::max(aggregate.low_mana_threshold, e.low_mana_threshold);
        aggregate.high_hp_threshold = std::max(aggregate.high_hp_threshold, e.high_hp_threshold);
        aggregate.low_health_threshold = std::max(aggregate.low_health_threshold, e.low_health_threshold);
        if (e.stationary_damage_mult > 1.0f)
            aggregate.stationary_damage_mult = std::max(aggregate.stationary_damage_mult, e.stationary_damage_mult);

        // Min for cooldowns/delays (only when talent actually provides the effect)
        if (e.has_cheat_death) {
            any_cheat_death = true;
            best_cheat_death_cooldown = std::min(best_cheat_death_cooldown, e.cheat_death_cooldown);
            aggregate.cheat_death_hp = std::max(aggregate.cheat_death_hp, e.cheat_death_hp);
        }
        if (e.stationary_damage_mult > 1.0f || e.stationary_damage_reduction > 0.0f ||
            e.stationary_heal_pct > 0.0f) {
            any_stationary = true;
            best_stationary_delay = std::min(best_stationary_delay, e.stationary_delay);
        }
        if (e.panic_freeze_radius > 0.0f)
            aggregate.panic_freeze_cooldown = std::min(aggregate.panic_freeze_cooldown, e.panic_freeze_cooldown);
        if (e.shield_regen_pct > 0.0f)
            aggregate.shield_regen_cooldown = std::min(aggregate.shield_regen_cooldown, e.shield_regen_cooldown);

        // Max for ints
        aggregate.combo_max_stacks = std::max(aggregate.combo_max_stacks, e.combo_max_stacks);
        aggregate.empowered_every = std::max(aggregate.empowered_every, e.empowered_every);
        aggregate.max_traps = std::max(aggregate.max_traps, e.max_traps);

        // Boolean OR
        aggregate.cc_immunity = aggregate.cc_immunity || e.cc_immunity;
        aggregate.has_cheat_death = aggregate.has_cheat_death || e.has_cheat_death;
        aggregate.has_avenge = aggregate.has_avenge || e.has_avenge;
    }

    // Apply collected min values
    if (any_cheat_death)
        aggregate.cheat_death_cooldown = best_cheat_death_cooldown;
    if (any_stationary)
        aggregate.stationary_delay = best_stationary_delay;

    return aggregate;
}

void apply_talent_effects(entt::registry& registry, entt::entity player, const GameConfig& config) {
    TalentEffect effects = compute_talent_effects(registry, player, config);

    if (!registry.all_of<ecs::EntityInfo>(player)) return;
    const auto& info = registry.get<ecs::EntityInfo>(player);
    const auto& cls = config.get_class(info.player_class);

    // Compute per-level growth to add on top of base class stats
    int levels_gained = 0;
    float growth_health = 0.0f, growth_damage = 0.0f, growth_cooldown_reduction = 0.0f;
    if (registry.all_of<ecs::PlayerLevel>(player)) {
        levels_gained = registry.get<ecs::PlayerLevel>(player).level - 1;
    }
    if (levels_gained > 0) {
        int class_index = info.player_class;
        const auto& leveling = config.leveling();
        if (class_index >= 0 && class_index < static_cast<int>(leveling.class_growth.size())) {
            const auto& growth = leveling.class_growth[class_index];
            growth_health = growth.health * levels_gained;
            growth_damage = growth.damage * levels_gained;
            growth_cooldown_reduction = growth.attack_cooldown_reduction * levels_gained;
        }
    }

    // Apply talent multipliers to (base + level growth) stats
    if (registry.all_of<ecs::Combat>(player)) {
        auto& combat = registry.get<ecs::Combat>(player);
        combat.damage = (cls.damage + growth_damage) * effects.damage_mult;
        combat.attack_cooldown = std::max(0.3f,
            (cls.attack_cooldown - growth_cooldown_reduction) * effects.attack_speed_mult);
        combat.attack_range = cls.attack_range * effects.attack_range_mult + effects.attack_range_bonus;
    }

    if (registry.all_of<ecs::Health>(player)) {
        auto& health = registry.get<ecs::Health>(player);
        float new_max = (cls.health + growth_health) * effects.health_mult;
        float ratio = health.max > 0.0f ? health.current / health.max : 1.0f;
        health.max = new_max;
        health.current = new_max * ratio;
    }

    if (registry.all_of<ecs::PlayerLevel>(player)) {
        auto& pl = registry.get<ecs::PlayerLevel>(player);
        float new_max = cls.base_mana * effects.mana_mult;
        float ratio = pl.max_mana > 0.0f ? pl.mana / pl.max_mana : 1.0f;
        pl.max_mana = new_max;
        pl.mana = new_max * ratio;
    }

    // Update or create TalentStats and TalentRuntimeState
    if (!registry.all_of<ecs::TalentStats>(player)) {
        registry.emplace<ecs::TalentStats>(player);
    }
    if (!registry.all_of<ecs::TalentRuntimeState>(player)) {
        registry.emplace<ecs::TalentRuntimeState>(player);
    }
    auto& tp = registry.get<ecs::TalentStats>(player);

    // Update computed fields (do NOT reset runtime state like timers/counters)
    tp.speed_mult = effects.speed_mult;
    tp.defense_mult = effects.defense_mult;
    tp.crit_chance = effects.crit_chance;
    tp.crit_damage_mult = effects.crit_damage_mult;
    tp.kill_heal_pct = effects.kill_heal_pct;
    tp.mana_cost_mult = effects.mana_cost_mult;
    tp.skill_damage_mult = effects.skill_damage_mult;
    tp.attack_range_bonus = effects.attack_range_bonus;
    tp.attack_range_mult = effects.attack_range_mult;
    tp.healing_received_mult = effects.healing_received_mult;
    tp.global_cdr = effects.global_cdr;
    tp.cooldown_mult = effects.cooldown_mult;
    tp.cc_immunity = effects.cc_immunity;
    tp.slow_on_hit_chance = effects.slow_on_hit_chance;
    tp.slow_on_hit_value = effects.slow_on_hit_value;
    tp.slow_on_hit_dur = effects.slow_on_hit_dur;
    tp.burn_on_hit_pct = effects.burn_on_hit_pct;
    tp.burn_on_hit_dur = effects.burn_on_hit_dur;
    tp.poison_on_hit_pct = effects.poison_on_hit_pct;
    tp.poison_on_hit_dur = effects.poison_on_hit_dur;
    tp.mana_on_hit_pct = effects.mana_on_hit_pct;
    tp.hit_speed_bonus = effects.hit_speed_bonus;
    tp.hit_speed_dur = effects.hit_speed_dur;
    tp.kill_explosion_pct = effects.kill_explosion_pct;
    tp.kill_explosion_radius = effects.kill_explosion_radius;
    tp.kill_damage_bonus = effects.kill_damage_bonus;
    tp.kill_damage_dur = effects.kill_damage_dur;
    tp.kill_speed_bonus = effects.kill_speed_bonus;
    tp.kill_speed_dur = effects.kill_speed_dur;
    tp.burn_spread_radius = effects.burn_spread_radius;
    tp.has_cheat_death = effects.has_cheat_death;
    tp.cheat_death_hp = effects.cheat_death_hp;
    tp.cheat_death_cooldown_max = effects.cheat_death_cooldown;
    tp.reflect_percent = effects.reflect_percent;
    tp.stationary_damage_mult = effects.stationary_damage_mult;
    tp.stationary_damage_reduction = effects.stationary_damage_reduction;
    tp.stationary_heal_pct = effects.stationary_heal_pct;
    tp.stationary_delay = effects.stationary_delay;
    tp.low_health_regen_pct = effects.low_health_regen_pct;
    tp.low_health_threshold = effects.low_health_threshold;
    tp.fury_threshold = effects.fury_threshold;
    tp.fury_damage_mult = effects.fury_damage_mult;
    tp.fury_attack_speed_mult = effects.fury_attack_speed_mult;
    tp.combo_damage_bonus = effects.combo_damage_bonus;
    tp.combo_max_stacks = effects.combo_max_stacks;
    tp.combo_window = effects.combo_window;
    tp.empowered_every = effects.empowered_every;
    tp.empowered_damage_mult = effects.empowered_damage_mult;
    tp.empowered_stun_dur = effects.empowered_stun_dur;
    tp.aura_damage_pct = effects.aura_damage_pct;
    tp.aura_range = effects.aura_range;
    tp.nearby_debuff_range = effects.nearby_debuff_range;
    tp.nearby_damage_reduction = effects.nearby_damage_reduction;
    tp.panic_freeze_radius = effects.panic_freeze_radius;
    tp.panic_freeze_duration = effects.panic_freeze_duration;
    tp.panic_freeze_threshold = effects.panic_freeze_threshold;
    tp.panic_freeze_cooldown_max = effects.panic_freeze_cooldown;
    tp.shield_regen_pct = effects.shield_regen_pct;
    tp.shield_regen_cooldown_max = effects.shield_regen_cooldown;
    tp.spell_echo_chance = effects.spell_echo_chance;
    tp.frozen_vulnerability = effects.frozen_vulnerability;
    tp.high_mana_damage_mult = effects.high_mana_damage_mult;
    tp.high_mana_threshold = effects.high_mana_threshold;
    tp.low_mana_regen_mult = effects.low_mana_regen_mult;
    tp.low_mana_threshold = effects.low_mana_threshold;
    tp.high_hp_bonus_damage = effects.high_hp_bonus_damage;
    tp.high_hp_threshold = effects.high_hp_threshold;
    tp.max_range_damage_bonus = effects.max_range_damage_bonus;
    tp.damage_share_percent = effects.damage_share_percent;
    tp.share_radius = effects.share_radius;
    tp.has_avenge = effects.has_avenge;
    tp.avenge_damage_mult = effects.avenge_damage_mult;
    tp.avenge_attack_speed_mult = effects.avenge_attack_speed_mult;
    tp.avenge_duration = effects.avenge_duration;
    tp.moving_dodge_chance = effects.moving_dodge_chance;
    tp.max_traps = effects.max_traps;
    tp.trap_lifetime_mult = effects.trap_lifetime_mult;
    tp.trap_radius_mult = effects.trap_radius_mult;
    tp.trap_vulnerability = effects.trap_vulnerability;
    tp.trap_vulnerability_dur = effects.trap_vulnerability_dur;
    tp.trap_cdr = effects.trap_cdr;
    tp.trap_cloud_damage = effects.trap_cloud_damage;
    tp.trap_cloud_duration = effects.trap_cloud_duration;
    tp.trap_cloud_radius = effects.trap_cloud_radius;
    tp.poison_death_explosion_pct = effects.poison_death_explosion_pct;
    tp.poison_explosion_radius = effects.poison_explosion_radius;
    // TalentRuntimeState (timers, stacks, counters) is intentionally NOT reset here
}

} // namespace mmo::server::systems

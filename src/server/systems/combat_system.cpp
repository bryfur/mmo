#include "combat_system.hpp"
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

void apply_damage(entt::registry& registry, entt::entity target, float damage,
                   float world_width, float world_height,
                   entt::entity attacker = entt::null) {
    if (target == entt::null) return;

    // Check target invulnerability and defense
    if (registry.all_of<ecs::BuffState>(target)) {
        auto& target_buffs = registry.get<ecs::BuffState>(target);
        if (target_buffs.is_invulnerable()) {
            return; // No damage
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

    auto& health = registry.get<ecs::Health>(target);
    health.current = std::max(0.0f, health.current - damage);

    // Apply lifesteal to attacker
    if (attacker != entt::null && damage > 0.0f && registry.all_of<ecs::BuffState>(attacker)) {
        float lifesteal = registry.get<ecs::BuffState>(attacker).get_lifesteal();
        if (lifesteal > 0.0f && registry.all_of<ecs::Health>(attacker)) {
            auto& attacker_health = registry.get<ecs::Health>(attacker);
            attacker_health.current = std::min(attacker_health.max,
                attacker_health.current + damage * lifesteal);
        }
    }

    // Respawn NPCs
    if (!health.is_alive() && registry.all_of<ecs::NPCTag>(target)) {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist_x(100.0f, world_width - 100.0f);
        std::uniform_real_distribution<float> dist_z(100.0f, world_height - 100.0f);

        // Clear all status effects on death
        if (registry.all_of<ecs::BuffState>(target)) {
            registry.get<ecs::BuffState>(target).effects.clear();
        }

        health.current = health.max;
        auto& transform = registry.get<ecs::Transform>(target);
        transform.x = dist_x(rng);
        transform.z = dist_z(rng);

        // Mark physics body for teleport to new position
        if (registry.all_of<ecs::PhysicsBody>(target)) {
            registry.get<ecs::PhysicsBody>(target).needs_teleport = true;
        }
    }
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

} // anonymous namespace

void update_combat(entt::registry& registry, float dt, const GameConfig& config) {
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

        // Apply damage multiplier from buffs
        float effective_damage = combat.damage;
        if (registry.all_of<ecs::BuffState>(entity)) {
            effective_damage *= registry.get<ecs::BuffState>(entity).get_damage_multiplier();
        }

        // Find and damage all targets in attack cone
        auto targets = find_targets_in_direction(registry, entity, EntityType::NPC,
                                                  combat.attack_range,
                                                  input.attack_dir_x, input.attack_dir_y,
                                                  cone_angle);
        for (auto target : targets) {
            apply_damage(registry, target, effective_damage, config.world().width, config.world().height, entity);
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

            apply_damage(registry, target, effective_damage, config.world().width, config.world().height, entity);
        }
    }
}

} // namespace mmo::server::systems

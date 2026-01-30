#include "combat_system.hpp"
#include "server/game_types.hpp"
#include "server/ecs/game_components.hpp"
#include <cmath>
#include <random>
#include <algorithm>

namespace mmo::systems {

namespace {

float distance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
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
        float dist = distance(attacker_transform.x, attacker_transform.y, 
                             transform.x, transform.y);
        
        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest = entity;
        }
    }
    
    return nearest;
}

void apply_damage(entt::registry& registry, entt::entity target, float damage,
                   float world_width, float world_height) {
    if (target == entt::null) return;

    auto& health = registry.get<ecs::Health>(target);
    health.current = std::max(0.0f, health.current - damage);

    // Respawn NPCs
    if (!health.is_alive() && registry.all_of<ecs::NPCTag>(target)) {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist_x(100.0f, world_width - 100.0f);
        std::uniform_real_distribution<float> dist_y(100.0f, world_height - 100.0f);

        health.current = health.max;
        auto& transform = registry.get<ecs::Transform>(target);
        transform.x = dist_x(rng);
        transform.y = dist_y(rng);

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
        float dy = transform.y - attacker_transform.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist > range || dist < 0.001f) continue;
        
        // Check if in cone (dot product check)
        float nx = dx / dist;
        float ny = dy / dist;
        float dot = nx * dir_x + ny * dir_y;
        
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
        }
        
        combat.is_attacking = false;
    }
    
    // Process player attacks - use mouse direction for 360-degree attacks
    auto player_view = registry.view<ecs::PlayerTag, ecs::Combat, ecs::InputState, ecs::Health, ecs::EntityInfo>();
    for (auto entity : player_view) {
        auto& combat = player_view.get<ecs::Combat>(entity);
        const auto& input_state = player_view.get<ecs::InputState>(entity);
        const auto& input = input_state.input;
        const auto& health = player_view.get<ecs::Health>(entity);
        const auto& info = player_view.get<ecs::EntityInfo>(entity);
        
        if (!health.is_alive() || !input.attacking || !combat.can_attack()) continue;
        
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
        
        // Find and damage all targets in attack cone
        auto targets = find_targets_in_direction(registry, entity, EntityType::NPC, 
                                                  combat.attack_range, 
                                                  input.attack_dir_x, input.attack_dir_y, 
                                                  cone_angle);
        for (auto target : targets) {
            apply_damage(registry, target, combat.damage, config.world().width, config.world().height);
        }
    }
    
    // Process NPC attacks
    auto npc_view = registry.view<ecs::NPCTag, ecs::Combat, ecs::AIState, ecs::Health>();
    for (auto entity : npc_view) {
        auto& combat = npc_view.get<ecs::Combat>(entity);
        const auto& ai = npc_view.get<ecs::AIState>(entity);
        const auto& health = npc_view.get<ecs::Health>(entity);
        
        if (!health.is_alive() || ai.target_id == 0 || !combat.can_attack()) continue;
        
        auto target = find_nearest_target(registry, entity, EntityType::Player, combat.attack_range);
        if (target != entt::null) {
            combat.is_attacking = true;
            combat.current_cooldown = combat.attack_cooldown;
            apply_damage(registry, target, combat.damage, config.world().width, config.world().height);
        }
    }
}

} // namespace mmo::systems

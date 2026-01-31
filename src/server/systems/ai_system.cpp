#include "ai_system.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <cmath>
#include <cstdlib>

namespace mmo::server::systems {

namespace {

float distance(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1;
    float dz = z2 - z1;
    return std::sqrt(dx * dx + dz * dz);
}

bool is_in_safe_zone(float x, float z, float center_x, float center_z, float radius) {
    float dx = x - center_x;
    float dz = z - center_z;
    return (dx * dx + dz * dz) < (radius * radius);
}

} // anonymous namespace

void update_ai(entt::registry& registry, float dt, const GameConfig& config) {
    const float town_center_x = config.world().width / 2.0f;
    const float town_center_z = config.world().height / 2.0f;
    const float safe_radius = config.safe_zone_radius();
    // Update hostile NPCs
    auto npc_view = registry.view<ecs::NPCTag, ecs::Transform, ecs::Velocity,
                                   ecs::Combat, ecs::AIState, ecs::Health>();

    for (auto entity : npc_view) {
        auto& health = npc_view.get<ecs::Health>(entity);
        if (!health.is_alive()) continue;

        auto& transform = npc_view.get<ecs::Transform>(entity);
        auto& velocity = npc_view.get<ecs::Velocity>(entity);
        auto& combat = npc_view.get<ecs::Combat>(entity);
        auto& ai = npc_view.get<ecs::AIState>(entity);

        // Find nearest player target (not in safe zone)
        entt::entity nearest_player = entt::null;
        float nearest_dist = ai.aggro_range;

        auto player_view = registry.view<ecs::PlayerTag, ecs::Transform, ecs::Health>();
        for (auto player : player_view) {
            const auto& player_health = player_view.get<ecs::Health>(player);
            if (!player_health.is_alive()) continue;

            const auto& player_transform = player_view.get<ecs::Transform>(player);

            // Don't aggro players in safe zone
            if (is_in_safe_zone(player_transform.x, player_transform.z, town_center_x, town_center_z, safe_radius)) continue;

            float dist = distance(transform.x, transform.z,
                                 player_transform.x, player_transform.z);

            if (dist < nearest_dist) {
                nearest_dist = dist;
                nearest_player = player;
            }
        }

        if (nearest_player != entt::null) {
            const auto& target_transform = registry.get<ecs::Transform>(nearest_player);
            const auto& target_net_id = registry.get<ecs::NetworkId>(nearest_player);
            ai.target_id = target_net_id.id;

            float dx = target_transform.x - transform.x;
            float dz = target_transform.z - transform.z;
            float dist = distance(transform.x, transform.z,
                                 target_transform.x, target_transform.z);

            if (dist > combat.attack_range) {
                velocity.x = (dx / dist) * config.monster().speed;
                velocity.z = (dz / dist) * config.monster().speed;
            } else {
                velocity.x = 0;
                velocity.z = 0;
            }
        } else {
            ai.target_id = 0;
            velocity.x = 0;
            velocity.z = 0;
        }
    }

    // Update town NPCs (wandering)
    auto town_npc_view = registry.view<ecs::TownNPCAI, ecs::Transform, ecs::Velocity>();

    for (auto entity : town_npc_view) {
        auto& ai = town_npc_view.get<ecs::TownNPCAI>(entity);
        auto& transform = town_npc_view.get<ecs::Transform>(entity);
        auto& velocity = town_npc_view.get<ecs::Velocity>(entity);

        if (ai.is_moving) {
            // Move towards target
            float dx = ai.target_x - transform.x;
            float dz = ai.target_z - transform.z;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist < 5.0f) {
                // Reached target, stop and idle
                ai.is_moving = false;
                ai.idle_timer = 2.0f + (std::rand() % 30) / 10.0f;  // 2-5 seconds
                velocity.x = 0;
                velocity.z = 0;
            } else {
                float speed = 30.0f;  // Slow walking speed
                velocity.x = (dx / dist) * speed;
                velocity.z = (dz / dist) * speed;
            }

            ai.move_timer -= dt;
            if (ai.move_timer <= 0) {
                // Timeout, stop moving
                ai.is_moving = false;
                ai.idle_timer = 1.0f;
                velocity.x = 0;
                velocity.z = 0;
            }
        } else {
            // Idle, wait then pick new target
            ai.idle_timer -= dt;
            if (ai.idle_timer <= 0) {
                // Pick random target within wander radius
                float angle = (std::rand() % 360) * 3.14159f / 180.0f;
                float radius = (std::rand() % 100) / 100.0f * ai.wander_radius;
                ai.target_x = ai.home_x + std::cos(angle) * radius;
                ai.target_z = ai.home_z + std::sin(angle) * radius;
                ai.is_moving = true;
                ai.move_timer = 5.0f;  // Max move time
            }
        }
    }
}

} // namespace mmo::server::systems

#include "movement_system.hpp"
#include "server/ecs/game_components.hpp"
#include <algorithm>
#include <cmath>

namespace mmo::systems {

void update_movement(entt::registry& registry, float dt, const GameConfig& config) {
    // Update player movement based on input
    // Only set velocity - physics system handles actual position updates
    auto player_view = registry.view<ecs::PlayerTag, ecs::Transform, ecs::Velocity,
                                      ecs::InputState, ecs::Health, ecs::EntityInfo>();

    for (auto entity : player_view) {
        auto& health = player_view.get<ecs::Health>(entity);
        if (!health.is_alive()) continue;

        auto& velocity = player_view.get<ecs::Velocity>(entity);
        const auto& input_state = player_view.get<ecs::InputState>(entity);
        const auto& input = input_state.input;
        const auto& info = player_view.get<ecs::EntityInfo>(entity);
        float speed = config.get_class(info.player_class).speed;

        // Use continuous movement direction for smooth camera-relative movement
        // move_dir is already normalized by the client
        float move_len = std::sqrt(input.move_dir_x * input.move_dir_x +
                                    input.move_dir_y * input.move_dir_y);

        if (move_len > 0.1f) {
            // Use continuous direction for smooth movement
            velocity.x = input.move_dir_x * speed;
            velocity.y = input.move_dir_y * speed;
        } else {
            velocity.x = 0.0f;
            velocity.y = 0.0f;
        }
        
        // Don't update transform directly - physics will handle it
        // But for entities without physics, we still update (fallback)
        if (!registry.all_of<ecs::PhysicsBody>(entity)) {
            auto& transform = player_view.get<ecs::Transform>(entity);
            transform.x += velocity.x * dt;
            transform.y += velocity.y * dt;
        }
    }
    
    // Update NPC movement (handled by AI system)
    // Physics system will apply velocities and handle collisions
    auto npc_view = registry.view<ecs::NPCTag, ecs::Transform, ecs::Velocity, ecs::Health>();
    
    for (auto entity : npc_view) {
        auto& health = npc_view.get<ecs::Health>(entity);
        if (!health.is_alive()) continue;
        
        // Only update transform if no physics body (fallback)
        if (!registry.all_of<ecs::PhysicsBody>(entity)) {
            auto& transform = npc_view.get<ecs::Transform>(entity);
            const auto& velocity = npc_view.get<ecs::Velocity>(entity);
            
            transform.x += velocity.x * dt;
            transform.y += velocity.y * dt;
            
            float half_size = config.monster().size / 2;
            transform.x = std::clamp(transform.x, half_size, config.world().width - half_size);
            transform.y = std::clamp(transform.y, half_size, config.world().height - half_size);
        }
    }
}

} // namespace mmo::systems

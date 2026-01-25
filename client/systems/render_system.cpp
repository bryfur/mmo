#include "render_system.hpp"
#include "common/protocol.hpp"

namespace mmo {

EntityState RenderSystem::build_entity_state(entt::registry& registry, entt::entity entity) {
    auto& net_id = registry.get<ecs::NetworkId>(entity);
    auto& transform = registry.get<ecs::Transform>(entity);
    auto& health = registry.get<ecs::Health>(entity);
    auto& info = registry.get<ecs::EntityInfo>(entity);
    auto& name = registry.get<ecs::Name>(entity);
    
    EntityState state;
    state.id = net_id.id;
    state.x = transform.x;
    state.y = transform.y;
    state.z = transform.z;
    state.health = health.current;
    state.max_health = health.max;
    state.type = info.type;
    state.player_class = info.player_class;
    state.color = info.color;
    state.npc_type = info.npc_type;
    state.building_type = info.building_type;
    state.environment_type = info.environment_type;
    state.rotation = transform.rotation;
    std::strncpy(state.name, name.value.c_str(), sizeof(state.name) - 1);
    state.name[sizeof(state.name) - 1] = '\0';
    
    // Optional velocity
    if (auto* vel = registry.try_get<ecs::Velocity>(entity)) {
        state.vx = vel->x;
        state.vy = vel->y;
    }
    
    // Optional combat state
    if (auto* combat = registry.try_get<ecs::Combat>(entity)) {
        state.is_attacking = combat->is_attacking;
        state.attack_cooldown = combat->current_cooldown;
    }
    
    // Optional attack direction
    if (auto* attack_dir = registry.try_get<ecs::AttackDirection>(entity)) {
        state.attack_dir_x = attack_dir->x;
        state.attack_dir_y = attack_dir->y;
    }
    
    // Optional scale
    if (auto* scale = registry.try_get<ecs::Scale>(entity)) {
        state.scale = scale->value;
    }
    
    return state;
}

void RenderSystem::collect_entities(entt::registry& registry, RenderScene& scene, uint32_t local_player_id) {
    // Query all entities with required components
    auto view = registry.view<ecs::NetworkId, ecs::Transform, ecs::Health, ecs::EntityInfo, ecs::Name>();
    
    for (auto entity : view) {
        EntityState state = build_entity_state(registry, entity);
        bool is_local = (state.id == local_player_id);
        scene.add_entity(state, is_local);
    }
}

void RenderSystem::collect_entity_shadows(entt::registry& registry, RenderScene& scene) {
    // Query all entities for shadow rendering
    auto view = registry.view<ecs::NetworkId, ecs::Transform, ecs::Health, ecs::EntityInfo, ecs::Name>();
    
    for (auto entity : view) {
        EntityState state = build_entity_state(registry, entity);
        scene.add_entity_shadow(state);
    }
}

} // namespace mmo

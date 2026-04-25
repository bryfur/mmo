#include "minimap_system.hpp"

#include "client/ecs/components.hpp"

namespace mmo::client::systems {

std::optional<uint32_t> minimap_color_for(protocol::EntityType type) {
    using protocol::EntityType;
    switch (type) {
        case EntityType::TownNPC:  return 0xFF00CC00u; // green
        case EntityType::NPC:      return 0xFF0000FFu; // red
        case EntityType::Player:   return 0xFFFFFF00u; // cyan
        case EntityType::Building: return 0xFF888888u; // gray
        default:                   return std::nullopt;
    }
}

void update_minimap(entt::registry& registry,
                    HUDState& hud,
                    PanelState& panel,
                    entt::entity local_player_entity,
                    uint32_t local_player_network_id,
                    float view_radius_world) {
    hud.minimap.icons.clear();
    hud.minimap.objective_areas.clear();

    if (local_player_entity == entt::null || !registry.valid(local_player_entity)) {
        return;
    }

    const auto& local_tf = registry.get<const ecs::Transform>(local_player_entity);
    hud.minimap.player_x = local_tf.x;
    hud.minimap.player_z = local_tf.z;
    panel.player_x = local_tf.x;
    panel.player_z = local_tf.z;

    const float radius_sq = view_radius_world * view_radius_world;

    auto view = registry.view<const ecs::Transform, const ecs::EntityInfo,
                              const ecs::NetworkId>();
    for (auto entity : view) {
        const auto& nid = view.get<const ecs::NetworkId>(entity);
        if (nid.id == local_player_network_id) continue;

        const auto& tf = view.get<const ecs::Transform>(entity);
        const float dx = tf.x - local_tf.x;
        const float dz = tf.z - local_tf.z;
        if (dx * dx + dz * dz > radius_sq) continue;

        const auto& info = view.get<const ecs::EntityInfo>(entity);
        const auto color = minimap_color_for(info.type);
        if (!color) continue;

        HUDState::MinimapState::MapIcon icon;
        icon.world_x = tf.x;
        icon.world_z = tf.z;
        icon.color = *color;
        icon.is_objective = false;
        hud.minimap.icons.push_back(icon);
    }
}

} // namespace mmo::client::systems

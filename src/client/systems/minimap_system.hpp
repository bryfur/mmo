#pragma once

#include "client/hud/hud_state.hpp"
#include "client/hud/panel_state.hpp"
#include "protocol/protocol.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <optional>

namespace mmo::client::systems {

// Color the minimap should use to represent an entity of this type, or
// nullopt if the type should not appear on the minimap. Pure look-up so
// the colour table is testable without spinning up a registry or HUD.
std::optional<uint32_t> minimap_color_for(protocol::EntityType type);

// Refresh `hud.minimap` and `panel_player_*` from the current world state.
// Clears the minimap icon list, snapshots the local player's planar position
// (and forwards it to PanelState so the world-map panel agrees), then
// pushes one icon per nearby entity within `view_radius_world` units.
//
// `local_player_entity` is the entt::entity for the local player, or
// entt::null when the player isn't yet known — in which case nothing is
// updated except clearing the icon list.
void update_minimap(entt::registry& registry, HUDState& hud, PanelState& panel, entt::entity local_player_entity,
                    uint32_t local_player_network_id, float view_radius_world);

} // namespace mmo::client::systems

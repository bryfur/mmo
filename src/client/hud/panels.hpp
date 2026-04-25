#pragma once

// Modal panels — inventory / talents / quest log / world map. Each builds its
// own background, drag-region, and close button via MouseUI when its owning
// PanelState says it should be visible.

#include "client/hud/hud_state.hpp"
#include "client/hud/panel_state.hpp"
#include "client/mouse_ui.hpp"
#include "engine/scene/ui_scene.hpp"

namespace mmo::client::hud {

void build_inventory_panel(engine::scene::UIScene& ui, const PanelState& panel, MouseUI& mui, float screen_w,
                           float screen_h);

void build_talent_panel(engine::scene::UIScene& ui,
                        PanelState& panel, // mutates talent_scroll_offset
                        MouseUI& mui, float screen_w, float screen_h);

void build_quest_log_panel(engine::scene::UIScene& ui, const HUDState& hud, const PanelState& panel, MouseUI& mui,
                           float screen_w, float screen_h);

} // namespace mmo::client::hud

namespace mmo::client {

// World-map panel + its dispatcher. Different namespace from the other
// panels for historical reasons; consolidate later.
void build_world_map_panel(engine::scene::UIScene& ui, const PanelState& state, float screen_w, float screen_h);

void build_gameplay_panels(engine::scene::UIScene& ui, const PanelState& state, float screen_w, float screen_h);

} // namespace mmo::client

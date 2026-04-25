#pragma once

#include "client/hud/hud_state.hpp"
#include "engine/scene/ui_scene.hpp"

#include <glm/glm.hpp>

namespace mmo::client::hud {

// Render floating world-space damage numbers — one per HUDState::damage_numbers
// entry. Numbers are projected through `view_projection`, fade out as their
// timer drops, and grow slightly as they fade for emphasis.
void build_damage_numbers(engine::scene::UIScene& ui, const HUDState& state, const glm::mat4& view_projection,
                          float screen_w, float screen_h);

// Stack of centered transient banners (level-up echoes, quest-complete prompts,
// etc.). Each notification fades out over its last 0.5s.
void build_notifications(engine::scene::UIScene& ui, const HUDState& state, float screen_w);

} // namespace mmo::client::hud

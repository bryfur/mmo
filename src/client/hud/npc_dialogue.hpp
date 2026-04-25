#pragma once

#include "client/hud/hud_state.hpp" // legacy NPCDialogueState lives in HUDState
#include "client/mouse_ui.hpp"
#include "client/network_message_handler.hpp" // NPCInteractionState
#include "engine/scene/ui_scene.hpp"

namespace mmo::client::hud {

// Modern quest-NPC dialogue popup (list view + detail view, accept/decline,
// drag-and-close). Drawn only when `state.showing_dialogue` is set.
void build_npc_dialogue(engine::scene::UIScene& ui, const NPCInteractionState& state, MouseUI& mui, float screen_w,
                        float screen_h);

// Legacy simple-text dialogue rendered when `hud.dialogue.visible` and the
// modern popup is not active. Bottom-of-screen panel with numbered options.
void build_legacy_dialogue(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);

} // namespace mmo::client::hud

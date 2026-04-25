#pragma once

#include "client/hud/hud_state.hpp"
#include "client/hud/panel_state.hpp"
#include "client/network_client.hpp"
#include "client/network_message_handler.hpp" // NPCInteractionState

#include <cstdint>
#include <entt/entt.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mmo::client::systems {

struct ClosestNPC {
    uint32_t network_id = 0;
    std::string name;
    float distance = 0.0f;
};

// Scan all entities in `registry` for the TownNPC nearest to (player_x, player_z)
// within `max_distance` world units (along the X/Z plane). Returns nullopt when
// no NPC is in range. Pure over the registry — no input handling, no
// component mutations — so it can be unit tested with a synthetic registry.
std::optional<ClosestNPC> find_closest_npc(const entt::registry& registry, float player_x, float player_z,
                                           float max_distance);

// Per-frame inputs to the NPC interaction handler. Bundled so callers can
// hand it everything in one struct without growing a 12-arg function.
struct NPCInteractionFrame {
    entt::registry& registry;
    NetworkClient& network;
    NPCInteractionState& npc_interaction;
    HUDState& hud;
    PanelState& panel;
    const std::unordered_map<uint32_t, entt::entity>& network_to_entity;
    const std::unordered_set<uint32_t>& npcs_with_turnins;
    uint32_t local_player_id = 0;

    // Edge-detected key state for this frame.
    bool interact_pressed = false;    // E (also accepts via dialogue)
    bool menu_toggle_pressed = false; // ESC
    bool key_w_just_pressed = false;  // up in quest list
    bool key_s_just_pressed = false;  // down in quest list
    bool key_enter_just_pressed = false;
    bool key_q_just_pressed = false; // back / decline
};

// Single entry-point that drives the NPC dialogue state machine for a frame:
//   - opens the dialog when E is pressed near a TownNPC
//   - drives W/S/Enter/Q navigation while open
//   - auto-turns-in completed quests
//   - sends QuestAccept and tracks accepted quests
// Pulled out of Game::update_playing so it can be reasoned about independently.
void update_npc_interaction(NPCInteractionFrame& f);

} // namespace mmo::client::systems

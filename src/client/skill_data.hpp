#pragma once

#include "client/hud/hud_state.hpp"

namespace mmo::client {

// Populate the local player's HUD skill bar with the default 5-skill loadout
// for the given class index (0=warrior, 1=mage, 2=paladin, 3=archer).
// Temporary fallback until the server sends authoritative skill data.
void populate_default_skill_bar(HUDState& hud, int class_index);

} // namespace mmo::client

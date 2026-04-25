#pragma once

// Persistent in-game HUD widgets — bars, skill bar, quest tracker, minimap,
// status effects, chat / vendor / party windows. Each free function reads
// HUDState and writes into a UIScene; some also register hit regions on a
// MouseUI for click handling. Widget *implementations* live in
// hud/gameplay_hud.cpp (bars/skill_bar/etc.) plus hud/chat_window.cpp,
// hud/vendor_window.cpp, hud/party_widgets.cpp.

#include "engine/scene/ui_scene.hpp"
#include "client/hud/hud_state.hpp"
#include "client/mouse_ui.hpp"

namespace mmo::client {

void build_health_bar(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_xp_bar(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_mana_bar(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_gold_display(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_skill_bar(engine::scene::UIScene& ui, const HUDState& hud, MouseUI& mui, float screen_w, float screen_h);
void build_quest_tracker(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_zone_name(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_level_up_notification(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_loot_feed(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_minimap(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_chat_window(engine::scene::UIScene& ui, const HUDState& hud, MouseUI& mui, float screen_w, float screen_h);
void build_vendor_window(engine::scene::UIScene& ui, const HUDState& hud, MouseUI& mui, float screen_w, float screen_h);
void build_party_frames(engine::scene::UIScene& ui, const HUDState& hud, MouseUI& mui, float screen_w, float screen_h);
void build_status_effect_row(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_party_invite_popup(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);

// Master entry point — calls each of the above in the right z-order.
void build_gameplay_hud(engine::scene::UIScene& ui, const HUDState& hud, MouseUI& mui, float screen_w, float screen_h);

} // namespace mmo::client

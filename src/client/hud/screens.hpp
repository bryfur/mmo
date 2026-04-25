#pragma once

#include "engine/scene/ui_scene.hpp"
#include "protocol/protocol.hpp"

#include <string>
#include <vector>

namespace mmo::client::hud {

// ---------------------------------------------------------------------------
// Pre-game screens (class select, connecting). Pure UI builders — no Game
// pointer, just the inputs they need.
// ---------------------------------------------------------------------------

void build_class_select(engine::scene::UIScene& ui, const std::vector<protocol::ClassInfo>& classes, int selected_index,
                        float screen_w, float screen_h);

void build_connecting(engine::scene::UIScene& ui, const std::string& host, uint16_t port, float connecting_timer,
                      float screen_w, float screen_h);

// Reticle dot + cross drawn at screen center for ranged classes.
void build_reticle(engine::scene::UIScene& ui, float screen_w, float screen_h);

// Local-player health bar (HP %d/%d) at bottom-left.
void build_player_health_bar(engine::scene::UIScene& ui, float current, float max, float screen_w, float screen_h);

// Full-screen "YOU DIED — press SPACE to respawn" overlay.
void build_death_overlay(engine::scene::UIScene& ui, float screen_w, float screen_h);

} // namespace mmo::client::hud

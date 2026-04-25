#pragma once

#include "engine/scene/ui_scene.hpp"
#include "engine/render_stats.hpp"
#include "client/network_client.hpp"

#include <string>

namespace mmo::client::hud {

struct DebugHUDInputs {
    const engine::RenderStats& render_stats;
    const NetworkClient::NetworkStats& network_stats;
    const std::string& gpu_driver_name;
    int screen_width = 0;
    int screen_height = 0;
    float fps = 0.0f;
    bool show_fps = false;
    bool show_debug = false;
};

// Top-left FPS + debug overlay. Skips drawing entirely when both `show_fps`
// and `show_debug` are false.
void build_debug_hud(engine::scene::UIScene& ui, const DebugHUDInputs& in);

} // namespace mmo::client::hud

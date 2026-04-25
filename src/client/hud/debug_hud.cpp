#include "debug_hud.hpp"
#include "client/ui_colors.hpp"

#include <cstdio>

namespace mmo::client::hud {

using engine::scene::UIScene;

void build_debug_hud(UIScene& ui, const DebugHUDInputs& in) {
    if (!in.show_fps && !in.show_debug) return;

    if (in.show_fps) {
        char fps_text[32];
        std::snprintf(fps_text, sizeof(fps_text), "FPS: %.0f", in.fps);
        ui.add_text(fps_text, 10.0f, 10.0f, 1.0f, engine::ui_colors::FPS_TEXT);
    }

    if (!in.show_debug) return;

    const float x = 10.0f;
    float y = in.show_fps ? 30.0f : 10.0f;
    const float line_h = 18.0f;
    const uint32_t color = engine::ui_colors::FPS_TEXT;

    ui.add_filled_rect(x - 5.0f, y - 5.0f, 420.0f, 5.0f * line_h + 10.0f, 0x80000000);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "GPU: %s %dx%d | Draws: %u | Tris: %uK",
                  in.gpu_driver_name.c_str(), in.screen_width, in.screen_height,
                  in.render_stats.draw_calls, in.render_stats.triangle_count / 1000);
    ui.add_text(buf, x, y, 0.8f, color);
    y += line_h;

    std::snprintf(buf, sizeof(buf),
                  "Entities: %u rendered, %u dist culled, %u frustum culled",
                  in.render_stats.entities_rendered,
                  in.render_stats.entities_distance_culled,
                  in.render_stats.entities_frustum_culled);
    ui.add_text(buf, x, y, 0.8f, color);
    y += line_h;

    std::snprintf(buf, sizeof(buf), "Frame: %.1f ms (%.0f FPS)",
                  in.fps > 0.0f ? 1000.0f / in.fps : 0.0f, in.fps);
    ui.add_text(buf, x, y, 0.8f, color);
    y += line_h;

    std::snprintf(buf, sizeof(buf), "Net: %.1f KB/s up, %.1f KB/s down",
                  in.network_stats.bytes_sent_per_sec / 1024.0f,
                  in.network_stats.bytes_recv_per_sec / 1024.0f);
    ui.add_text(buf, x, y, 0.8f, color);
    y += line_h;

    std::snprintf(buf, sizeof(buf),
                  "Net: %.0f pkt/s up, %.0f pkt/s down, queue: %u",
                  in.network_stats.packets_sent_per_sec,
                  in.network_stats.packets_recv_per_sec,
                  in.network_stats.message_queue_size);
    ui.add_text(buf, x, y, 0.8f, color);
}

} // namespace mmo::client::hud

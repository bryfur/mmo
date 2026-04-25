#include "screens.hpp"
#include "client/ui_colors.hpp"
#include "client/hud/hud_layout.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace mmo::client::hud {

using engine::scene::UIScene;
using namespace engine::ui_colors;

void build_class_select(UIScene& ui,
                        const std::vector<protocol::ClassInfo>& classes,
                        int selected_index,
                        float screen_w, float screen_h) {
    if (classes.empty()) return;

    const float center_x = screen_w * 0.5f;
    const float center_y = screen_h * 0.5f;
    const int num_classes = static_cast<int>(classes.size());

    ui.add_filled_rect(0, 0, screen_w, 100.0f, TITLE_BG);

    auto draw_centered = [&](const char* text, float y, float scale, uint32_t color) {
        const float w = static_cast<float>(std::strlen(text)) * 8.0f * scale;
        ui.add_text(text, center_x - w * 0.5f, y, scale, color);
    };

    draw_centered("SELECT YOUR CLASS", 30.0f, 2.0f, WHITE);
    draw_centered("Use A/D to select, SPACE to confirm", 70.0f, 1.0f, TEXT_DIM);

    const float box_size = 120.0f;
    const float spacing = 150.0f;
    const float start_x = center_x - spacing * (num_classes - 1) * 0.5f;
    const float box_y = center_y - 50.0f;
    const float half = box_size * 0.5f;

    for (int i = 0; i < num_classes; ++i) {
        const auto& cls = classes[i];
        const float x = start_x + i * spacing;
        const bool selected = (i == selected_index);

        if (selected) {
            ui.add_filled_rect(x - half - 10.0f, box_y - half - 10.0f,
                               box_size + 20.0f, box_size + 20.0f, SELECTION);
            ui.add_rect_outline(x - half - 10.0f, box_y - half - 10.0f,
                                box_size + 20.0f, box_size + 20.0f, WHITE, 3.0f);
        }

        ui.add_filled_rect(x - half, box_y - half, box_size, box_size, cls.select_color);
        ui.add_filled_rect(x - half, box_y - half, box_size, box_size, cls.color);
        ui.add_rect_outline(x - half, box_y - half, box_size, box_size, WHITE, 2.0f);

        const uint32_t text_color = selected ? WHITE : TEXT_DIM;
        const float name_w = static_cast<float>(std::strlen(cls.name)) * 8.0f;
        const float desc_w = static_cast<float>(std::strlen(cls.short_desc)) * 8.0f * 0.8f;
        ui.add_text(cls.name,       x - name_w * 0.5f, box_y + half + 15.0f, 1.0f, text_color);
        ui.add_text(cls.short_desc, x - desc_w * 0.5f, box_y + half + 40.0f, 0.8f, TEXT_DIM);
    }

    if (selected_index < 0 || selected_index >= num_classes) return;
    const auto& sel = classes[selected_index];
    ui.add_filled_rect(center_x - 200.0f, screen_h - 120.0f, 400.0f, 80.0f, PANEL_BG);
    ui.add_rect_outline(center_x - 200.0f, screen_h - 120.0f, 400.0f, 80.0f, sel.select_color, 2.0f);

    const float d1 = static_cast<float>(std::strlen(sel.desc_line1)) * 8.0f * 0.9f;
    const float d2 = static_cast<float>(std::strlen(sel.desc_line2)) * 8.0f * 0.9f;
    ui.add_text(sel.desc_line1, center_x - d1 * 0.5f, screen_h - 105.0f, 0.9f, WHITE);
    ui.add_text(sel.desc_line2, center_x - d2 * 0.5f, screen_h - 80.0f,  0.9f, WHITE);

    const char* hint = "Press ESC anytime to open Settings Menu";
    const float hint_w = static_cast<float>(std::strlen(hint)) * 8.0f * 0.8f;
    ui.add_text(hint, center_x - hint_w * 0.5f, screen_h - 25.0f, 0.8f, TEXT_HINT);
}

void build_connecting(UIScene& ui,
                      const std::string& host,
                      uint16_t port,
                      float connecting_timer,
                      float screen_w, float screen_h) {
    const float center_x = screen_w * 0.5f;
    const float center_y = screen_h * 0.5f;

    ui.add_filled_rect(center_x - 200.0f, center_y - 100.0f, 400.0f, 200.0f, PANEL_BG);
    ui.add_rect_outline(center_x - 200.0f, center_y - 100.0f, 400.0f, 200.0f, WHITE, 2.0f);

    ui.add_text("CONNECTING", center_x - 80.0f, center_y - 80.0f, 1.5f, WHITE);

    // Spinner: 8 dots ramping in alpha around a circle.
    constexpr int kDots = 8;
    constexpr float kRadius = 40.0f;
    constexpr float kDotRadius = 8.0f;
    const float angle_offset = connecting_timer * 3.0f;

    for (int i = 0; i < kDots; ++i) {
        const float angle = angle_offset + (i / static_cast<float>(kDots)) * 2.0f * 3.14159f;
        const float x = center_x + std::cos(angle) * kRadius;
        const float y = center_y + std::sin(angle) * kRadius;
        const uint8_t alpha = static_cast<uint8_t>(255.0f * (i + 1) / static_cast<float>(kDots));
        ui.add_filled_rect(x - kDotRadius, y - kDotRadius,
                           kDotRadius * 2.0f, kDotRadius * 2.0f,
                           0x00FFFFFFu | (static_cast<uint32_t>(alpha) << 24));
    }

    const std::string msg = "Connecting to " + host + ":" + std::to_string(port);
    ui.add_text(msg, center_x - 120.0f, center_y + 60.0f, 0.8f, TEXT_DIM);
}

void build_reticle(UIScene& ui, float screen_w, float screen_h) {
    const float cx = screen_w * 0.5f;
    const float cy = screen_h * 0.5f;
    constexpr float outer = 12.0f, inner = 4.0f, dot = 2.0f;
    ui.add_line(cx, cy - outer, cx, cy - inner, 0xCCFFFFFF, 2.0f);
    ui.add_line(cx, cy + inner, cx, cy + outer, 0xCCFFFFFF, 2.0f);
    ui.add_line(cx - outer, cy, cx - inner, cy, 0xCCFFFFFF, 2.0f);
    ui.add_line(cx + inner, cy, cx + outer, cy, 0xCCFFFFFF, 2.0f);
    ui.add_filled_rect(cx - dot * 0.5f, cy - dot * 0.5f, dot, dot, 0xCCFFFFFF);
}

void build_player_health_bar(UIScene& ui, float current, float max,
                             float /*screen_w*/, float screen_h) {
    constexpr float bar_width = 250.0f;
    constexpr float bar_height = 25.0f;
    constexpr float padding = 20.0f;
    const float hx = padding;
    const float hy = screen_h - padding - bar_height;

    ui.add_filled_rect(hx - 2.0f, hy - 2.0f, bar_width + 4.0f, bar_height + 4.0f, HEALTH_FRAME);
    ui.add_rect_outline(hx - 2.0f, hy - 2.0f, bar_width + 4.0f, bar_height + 4.0f, BORDER, 2.0f);
    ui.add_filled_rect(hx, hy, bar_width, bar_height, HEALTH_BG);

    const float ratio = hud_layout::bar_ratio(current, max);
    uint32_t hp_color = HEALTH_LOW;
    if (ratio > 0.5f) hp_color = HEALTH_HIGH;
    else if (ratio > 0.25f) hp_color = HEALTH_MED;
    ui.add_filled_rect(hx, hy, bar_width * ratio, bar_height, hp_color);

    char hp_text[32];
    std::snprintf(hp_text, sizeof(hp_text), "HP: %.0f / %.0f", current, max);
    ui.add_text(hp_text, hx + 10.0f, hy + 5.0f, 1.0f, WHITE);
}

void build_death_overlay(UIScene& ui, float screen_w, float screen_h) {
    ui.add_filled_rect(0, 0, screen_w, screen_h, 0x88000000);
    const float cx = screen_w * 0.5f;
    const float cy = screen_h * 0.5f;
    ui.add_text("YOU DIED", cx - 64.0f, cy - 20.0f, 2.0f, 0xFF4444FF);
    ui.add_text("Press SPACE to respawn", cx - 100.0f, cy + 30.0f, 1.0f, TEXT_DIM);
}

} // namespace mmo::client::hud

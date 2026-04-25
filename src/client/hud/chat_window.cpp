#include "client/hud/widgets.hpp"

#include <string>

namespace mmo::client {

using engine::scene::UIScene;

namespace {

uint32_t chat_channel_color(uint8_t channel) {
    switch (channel) {
        case 0: return 0xFFCCCCCC; // Say - light gray
        case 1: return 0xFF88FFFF; // Zone - cyan
        case 2: return 0xFF00CCFF; // Global - yellow
        case 3: return 0xFF0088FF; // System - orange
        case 4: return 0xFFFF88FF; // Whisper - magenta
        default: return 0xFFCCCCCC;
    }
}

const char* chat_channel_prefix(uint8_t channel) {
    switch (channel) {
        case 0: return "[Say]";
        case 1: return "[Zone]";
        case 2: return "[Global]";
        case 3: return "[System]";
        case 4: return "[Whisper]";
        default: return "";
    }
}

} // namespace

void build_chat_window(UIScene& ui, const HUDState& hud, MouseUI& mui,
                      float /*screen_w*/, float screen_h) {
    const auto& chat = hud.chat;
    const float width = 460.0f;
    const float line_height = 14.0f;
    const int visible = ChatState::VISIBLE_LINES;
    const float height = visible * line_height + 10.0f
                       + (chat.input_active ? 22.0f : 0.0f) + 22.0f;

    // Default position sits at bottom-left, ABOVE the skill bar + health bar.
    // Skill bar occupies y=screen_h-80..screen_h; keep 12 px gap.
    const float default_y = screen_h - 92.0f - height;
    auto pos = mui.default_pos(WidgetId::TitleChat, 20.0f, default_y);
    const float x = pos.x;
    float y = pos.y;

    // Background + title bar
    ui.add_filled_rect(x, y, width, height, 0xAA000000);
    ui.add_rect_outline(x, y, width, height, 0xFF555555, 1.0f);
    ui.add_filled_rect(x, y, width, 18.0f, 0xCC222244);
    ui.add_text("Chat", x + 8, y + 3, 0.8f, 0xFFCCDDFF);
    mui.push_region(WidgetId::TitleChat, WidgetId::TitleChat, x, y, width, 18.0f);

    y += 18.0f;

    // Recent chat lines
    int start = static_cast<int>(chat.lines.size()) - visible;
    if (start < 0) start = 0;
    float ty = y + 6.0f;
    for (int i = start; i < static_cast<int>(chat.lines.size()); ++i) {
        const auto& line = chat.lines[i];
        const uint32_t color = chat_channel_color(line.channel);
        const std::string prefix = chat_channel_prefix(line.channel);
        std::string rendered = (line.channel == 3)
            ? prefix + " " + line.text
            : prefix + " " + line.sender + ": " + line.text;
        if (rendered.size() > 80) rendered = rendered.substr(0, 80) + "...";
        ui.add_text(rendered, x + 6, ty, 0.75f, color);
        ty += line_height;
    }

    // Input line
    if (chat.input_active) {
        const float iy = y + height - 20.0f;
        ui.add_filled_rect(x + 4, iy, width - 8, 18.0f, 0xDD222233);
        ui.add_rect_outline(x + 4, iy, width - 8, 18.0f, 0xFF666677, 1.0f);
        const std::string prefix = chat_channel_prefix(chat.selected_channel);
        const std::string display = prefix + " > " + chat.input_buffer + "_";
        ui.add_text(display, x + 8, iy + 3, 0.8f, 0xFFFFFFFF);
    } else {
        ui.add_text("[Enter] to chat", x + 6, y + height - 14.0f, 0.65f, 0xFF888899);
    }
}

} // namespace mmo::client

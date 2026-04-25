#include "client/ui_colors.hpp"
#include "panels.hpp"

#include <cstdio>

namespace mmo::client::hud {

using engine::scene::UIScene;
using namespace engine::ui_colors;

void build_quest_log_panel(UIScene& ui, const HUDState& hud, const PanelState& panel, MouseUI& mui, float screen_w,
                           float screen_h) {
    constexpr float w = 400.0f;
    constexpr float h = 400.0f;
    auto pos = mui.default_pos(WidgetId::TitleQuestLog, (screen_w - w) * 0.5f, (screen_h - h) * 0.5f);
    const float px = pos.x;
    const float py = pos.y;

    ui.add_filled_rect(px, py, w, h, 0xE0222222);
    ui.add_rect_outline(px, py, w, h, 0xFF888888, 2.0f);

    // Title bar (drag region) + close
    ui.add_filled_rect(px, py, w, 28.0f, 0xFF335533);
    ui.add_text("QUEST LOG", px + 10.0f, py + 5.0f, 1.0f, WHITE);
    constexpr float close_size = 22.0f;
    const float close_x = px + w - close_size - 4.0f;
    const float close_y = py + 3.0f;
    ui.add_filled_rect(close_x, close_y, close_size, close_size, 0xFF553344);
    ui.add_text("X", close_x + 7.0f, close_y + 4.0f, 0.9f, 0xFFFFFFFF);
    mui.push_region(WidgetId::TitleQuestLog, WidgetId::TitleQuestLog, px, py, w - close_size - 8.0f, 28.0f);
    mui.push_region(WidgetId::CloseQuestLog, WidgetId::TitleQuestLog, close_x, close_y, close_size, close_size);
    ui.add_text("[L/ESC] Close  [X] Abandon", px + 10.0f, py + h - 22.0f, 0.6f, TEXT_HINT);

    if (hud.tracked_quests.empty()) {
        ui.add_text("No active quests.", px + 20.0f, py + 60.0f, 0.9f, TEXT_DIM);
        return;
    }

    float qy = py + 40.0f;
    for (int i = 0; i < static_cast<int>(hud.tracked_quests.size()); ++i) {
        const auto& quest = hud.tracked_quests[i];
        const bool selected = (i == panel.quest_cursor);

        const float entry_h = 20.0f + static_cast<float>(quest.objectives.size()) * 16.0f + 8.0f;
        if (qy + entry_h > py + h - 30.0f) {
            break;
        }

        if (selected) {
            ui.add_filled_rect(px + 5.0f, qy, w - 10.0f, entry_h, 0x40FFFFFF);
        }

        ui.add_text(quest.quest_name, px + 15.0f, qy + 2.0f, 0.9f, selected ? 0xFFFFCC00 : WHITE);
        qy += 22.0f;

        for (const auto& obj : quest.objectives) {
            char progress[80];
            std::snprintf(progress, sizeof(progress), "  - %s: %d/%d", obj.description.c_str(), obj.current,
                          obj.required);
            ui.add_text(progress, px + 20.0f, qy, 0.7f, obj.complete ? 0xFF00FF00 : 0xFFCCCCCC);
            qy += 16.0f;
        }
        qy += 8.0f;
    }
}

} // namespace mmo::client::hud

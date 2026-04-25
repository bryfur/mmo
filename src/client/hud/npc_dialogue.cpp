#include "npc_dialogue.hpp"
#include "client/ui_colors.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace mmo::client::hud {

using engine::scene::UIScene;
using namespace engine::ui_colors;

namespace {

void build_quest_list(UIScene& ui, const NPCInteractionState& state,
                      MouseUI& mui,
                      float px, float py, float pw, float ph) {
    if (state.available_quests.empty()) {
        ui.add_text("No quests available.", px + 20.0f, py + 60.0f, 1.0f, 0xFF888888);
        return;
    }

    ui.add_text("Available Quests:", px + 20.0f, py + 50.0f, 1.0f, 0xFFCCCCCC);

    float qy = py + 80.0f;
    for (int i = 0; i < static_cast<int>(state.available_quests.size()); ++i) {
        const auto& quest = state.available_quests[i];
        const bool selected = (i == state.selected_quest);

        if (selected) {
            ui.add_filled_rect(px + 10.0f, qy - 2.0f, pw - 20.0f, 28.0f, 0x40FFFFFF);
        }

        ui.add_text(quest.quest_name, px + 25.0f, qy + 2.0f, 1.0f,
                    selected ? 0xFFFFFFFF : 0xFFCCCCCC);

        char reward_text[64];
        std::snprintf(reward_text, sizeof(reward_text),
                      "XP: %d  Gold: %d", quest.xp_reward, quest.gold_reward);
        ui.add_text(reward_text, px + pw - 180.0f, qy + 4.0f, 0.7f, 0xFF00DDFF);

        mui.push_region(quest_row_id(i), WidgetId::TitleDialogue,
                        px + 10.0f, qy - 2.0f, pw - 20.0f, 28.0f);
        qy += 32.0f;
    }

    ui.add_text("Click to view - [W/S] Navigate  [ENTER] View Quest",
                px + 20.0f, py + ph - 35.0f, 0.8f, 0xFF888888);
}

void build_quest_detail(UIScene& ui, const NPCInteractionState& state,
                        float px, float py, float pw, float ph) {
    const auto& quest = state.available_quests[state.selected_quest];
    const float text_w = pw - 40.0f;
    const float btn_y = py + ph - 50.0f;

    float oy = py + 50.0f;
    oy += ui.add_text_wrapped(quest.quest_name, px + 20.0f, oy, text_w, 1.2f, 0xFF00DDFF);
    oy += 4.0f;
    oy += ui.add_text_wrapped(quest.dialogue, px + 20.0f, oy, text_w, 0.8f, 0xFFCCCCCC);
    oy += 4.0f;
    oy += ui.add_text_wrapped(quest.description, px + 20.0f, oy, text_w, 0.85f, 0xFFAAAAAA);
    oy += 8.0f;

    ui.add_text("Objectives:", px + 20.0f, oy, 1.0f, 0xFFFFFFFF);
    oy += 22.0f;
    for (const auto& obj : quest.objectives) {
        if (oy + 16.0f > btn_y - 60.0f) break;  // Reserve space for rewards + buttons.
        char obj_text[128];
        std::snprintf(obj_text, sizeof(obj_text), "- %s (%d)",
                      obj.description.c_str(), obj.count);
        oy += ui.add_text_wrapped(std::string(obj_text), px + 30.0f, oy,
                                  text_w - 10.0f, 0.85f, 0xFFCCCCCC);
    }

    oy = std::max(oy + 8.0f, btn_y - 50.0f);
    ui.add_text("Rewards:", px + 20.0f, oy, 1.0f, 0xFFFFFFFF);
    oy += 22.0f;
    char reward_buf[64];
    std::snprintf(reward_buf, sizeof(reward_buf),
                  "XP: %d   Gold: %d", quest.xp_reward, quest.gold_reward);
    ui.add_text(reward_buf, px + 30.0f, oy, 0.9f, 0xFF00DDFF);

    // Accept / decline buttons.
    ui.add_filled_rect(px + pw * 0.5f - 170.0f, btn_y, 160.0f, 35.0f, 0xFF004400);
    ui.add_rect_outline(px + pw * 0.5f - 170.0f, btn_y, 160.0f, 35.0f, 0xFF00CC00, 2.0f);
    ui.add_text("Accept [E/Enter]", px + pw * 0.5f - 155.0f, btn_y + 8.0f, 0.9f, 0xFF00FF00);

    ui.add_filled_rect(px + pw * 0.5f + 20.0f, btn_y, 140.0f, 35.0f, 0xFF440000);
    ui.add_rect_outline(px + pw * 0.5f + 20.0f, btn_y, 140.0f, 35.0f, 0xFFCC0000, 2.0f);
    ui.add_text("Decline [Q]", px + pw * 0.5f + 42.0f, btn_y + 8.0f, 0.9f, 0xFFFF4444);
}

} // namespace

void build_npc_dialogue(UIScene& ui,
                        const NPCInteractionState& state,
                        MouseUI& mui,
                        float screen_w, float screen_h) {
    if (!state.showing_dialogue) return;

    constexpr float pw = 500.0f;
    constexpr float ph = 400.0f;
    auto pos = mui.default_pos(WidgetId::TitleDialogue,
                               (screen_w - pw) * 0.5f,
                               (screen_h - ph) * 0.5f);
    const float px = pos.x;
    const float py = pos.y;

    ui.add_filled_rect(px, py, pw, ph, 0xEE1a1a2e);
    ui.add_rect_outline(px, py, pw, ph, 0xFF00BBFF, 2.0f);

    // NPC name header (drag handle)
    ui.add_filled_rect(px, py, pw, 35.0f, 0xFF332211);
    ui.add_text(state.npc_name, px + 15.0f, py + 8.0f, 1.2f, 0xFF00DDFF);

    // Close button (X)
    constexpr float cs = 24.0f;
    const float cx_btn = px + pw - cs - 6.0f;
    const float cy_btn = py + 5.0f;
    ui.add_filled_rect(cx_btn, cy_btn, cs, cs, 0xFF553344);
    ui.add_text("X", cx_btn + 8.0f, cy_btn + 5.0f, 0.95f, 0xFFFFFFFF);

    mui.push_region(WidgetId::TitleDialogue, WidgetId::TitleDialogue,
                    px, py, pw - cs - 10.0f, 35.0f);
    mui.push_region(WidgetId::CloseDialogue, WidgetId::TitleDialogue,
                    cx_btn, cy_btn, cs, cs);

    if (state.showing_quest_detail) {
        build_quest_detail(ui, state, px, py, pw, ph);
    } else {
        build_quest_list(ui, state, mui, px, py, pw, ph);
    }
}

void build_legacy_dialogue(UIScene& ui, const HUDState& hud,
                           float screen_w, float screen_h) {
    const auto& d = hud.dialogue;
    if (!d.visible) return;

    constexpr float w = 500.0f;
    constexpr float h = 250.0f;
    const float x = (screen_w - w) * 0.5f;
    const float y = screen_h - h - 80.0f;

    ui.add_filled_rect(x, y, w, h, 0xE0222222);
    ui.add_rect_outline(x, y, w, h, 0xFF888888, 2.0f);

    // NPC name header
    ui.add_filled_rect(x, y, w, 28.0f, 0xFF443322);
    ui.add_text(d.npc_name, x + 10.0f, y + 5.0f, 1.0f, 0xFFFFCC00);

    ui.add_text_wrapped(d.dialogue, x + 15.0f, y + 40.0f, w - 30.0f, 0.8f, WHITE);

    float option_y = y + 100.0f;
    for (int i = 0; i < d.quest_count; ++i) {
        const bool selected = (i == d.selected_option);
        const uint32_t color = selected ? 0xFFFFFF00 : 0xFFCCCCCC;
        const uint32_t bg = selected ? 0x40FFFFFF : 0x00000000;

        ui.add_filled_rect(x + 10.0f, option_y, w - 20.0f, 22.0f, bg);
        const std::string option = "[" + std::to_string(i + 1) + "] " + d.quest_names[i];
        ui.add_text(option, x + 15.0f, option_y + 3.0f, 0.8f, color);
        option_y += 26.0f;
    }

    ui.add_text("[E/ESC] Close    [Enter] Accept",
                x + 10.0f, y + h - 25.0f, 0.7f, TEXT_HINT);
}

} // namespace mmo::client::hud

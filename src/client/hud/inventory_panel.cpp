#include "client/ui_colors.hpp"
#include "panels.hpp"

#include <cstdio>

namespace mmo::client::hud {

using engine::scene::UIScene;
using namespace engine::ui_colors;

void build_inventory_panel(UIScene& ui, const PanelState& panel, MouseUI& mui, float screen_w, float screen_h) {
    constexpr float w = 350.0f;
    constexpr float h = 500.0f;
    auto pos = mui.default_pos(WidgetId::TitleInventory, (screen_w - w) * 0.5f, (screen_h - h) * 0.5f);
    const float px = pos.x;
    const float py = pos.y;

    ui.add_filled_rect(px, py, w, h, 0xE0222222);
    ui.add_rect_outline(px, py, w, h, 0xFF888888, 2.0f);

    // Title bar (drag region) + close
    ui.add_filled_rect(px, py, w, 28.0f, 0xFF334455);
    ui.add_text("INVENTORY", px + 10.0f, py + 5.0f, 1.0f, WHITE);
    constexpr float close_size = 22.0f;
    const float close_x = px + w - close_size - 4.0f;
    const float close_y = py + 3.0f;
    ui.add_filled_rect(close_x, close_y, close_size, close_size, 0xFF553344);
    ui.add_text("X", close_x + 7.0f, close_y + 4.0f, 0.9f, 0xFFFFFFFF);
    mui.push_region(WidgetId::TitleInventory, WidgetId::TitleInventory, px, py, w - close_size - 8.0f, 28.0f);
    mui.push_region(WidgetId::CloseInventory, WidgetId::TitleInventory, close_x, close_y, close_size, close_size);

    // Equipped section
    float ey = py + 35.0f;
    ui.add_text("Equipped:", px + 10.0f, ey, 0.8f, 0xFFCCCCCC);
    ey += 18.0f;

    char equip_text[64];
    std::snprintf(equip_text, sizeof(equip_text), "[1] Weapon: %s",
                  panel.equipped_weapon > 0 ? item_name(panel.equipped_weapon) : "None");
    ui.add_text(equip_text, px + 15.0f, ey, 0.7f, panel.equipped_weapon > 0 ? 0xFF66AAFF : TEXT_DIM);
    ey += 16.0f;

    std::snprintf(equip_text, sizeof(equip_text), "[2] Armor: %s",
                  panel.equipped_armor > 0 ? item_name(panel.equipped_armor) : "None");
    ui.add_text(equip_text, px + 15.0f, ey, 0.7f, panel.equipped_armor > 0 ? 0xFF66AAFF : TEXT_DIM);
    ey += 22.0f;

    ui.add_line(px + 10.0f, ey, px + w - 10.0f, ey, 0xFF444444, 1.0f);
    ey += 8.0f;

    ui.add_text("Backpack:  [Enter] Equip  [U] Use", px + 10.0f, ey, 0.65f, TEXT_HINT);
    ey += 16.0f;

    // Backpack slots
    for (int i = 0; i < PanelState::MAX_INVENTORY_SLOTS; ++i) {
        const bool selected = (i == panel.inventory_cursor);
        const float slot_y = ey + i * 20.0f;
        if (slot_y + 20.0f > py + h - 5.0f) {
            break;
        }

        if (selected) {
            ui.add_filled_rect(px + 5.0f, slot_y, w - 10.0f, 18.0f, 0x40FFFFFF);
        }

        const auto& slot = panel.inventory_slots[i];
        if (slot.empty()) {
            ui.add_text("---", px + 15.0f, slot_y + 2.0f, 0.7f, 0xFF555555);
        } else {
            char item_text[64];
            std::snprintf(item_text, sizeof(item_text), "%s x%d", item_name(slot.item_id), slot.count);
            ui.add_text(item_text, px + 15.0f, slot_y + 2.0f, 0.7f, selected ? WHITE : 0xFFCCCCCC);
        }
        mui.push_region(inventory_slot_id(i), WidgetId::TitleInventory, px + 5.0f, slot_y, w - 10.0f, 18.0f);
    }
}

} // namespace mmo::client::hud

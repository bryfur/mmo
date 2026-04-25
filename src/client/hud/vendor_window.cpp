#include "client/hud/widgets.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace mmo::client {

using engine::scene::UIScene;

void build_vendor_window(UIScene& ui, const HUDState& hud, MouseUI& mui,
                        float screen_w, float screen_h) {
    const auto& v = hud.vendor;
    if (!v.visible) return;

    const float w = 460.0f;
    const float h = 360.0f;
    const float default_x = (screen_w - w) * 0.5f;
    const float default_y = (screen_h - h) * 0.5f;
    auto pos = mui.default_pos(WidgetId::TitleVendor, default_x, default_y);
    const float x = pos.x;
    const float y = pos.y;

    // Panel background
    ui.add_filled_rect(x, y, w, h, 0xEE1A1A22);
    ui.add_rect_outline(x, y, w, h, 0xFF999999, 2.0f);

    // Title bar (drag region)
    ui.add_filled_rect(x, y, w, 26.0f, 0xFF222244);
    const std::string title = v.vendor_name.empty() ? std::string("Vendor") : v.vendor_name;
    ui.add_text(title, x + 12, y + 6, 1.0f, 0xFFFFEECC);

    // Close button (top-right X)
    const float close_size = 20.0f;
    const float close_x = x + w - close_size - 4.0f;
    const float close_y = y + 3.0f;
    ui.add_filled_rect(close_x, close_y, close_size, close_size, 0xFF553344);
    ui.add_text("X", close_x + 6, close_y + 3, 0.9f, 0xFFFFFFFF);
    mui.push_region(WidgetId::TitleVendor, WidgetId::TitleVendor,
                    x, y, w - close_size - 8.0f, 26.0f);
    mui.push_region(WidgetId::CloseVendor, WidgetId::TitleVendor,
                    close_x, close_y, close_size, close_size);

    // Mode tab
    const float tab_y = y + 34.0f;
    const std::string tab_label = v.buying ? "BUY  (Tab to Sell)" : "SELL  (Tab to Buy)";
    ui.add_filled_rect(x + 10, tab_y - 2, 190.0f, 18.0f, 0xFF2A2A55);
    ui.add_text(tab_label, x + 14, tab_y, 0.8f, v.buying ? 0xFF00DDFF : 0xFF66FFFF);
    mui.push_region(WidgetId::VendorTab, WidgetId::TitleVendor,
                    x + 10, tab_y - 2, 190.0f, 18.0f);

    // Gold readout
    char gold_buf[48];
    std::snprintf(gold_buf, sizeof(gold_buf), "Gold: %d", hud.gold);
    ui.add_text(gold_buf, x + w - 120, tab_y, 0.85f, 0xFF00DDFF);

    // List of items
    const float list_y = y + 58.0f;
    const float row_h = 22.0f;
    const int max_rows = 12;
    const int count = static_cast<int>(v.stock.size());
    const int cursor = std::max(0, std::min(v.cursor, count - 1));

    for (int i = 0; i < count && i < max_rows; ++i) {
        const float row_y = list_y + i * row_h;
        const bool selected = (i == cursor);
        const bool hovered = mui.mouse_x >= x + 10 && mui.mouse_x <= x + w - 10
                          && mui.mouse_y >= row_y && mui.mouse_y <= row_y + row_h - 2;
        const uint32_t row_color =
            selected ? 0xFF334466 : (hovered ? 0xFF2A3A4A : 0xFF222233);
        ui.add_filled_rect(x + 10, row_y, w - 20, row_h - 2, row_color);

        const auto& slot = v.stock[i];
        uint32_t name_color = 0xFFCCCCCC;
        if (slot.rarity == "uncommon")       name_color = 0xFF00CC00;
        else if (slot.rarity == "rare")      name_color = 0xFF0088FF;
        else if (slot.rarity == "epic")      name_color = 0xFFCC00CC;
        else if (slot.rarity == "legendary") name_color = 0xFF00AAFF;

        char buf[128];
        if (slot.stock < 0) {
            std::snprintf(buf, sizeof(buf), "%s", slot.item_name.c_str());
        } else {
            std::snprintf(buf, sizeof(buf), "%s (stock: %d)",
                          slot.item_name.c_str(), slot.stock);
        }
        ui.add_text(buf, x + 18, row_y + 4, 0.85f, name_color);

        char price_buf[32];
        std::snprintf(price_buf, sizeof(price_buf), "%d g", slot.price);
        ui.add_text(price_buf, x + w - 70, row_y + 4, 0.85f, 0xFF00DDFF);
        mui.push_region(vendor_row_id(i), WidgetId::TitleVendor,
                        x + 10, row_y, w - 20, row_h - 2);
    }

    ui.add_text("Click to purchase  -  Drag title to move  -  Esc to close",
                x + 12, y + h - 22, 0.7f, 0xFFAAAAAA);
}

} // namespace mmo::client

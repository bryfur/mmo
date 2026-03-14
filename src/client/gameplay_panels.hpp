#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mmo::client {

// Item data for inventory display
struct ItemSlot {
    uint16_t item_id = 0;
    uint16_t count = 0;
    bool empty() const { return item_id == 0; }
};

// Simple item name lookup (client-side, by item_id)
inline const char* item_name(uint16_t id) {
    switch (id) {
        case 1: return "Iron Sword";
        case 2: return "Steel Sword";
        case 3: return "Leather Armor";
        case 4: return "Chain Mail";
        case 5: return "Health Potion";
        case 6: return "Mana Potion";
        case 7: return "Wolf Pelt";
        case 8: return "Boar Tusk";
        case 9: return "Spider Silk";
        case 10: return "Goblin Ear";
        default: return "Unknown Item";
    }
}

// Which panel is currently open (only one at a time)
enum class ActivePanel : uint8_t {
    None = 0,
    Inventory,
    Talents,
    QuestLog,
};

// Talent node for display
struct TalentNode {
    uint16_t talent_id = 0;
    std::string name;
    std::string description;
    bool unlocked = false;
    bool available = false;   // Has prerequisites met
    int row = 0;
    int col = 0;
};

// Panel state for all gameplay panels
struct PanelState {
    ActivePanel active_panel = ActivePanel::None;

    // Inventory
    static constexpr int MAX_INVENTORY_SLOTS = 20;
    ItemSlot inventory_slots[MAX_INVENTORY_SLOTS] = {};
    uint16_t equipped_weapon = 0;
    uint16_t equipped_armor = 0;
    int inventory_cursor = 0;

    // Talents
    uint8_t talent_points = 0;
    std::vector<uint16_t> unlocked_talents;
    int talent_cursor = 0;

    // Quest log
    int quest_cursor = 0;

    // Toggle helpers
    void toggle_panel(ActivePanel panel) {
        if (active_panel == panel) {
            active_panel = ActivePanel::None;
        } else {
            active_panel = panel;
        }
    }

    bool is_panel_open() const { return active_panel != ActivePanel::None; }
};

} // namespace mmo::client

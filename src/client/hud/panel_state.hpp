#pragma once

// Modal panel state types — inventory, talent tree, quest log, world-map.
// Builder declarations live in client/hud/panels.hpp.

#include <cstdint>
#include <string>
#include <vector>

namespace mmo::client {

// ============================================================================
// Data structures (client branch - compact types for network/panel state)
// ============================================================================

// Item data for inventory display
struct ItemSlot {
    uint16_t item_id = 0;
    uint16_t count = 0;
    bool empty() const { return item_id == 0; }
};

// Simple item name lookup (client-side, by item_id)
inline const char* item_name(uint16_t id) {
    switch (id) {
        case 1:
            return "Iron Sword";
        case 2:
            return "Steel Sword";
        case 3:
            return "Leather Armor";
        case 4:
            return "Chain Mail";
        case 5:
            return "Health Potion";
        case 6:
            return "Mana Potion";
        case 7:
            return "Wolf Pelt";
        case 8:
            return "Boar Tusk";
        case 9:
            return "Spider Silk";
        case 10:
            return "Goblin Ear";
        default:
            return "Unknown Item";
    }
}

// Which panel is currently open (only one at a time)
enum class ActivePanel : uint8_t {
    None = 0,
    Inventory,
    Talents,
    QuestLog,
    WorldMap,
};

// Talent node for display
struct TalentNode {
    uint16_t talent_id = 0;
    std::string name;
    std::string description;
    bool unlocked = false;
    bool available = false; // Has prerequisites met
    int row = 0;
    int col = 0;
};

// ============================================================================
// Data structures (server branch - for panel rendering)
// ============================================================================

struct InventorySlotDisplay {
    std::string item_name;
    std::string item_id;
    int count = 0;
    std::string rarity = "common";
    bool is_equipped_weapon = false;
    bool is_equipped_armor = false;
};

struct QuestLogEntry {
    std::string quest_id;
    std::string name;
    std::string description;
    struct Objective {
        std::string description;
        int current = 0;
        int required = 0;
        bool complete = false;
    };
    std::vector<Objective> objectives;
    bool all_complete = false;
};

struct TalentDisplay {
    std::string id;
    std::string name;
    std::string description;
    int tier = 1;
    bool unlocked = false;
    bool available = false;
    std::string prerequisite;
};

struct TalentBranchDisplay {
    std::string name;
    std::string description;
    std::vector<TalentDisplay> talents;
};

struct MapQuestMarker {
    std::string quest_name;
    float world_x = 0, world_z = 0;
    float radius = 100;
    bool complete = false;
};

// Panel state for all gameplay panels
struct PanelState {
    // Panel switching (only one panel open at a time)
    ActivePanel active_panel = ActivePanel::None;

    int inventory_selected = -1;
    int quest_log_selected = 0;
    int talent_selected = -1;

    // Inventory data (server branch - string-based)
    std::vector<InventorySlotDisplay> inventory_slots_display;
    InventorySlotDisplay equipped_weapon_display;
    InventorySlotDisplay equipped_armor_display;
    int gold = 0;

    // Inventory data (client branch - compact)
    static constexpr int MAX_INVENTORY_SLOTS = 20;
    ItemSlot inventory_slots[MAX_INVENTORY_SLOTS] = {};
    uint16_t equipped_weapon = 0;
    uint16_t equipped_armor = 0;
    int inventory_cursor = 0;

    // Quest data (server branch)
    std::vector<QuestLogEntry> quest_entries;

    // Talent data (server branch)
    std::vector<TalentBranchDisplay> talent_branches;
    int talent_points_display = 0;

    // Talent data (client branch)
    uint8_t talent_points = 0;
    std::vector<std::string> unlocked_talents; // String IDs from server
    int talent_cursor = 0;

    // Talent tree definition (received from server)
    struct ClientTalent {
        std::string id;
        std::string name;
        std::string description;
        int tier = 1;
        std::string prerequisite;
        std::string branch_name;
    };
    std::vector<ClientTalent> talent_tree; // Full list for player's class
    float talent_scroll_offset = 0.0f;     // Pixel scroll for talent tree view

    // Quest log cursor (client branch)
    int quest_cursor = 0;

    // World map data
    std::vector<MapQuestMarker> map_quest_markers;
    float player_x = 0, player_z = 0;

    bool any_panel_open() const { return active_panel != ActivePanel::None; }
    bool is_panel_open() const { return active_panel != ActivePanel::None; }

    void toggle_panel(ActivePanel panel) {
        if (active_panel == panel) {
            active_panel = ActivePanel::None;
        } else {
            active_panel = panel;
        }
    }

    void close_all() { active_panel = ActivePanel::None; }
};

// ============================================================================
// Helpers
// ============================================================================

inline uint32_t rarity_color(const std::string& rarity) {
    if (rarity == "uncommon") {
        return 0xFF00CC00;
    }
    if (rarity == "rare") {
        return 0xFF0088FF;
    }
    if (rarity == "epic") {
        return 0xFFCC00CC;
    }
    if (rarity == "legendary") {
        return 0xFF00AAFF;
    }
    return 0xFF888888; // common
}

} // namespace mmo::client

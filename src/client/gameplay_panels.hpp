#pragma once

#include "engine/scene/ui_scene.hpp"
#include "engine/render_constants.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

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

// Panel state for all gameplay panels (combined from both branches)
struct PanelState {
    // Client branch panel switching
    ActivePanel active_panel = ActivePanel::None;

    // Server branch panel toggles
    bool inventory_open = false;
    bool quest_log_open = false;
    bool talent_tree_open = false;
    bool world_map_open = false;

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
    std::vector<std::string> unlocked_talents;  // String IDs from server
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
    std::vector<ClientTalent> talent_tree;  // Full list for player's class

    // Quest log cursor (client branch)
    int quest_cursor = 0;

    // World map data
    std::vector<MapQuestMarker> map_quest_markers;
    float player_x = 0, player_z = 0;

    bool any_panel_open() const { return inventory_open || quest_log_open || talent_tree_open || world_map_open || active_panel != ActivePanel::None; }
    bool is_panel_open() const { return active_panel != ActivePanel::None || any_panel_open(); }

    void toggle_panel(ActivePanel panel) {
        if (active_panel == panel) {
            active_panel = ActivePanel::None;
        } else {
            active_panel = panel;
        }
    }

    void toggle_inventory() {
        inventory_open = !inventory_open;
        if (inventory_open) { quest_log_open = false; talent_tree_open = false; world_map_open = false; }
    }
    void toggle_quest_log() {
        quest_log_open = !quest_log_open;
        if (quest_log_open) { inventory_open = false; talent_tree_open = false; world_map_open = false; }
    }
    void toggle_talent_tree() {
        talent_tree_open = !talent_tree_open;
        if (talent_tree_open) { inventory_open = false; quest_log_open = false; world_map_open = false; }
    }
    void toggle_world_map() {
        world_map_open = !world_map_open;
        if (world_map_open) { inventory_open = false; quest_log_open = false; talent_tree_open = false; }
    }
    void close_all() { inventory_open = false; quest_log_open = false; talent_tree_open = false; world_map_open = false; active_panel = ActivePanel::None; }
};

// ============================================================================
// Helpers
// ============================================================================

inline uint32_t rarity_color(const std::string& rarity) {
    if (rarity == "uncommon")  return 0xFF00CC00;
    if (rarity == "rare")      return 0xFF0088FF;
    if (rarity == "epic")      return 0xFFCC00CC;
    if (rarity == "legendary") return 0xFF00AAFF;
    return 0xFF888888; // common
}

// ============================================================================
// Inventory Panel
// ============================================================================

inline void build_inventory_panel(engine::scene::UIScene& ui, const PanelState& state,
                                  float screen_w, float screen_h) {
    using namespace engine::ui_colors;

    constexpr float PW = 400.0f;
    constexpr float PH = 500.0f;
    const float px = (screen_w - PW) * 0.5f;
    const float py = (screen_h - PH) * 0.5f;

    // Background and border
    ui.add_filled_rect(px, py, PW, PH, PANEL_BG);
    ui.add_rect_outline(px, py, PW, PH, BORDER, 2.0f);

    // Title bar
    ui.add_filled_rect(px, py, PW, 36.0f, 0xFF332211);
    ui.add_text("INVENTORY", px + PW * 0.5f - 40.0f, py + 10.0f, 1.2f, WHITE);

    // Item grid: 4 columns x 5 rows, slot 70x70, gap 10
    constexpr int COLS = 4;
    constexpr int ROWS = 5;
    constexpr float SLOT = 70.0f;
    constexpr float GAP = 10.0f;
    const float grid_w = COLS * SLOT + (COLS - 1) * GAP;
    const float grid_x = px + (PW - grid_w) * 0.5f;
    const float grid_y = py + 50.0f;

    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            int idx = row * COLS + col;
            float sx = grid_x + col * (SLOT + GAP);
            float sy = grid_y + row * (SLOT + GAP);

            // Slot background
            ui.add_filled_rect(sx, sy, SLOT, SLOT, 0xCC111111);

            if (idx < static_cast<int>(state.inventory_slots_display.size())) {
                const auto& slot = state.inventory_slots_display[idx];
                if (!slot.item_id.empty()) {
                    // Rarity border
                    ui.add_rect_outline(sx, sy, SLOT, SLOT, rarity_color(slot.rarity), 2.0f);

                    // Item name — truncate to fit 70px slot at scale 0.6 (~12 chars)
                    std::string name = slot.item_name;
                    if (name.length() > 12) name = name.substr(0, 11) + "~";
                    ui.add_text(name, sx + 4.0f, sy + 26.0f, 0.6f, WHITE);

                    // Stack count
                    if (slot.count > 1) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d", slot.count);
                        ui.add_text(buf, sx + SLOT - 18.0f, sy + SLOT - 18.0f, 0.7f, TEXT_DIM);
                    }
                } else {
                    ui.add_rect_outline(sx, sy, SLOT, SLOT, 0xFF333333, 1.0f);
                }
            } else {
                ui.add_rect_outline(sx, sy, SLOT, SLOT, 0xFF333333, 1.0f);
            }

            // Selection highlight
            if (idx == state.inventory_selected) {
                ui.add_rect_outline(sx, sy, SLOT, SLOT, WHITE, 3.0f);
            }
        }
    }

    // Equipment section
    const float equip_y = grid_y + ROWS * (SLOT + GAP) + 5.0f;
    ui.add_filled_rect(px + 10.0f, equip_y, PW - 20.0f, 1.0f, BORDER);

    const float eq_label_y = equip_y + 8.0f;
    const float eq_slot_y = equip_y + 26.0f;
    constexpr float EQ_SLOT_W = 170.0f;
    constexpr float EQ_SLOT_H = 40.0f;

    // Weapon slot
    ui.add_text("Weapon", px + 20.0f, eq_label_y, 0.7f, TEXT_DIM);
    ui.add_filled_rect(px + 20.0f, eq_slot_y, EQ_SLOT_W, EQ_SLOT_H, 0xCC111111);
    if (!state.equipped_weapon_display.item_id.empty()) {
        ui.add_rect_outline(px + 20.0f, eq_slot_y, EQ_SLOT_W, EQ_SLOT_H,
                            rarity_color(state.equipped_weapon_display.rarity), 2.0f);
        ui.add_text(state.equipped_weapon_display.item_name, px + 28.0f, eq_slot_y + 12.0f, 0.7f, WHITE);
    } else {
        ui.add_rect_outline(px + 20.0f, eq_slot_y, EQ_SLOT_W, EQ_SLOT_H, 0xFF333333, 1.0f);
        ui.add_text("Empty", px + 70.0f, eq_slot_y + 12.0f, 0.7f, TEXT_HINT);
    }

    // Armor slot
    const float armor_x = px + PW - 20.0f - EQ_SLOT_W;
    ui.add_text("Armor", armor_x, eq_label_y, 0.7f, TEXT_DIM);
    ui.add_filled_rect(armor_x, eq_slot_y, EQ_SLOT_W, EQ_SLOT_H, 0xCC111111);
    if (!state.equipped_armor_display.item_id.empty()) {
        ui.add_rect_outline(armor_x, eq_slot_y, EQ_SLOT_W, EQ_SLOT_H,
                            rarity_color(state.equipped_armor_display.rarity), 2.0f);
        ui.add_text(state.equipped_armor_display.item_name, armor_x + 8.0f, eq_slot_y + 12.0f, 0.7f, WHITE);
    } else {
        ui.add_rect_outline(armor_x, eq_slot_y, EQ_SLOT_W, EQ_SLOT_H, 0xFF333333, 1.0f);
        ui.add_text("Empty", armor_x + 50.0f, eq_slot_y + 12.0f, 0.7f, TEXT_HINT);
    }

    // Gold counter
    char gold_buf[128];
    snprintf(gold_buf, sizeof(gold_buf), "Gold: %d", state.gold);
    ui.add_text(gold_buf, px + PW * 0.5f - 30.0f, py + PH - 28.0f, 0.9f, 0xFF00DDFF);
}

// ============================================================================
// Quest Log Panel
// ============================================================================

inline void build_quest_log_panel(engine::scene::UIScene& ui, const PanelState& state,
                                  float screen_w, float screen_h) {
    using namespace engine::ui_colors;

    constexpr float PW = 350.0f;
    constexpr float PH = 500.0f;
    const float px = screen_w - PW - 20.0f;
    const float py = (screen_h - PH) * 0.5f;

    // Background and border
    ui.add_filled_rect(px, py, PW, PH, PANEL_BG);
    ui.add_rect_outline(px, py, PW, PH, BORDER, 2.0f);

    // Title bar
    ui.add_filled_rect(px, py, PW, 36.0f, 0xFF332211);
    ui.add_text("QUEST LOG", px + PW * 0.5f - 42.0f, py + 10.0f, 1.2f, WHITE);

    // Quest list
    float cy = py + 50.0f;
    const float max_y = py + PH - 30.0f;
    constexpr float LEFT_PAD = 16.0f;
    constexpr float INDENT = 24.0f;

    for (int qi = 0; qi < static_cast<int>(state.quest_entries.size()); ++qi) {
        if (cy > max_y) break;

        const auto& quest = state.quest_entries[qi];

        // Selection highlight
        if (qi == state.quest_log_selected) {
            ui.add_filled_rect(px + 4.0f, cy - 2.0f, PW - 8.0f, 20.0f, SELECTION);
        }

        // Quest name
        uint32_t name_color = quest.all_complete ? 0xFF00CC00 : 0xFF00DDFF;
        ui.add_text(quest.name, px + LEFT_PAD, cy, 0.8f, name_color);
        cy += 22.0f;

        // Objectives
        for (const auto& obj : quest.objectives) {
            if (cy > max_y) break;

            char buf[128];
            if (obj.complete) {
                snprintf(buf, sizeof(buf), "[X] %s", obj.description.c_str());
                ui.add_text(buf, px + LEFT_PAD + INDENT, cy, 0.65f, 0xFF00CC00);
            } else {
                snprintf(buf, sizeof(buf), "[ ] %s: %d/%d",
                         obj.description.c_str(), obj.current, obj.required);
                ui.add_text(buf, px + LEFT_PAD + INDENT, cy, 0.65f, TEXT_DIM);
            }
            cy += 17.0f;
        }

        // Spacing between quests
        cy += 8.0f;
    }

    // Scroll indicator
    if (!state.quest_entries.empty() && cy > max_y) {
        ui.add_text("-- more --", px + PW * 0.5f - 30.0f, py + PH - 22.0f, 0.6f, TEXT_HINT);
    }
}

// ============================================================================
// Talent Tree Panel
// ============================================================================

inline void build_talent_tree_panel(engine::scene::UIScene& ui, const PanelState& state,
                                    float screen_w, float screen_h) {
    using namespace engine::ui_colors;

    constexpr float PW = 600.0f;
    constexpr float PH = 450.0f;
    const float px = (screen_w - PW) * 0.5f;
    const float py = (screen_h - PH) * 0.5f;

    // Background and border
    ui.add_filled_rect(px, py, PW, PH, PANEL_BG);
    ui.add_rect_outline(px, py, PW, PH, BORDER, 2.0f);

    // Title bar
    ui.add_filled_rect(px, py, PW, 36.0f, 0xFF332211);
    ui.add_text("TALENTS", px + PW * 0.5f - 32.0f, py + 10.0f, 1.2f, WHITE);

    // Available points
    char pts_buf[64];
    snprintf(pts_buf, sizeof(pts_buf), "Points: %d", state.talent_points_display);
    ui.add_text(pts_buf, px + PW - 110.0f, py + 10.0f, 0.8f,
                state.talent_points_display > 0 ? 0xFF00FF00 : TEXT_DIM);

    // Columns
    const int num_branches = static_cast<int>(state.talent_branches.size());
    if (num_branches == 0) {
        ui.add_text("No talent branches available", px + PW * 0.5f - 100.0f,
                    py + PH * 0.5f, 0.8f, TEXT_HINT);
        return;
    }

    const float col_w = (PW - 40.0f) / static_cast<float>(num_branches);
    const float col_start_y = py + 50.0f;

    constexpr float TALENT_W = 150.0f;
    constexpr float TALENT_H = 60.0f;
    constexpr float TIER_GAP = 16.0f;

    constexpr uint32_t COLOR_UNLOCKED  = 0xFF00CC00;
    constexpr uint32_t COLOR_AVAILABLE = 0xFFFFAA00;
    constexpr uint32_t COLOR_LOCKED    = 0xFF555555;

    int global_idx = 0;

    for (int bi = 0; bi < num_branches; ++bi) {
        const auto& branch = state.talent_branches[bi];
        const float col_x = px + 20.0f + bi * col_w;
        const float col_cx = col_x + col_w * 0.5f;

        // Branch name
        ui.add_text(branch.name,
                    col_cx - static_cast<float>(branch.name.size()) * 3.5f,
                    col_start_y, 0.85f, 0xFF00DDFF);

        // Column separator lines
        if (bi > 0) {
            ui.add_line(col_x - 2.0f, col_start_y - 4.0f,
                        col_x - 2.0f, py + PH - 16.0f, 0xFF444444, 1.0f);
        }

        // Draw talents by tier with prerequisite lines
        float prev_box_cx = 0.0f;
        float prev_box_by = 0.0f;
        bool had_prev = false;

        for (const auto& talent : branch.talents) {
            int tier = talent.tier;
            if (tier < 1) tier = 1;
            if (tier > 3) tier = 3;

            float tx = col_cx - TALENT_W * 0.5f;
            float ty = col_start_y + 26.0f + (tier - 1) * (TALENT_H + TIER_GAP);

            // Prerequisite connecting line
            if (!talent.prerequisite.empty() && had_prev) {
                ui.add_line(prev_box_cx, prev_box_by,
                            col_cx, ty, 0xFF444444, 2.0f);
            }

            // Talent box background
            ui.add_filled_rect(tx, ty, TALENT_W, TALENT_H, 0xCC111111);

            // Border color based on state
            uint32_t border_color = COLOR_LOCKED;
            if (talent.unlocked) {
                border_color = COLOR_UNLOCKED;
            } else if (talent.available) {
                border_color = COLOR_AVAILABLE;
            }
            ui.add_rect_outline(tx, ty, TALENT_W, TALENT_H, border_color, 2.0f);

            // Selection highlight
            if (global_idx == state.talent_selected) {
                ui.add_rect_outline(tx - 2.0f, ty - 2.0f,
                                    TALENT_W + 4.0f, TALENT_H + 4.0f, WHITE, 2.0f);
            }

            // Talent name
            uint32_t name_color = talent.unlocked ? WHITE
                                : (talent.available ? 0xFFFFDD88 : TEXT_HINT);
            ui.add_text(talent.name, tx + 6.0f, ty + 8.0f, 0.7f, name_color);

            // Description
            ui.add_text(talent.description, tx + 6.0f, ty + 28.0f, 0.5f,
                        talent.unlocked ? TEXT_DIM : 0xFF555555);

            // Unlocked indicator
            if (talent.unlocked) {
                ui.add_text("*", tx + TALENT_W - 16.0f, ty + 4.0f, 0.8f, COLOR_UNLOCKED);
            }

            prev_box_cx = col_cx;
            prev_box_by = ty + TALENT_H;
            had_prev = true;
            ++global_idx;
        }
    }
}

// ============================================================================
// World Map Panel
// ============================================================================

inline void build_world_map_panel(engine::scene::UIScene& ui, const PanelState& state,
                                   float screen_w, float screen_h) {
    using namespace engine::ui_colors;

    // Panel dimensions: 80% of screen, centered
    const float PW = screen_w * 0.8f;
    const float PH = screen_h * 0.8f;
    const float px = (screen_w - PW) * 0.5f;
    const float py = (screen_h - PH) * 0.5f;

    // Background and border
    ui.add_filled_rect(px, py, PW, PH, 0xEE111122);
    ui.add_rect_outline(px, py, PW, PH, BORDER, 2.0f);

    // Title bar
    ui.add_filled_rect(px, py, PW, 36.0f, 0xFF1A1A33);
    ui.add_text("WORLD MAP", px + PW * 0.5f - 44.0f, py + 10.0f, 1.2f, WHITE);

    // Close hint
    ui.add_text("[M/ESC] Close", px + PW - 110.0f, py + 12.0f, 0.7f, TEXT_HINT);

    // Map area (inside panel, with padding)
    const float MAP_PAD = 16.0f;
    const float map_x = px + MAP_PAD;
    const float map_y = py + 44.0f;
    const float map_w = PW - MAP_PAD * 2.0f;
    const float map_h = PH - 44.0f - MAP_PAD;

    // Map background
    ui.add_filled_rect(map_x, map_y, map_w, map_h, 0xFF0A0A18);
    ui.add_rect_outline(map_x, map_y, map_w, map_h, 0xFF333355, 1.0f);

    // World-to-screen coordinate helper
    constexpr float WORLD_SIZE = 32000.0f;
    auto world_to_map = [&](float wx, float wz, float& sx, float& sy) {
        sx = map_x + (wx / WORLD_SIZE) * map_w;
        sy = map_y + (wz / WORLD_SIZE) * map_h;
    };

    // --- Zone regions ---
    struct ZoneDisplay { const char* name; float cx, cz, radius; uint32_t color; const char* levels; };
    ZoneDisplay zones[] = {
        {"Thornwall",           4000,  4000,  800,   0x3000FF00, "Lv 1-3"},
        {"Greenhollow Meadows", 5500,  5500,  2500,  0x2044DD44, "Lv 3-6"},
        {"Whispering Woods",    2000,  6000,  2500,  0x2022AA22, "Lv 5-8"},
        {"Dustwind Flats",      7500,  4000,  2800,  0x20AABB44, "Lv 7-10"},
        {"Ironpeak Foothills",  4000,  10000, 3000,  0x208888AA, "Lv 10-14"},
        {"Shadewood Marsh",     12000, 7000,  3500,  0x20664488, "Lv 14-18"},
        {"Fallen Citadel",      16000, 4000,  3500,  0x20444488, "Lv 18-22"},
        {"Ashen Wastes",        22000, 10000, 4500,  0x20884444, "Lv 22-28"},
        {"Void Rift",           26000, 20000, 4000,  0x20AA44AA, "Lv 28-35"},
        {"Dragon's Reach",      28000, 28000, 4500,  0x20FF4400, "Lv 35-40"},
    };

    for (const auto& zone : zones) {
        float sx = 0.0f, sy = 0.0f;
        world_to_map(zone.cx, zone.cz, sx, sy);
        float sr = (zone.radius / WORLD_SIZE) * map_w;

        // Filled circle for zone area
        ui.add_circle(sx, sy, sr, zone.color, 32);

        // Zone outline
        uint32_t outline_color = (zone.color & 0x00FFFFFF) | 0x40000000;
        ui.add_circle_outline(sx, sy, sr, outline_color, 1.0f, 32);

        // Zone name (centered above the zone)
        float name_len = static_cast<float>(strlen(zone.name)) * 4.0f;
        ui.add_text(zone.name, sx - name_len, sy - 8.0f, 0.65f, 0xCCFFFFFF);

        // Level range (below name)
        float lvl_len = static_cast<float>(strlen(zone.levels)) * 3.5f;
        ui.add_text(zone.levels, sx - lvl_len, sy + 6.0f, 0.55f, 0xAABBBBBB);
    }

    // --- Points of interest ---
    struct POI { const char* name; float x, z; uint32_t color; };
    POI pois[] = {
        {"Town",              4000,  4000,  0xFF00FF00},
        {"Mining Camp",       8000,  2000,  0xFF88AAFF},
        {"Forest Shrine",     12000, 6000,  0xFF44FF44},
        {"Fishing Village",   6000,  14000, 0xFF88AAFF},
        {"Western Ruins",     2000,  8000,  0xFFFF4444},
        {"Central Crossroads",16000, 16000, 0xFFFFFFFF},
        {"Mountain Pass",     20000, 10000, 0xFF888888},
        {"Eastern Outpost",   28000, 16000, 0xFF88AAFF},
        {"Dark Forest",       10000, 22000, 0xFF884488},
    };

    for (const auto& poi : pois) {
        float sx = 0.0f, sy = 0.0f;
        world_to_map(poi.x, poi.z, sx, sy);

        // Small diamond shape using two triangles approximated with a small filled rect + outline
        constexpr float POI_SIZE = 5.0f;
        ui.add_filled_rect(sx - POI_SIZE, sy - POI_SIZE, POI_SIZE * 2.0f, POI_SIZE * 2.0f, poi.color);
        ui.add_rect_outline(sx - POI_SIZE, sy - POI_SIZE, POI_SIZE * 2.0f, POI_SIZE * 2.0f, 0xFFFFFFFF, 1.0f);

        // Label to the right
        ui.add_text(poi.name, sx + POI_SIZE + 4.0f, sy - 5.0f, 0.5f, poi.color);
    }

    // --- Quest objective markers ---
    for (const auto& qm : state.map_quest_markers) {
        float sx = 0.0f, sy = 0.0f;
        world_to_map(qm.world_x, qm.world_z, sx, sy);
        float sr = (qm.radius / WORLD_SIZE) * map_w;
        if (sr < 6.0f) sr = 6.0f;

        uint32_t area_color = qm.complete ? 0x2000FF00 : 0x20FFD700;
        uint32_t marker_color = qm.complete ? 0xFF00CC00 : 0xFFFFD700;

        // Objective radius circle
        ui.add_circle(sx, sy, sr, area_color, 24);
        ui.add_circle_outline(sx, sy, sr, marker_color, 1.5f, 24);

        // Diamond marker at center
        constexpr float QM_SIZE = 4.0f;
        ui.add_filled_rect(sx - QM_SIZE, sy - QM_SIZE, QM_SIZE * 2.0f, QM_SIZE * 2.0f, marker_color);

        // Quest name label
        ui.add_text(qm.quest_name, sx + QM_SIZE + 4.0f, sy - 5.0f, 0.45f, marker_color);
    }

    // --- Player position ---
    {
        float sx = 0.0f, sy = 0.0f;
        world_to_map(state.player_x, state.player_z, sx, sy);

        // Player marker: white filled rect with outline
        constexpr float PL_SIZE = 6.0f;
        ui.add_filled_rect(sx - PL_SIZE, sy - PL_SIZE, PL_SIZE * 2.0f, PL_SIZE * 2.0f, 0xFFFFFFFF);
        ui.add_rect_outline(sx - PL_SIZE - 1.0f, sy - PL_SIZE - 1.0f,
                            PL_SIZE * 2.0f + 2.0f, PL_SIZE * 2.0f + 2.0f, 0xFF00CCFF, 2.0f);

        // "You" label
        ui.add_text("You", sx - 8.0f, sy - PL_SIZE - 14.0f, 0.7f, 0xFF00CCFF);
    }

    // --- Grid lines for reference ---
    for (int i = 1; i < 4; ++i) {
        float gx = map_x + (static_cast<float>(i) / 4.0f) * map_w;
        float gy = map_y + (static_cast<float>(i) / 4.0f) * map_h;
        ui.add_line(gx, map_y, gx, map_y + map_h, 0x15FFFFFF, 1.0f);
        ui.add_line(map_x, gy, map_x + map_w, gy, 0x15FFFFFF, 1.0f);
    }

    // --- Cardinal labels ---
    ui.add_text("N", map_x + map_w * 0.5f - 3.0f, map_y + 2.0f, 0.5f, 0x66FFFFFF);
    ui.add_text("S", map_x + map_w * 0.5f - 3.0f, map_y + map_h - 14.0f, 0.5f, 0x66FFFFFF);
    ui.add_text("W", map_x + 2.0f, map_y + map_h * 0.5f - 5.0f, 0.5f, 0x66FFFFFF);
    ui.add_text("E", map_x + map_w - 10.0f, map_y + map_h * 0.5f - 5.0f, 0.5f, 0x66FFFFFF);
}

// ============================================================================
// Main entry point
// ============================================================================

inline void build_gameplay_panels(engine::scene::UIScene& ui, const PanelState& state,
                                  float screen_w, float screen_h) {
    // Only render server-branch panels that aren't handled by active_panel switch
    if (state.inventory_open && state.active_panel != ActivePanel::Inventory)
        build_inventory_panel(ui, state, screen_w, screen_h);
    if (state.quest_log_open && state.active_panel != ActivePanel::QuestLog)
        build_quest_log_panel(ui, state, screen_w, screen_h);
    // Talent tree: always skip — build_talent_panel_ui in game.cpp is the active version
    if (state.world_map_open)   build_world_map_panel(ui, state, screen_w, screen_h);
}
} // namespace mmo::client

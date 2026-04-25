#include "client/hud/panels.hpp"
#include <cstdio>
#include <cstring>

namespace mmo::client {

using engine::scene::UIScene;
using namespace engine::ui_colors;

// ============================================================================
// World Map Panel
// ============================================================================

void build_world_map_panel(UIScene& ui, const PanelState& state,
                           float screen_w, float screen_h) {
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

void build_gameplay_panels(UIScene& ui, const PanelState& state,
                           float screen_w, float screen_h) {
    if (state.active_panel == ActivePanel::WorldMap)
        build_world_map_panel(ui, state, screen_w, screen_h);
}

} // namespace mmo::client

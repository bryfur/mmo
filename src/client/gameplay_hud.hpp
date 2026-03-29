#pragma once

#include "engine/scene/ui_scene.hpp"
#include "client/ui_colors.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mmo::client {

using engine::scene::UIScene;
using namespace engine::ui_colors;

// ============================================================================
// HUD-specific colors
// ============================================================================

namespace hud_colors {
    // XP bar
    constexpr uint32_t XP_FILL       = 0xFF00DDFF;  // gold/yellow (ABGR)
    constexpr uint32_t XP_BG         = 0xFF1A1A00;
    constexpr uint32_t XP_FRAME      = 0xFF000000;
    constexpr uint32_t XP_TEXT       = 0xFF44EEFF;

    // Mana bar
    constexpr uint32_t MANA_FILL     = 0xFFFF4400;  // blue (ABGR)
    constexpr uint32_t MANA_BG       = 0xFF660000;
    constexpr uint32_t MANA_FRAME    = 0xFF000000;

    // Gold
    constexpr uint32_t GOLD_COIN     = 0xFF00CCFF;  // yellow/gold (ABGR)
    constexpr uint32_t GOLD_TEXT     = 0xFF00EEFF;

    // Skill bar
    constexpr uint32_t SKILL_BG      = 0xCC1A1A1A;
    constexpr uint32_t SKILL_BORDER  = 0xFF555555;
    constexpr uint32_t SKILL_READY   = 0xFF444444;
    constexpr uint32_t COOLDOWN_OVERLAY = 0xAA000000;
    constexpr uint32_t KEY_NUMBER    = 0xFFCCCCCC;
    constexpr uint32_t SKILL_NAME    = 0xFF999999;

    // Quest tracker
    constexpr uint32_t QUEST_PANEL   = 0xCC1A1A1A;
    constexpr uint32_t QUEST_TITLE   = 0xFF55CCFF;
    constexpr uint32_t QUEST_OBJ     = 0xFFCCCCCC;
    constexpr uint32_t QUEST_DONE    = 0xFF00CC00;
    constexpr uint32_t QUEST_HEADER  = 0xFF88AACC;

    // Zone name
    constexpr uint32_t ZONE_TEXT     = 0xFFEEDDCC;
    constexpr uint32_t ZONE_SHADOW   = 0x80000000;

    // Level up
    constexpr uint32_t LEVEL_UP_TEXT = 0xFF00DDFF;
    constexpr uint32_t LEVEL_UP_SUB  = 0xFFCCEEFF;

    // Loot rarity
    constexpr uint32_t LOOT_COMMON   = 0xFFCCCCCC;
    constexpr uint32_t LOOT_UNCOMMON = 0xFF00CC00;
    constexpr uint32_t LOOT_RARE     = 0xFFFF8800;
    constexpr uint32_t LOOT_EPIC     = 0xFFFF44FF;
}

// ============================================================================
// Data structures (client branch - compact types for network messages)
// ============================================================================

// Tracked quest objective for the HUD quest tracker
struct QuestObjective {
    std::string description;
    uint16_t current = 0;
    uint16_t required = 0;
    bool complete() const { return current >= required; }
};

// Tracked quest for HUD display
struct TrackedQuest {
    uint16_t quest_id = 0;
    std::string name;
    std::vector<QuestObjective> objectives;
};

// Skill bar slot
struct SkillBarSlot {
    uint16_t skill_id = 0;
    std::string name;
    float cooldown_remaining = 0.0f;
    float cooldown_total = 0.0f;
    bool unlocked = false;
};

// Floating damage number
struct DamageNumber {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float damage = 0.0f;
    float timer = 0.0f;
    bool is_heal = false;

    static constexpr float DURATION = 1.5f;
    float alpha() const { return timer > 0.0f ? timer / DURATION : 0.0f; }
};

// Notification popup (level-up, quest complete, etc.)
struct Notification {
    std::string text;
    float timer = 0.0f;
    uint32_t color = 0xFFFFFFFF;

    static constexpr float DURATION = 3.0f;
};

// NPC dialogue state
struct NPCDialogueState {
    bool visible = false;
    uint32_t npc_id = 0;
    std::string npc_name;
    std::string dialogue;
    uint8_t quest_count = 0;
    uint16_t quest_ids[4] = {};
    std::string quest_names[4];
    int selected_option = 0;
};

// ============================================================================
// Data structures (server branch - for HUD rendering)
// ============================================================================

struct SkillSlot {
    std::string name;
    std::string skill_id;
    float cooldown = 0.0f;
    float max_cooldown = 0.0f;
    bool available = false;
    int key_number = 0;
};

struct QuestTrackerEntry {
    std::string quest_id;
    std::string quest_name;
    struct Objective {
        std::string description;
        int current = 0;
        int required = 0;
        bool complete = false;
        bool is_complete() const { return complete; }
    };
    std::vector<Objective> objectives;
};

struct LootFeedEntry {
    std::string text;
    uint32_t color = 0xFFFFFFFF;
    float timer = 3.0f;
};

struct HUDState {
    // Player stats
    int level = 1;
    int xp = 0;
    int xp_to_next_level = 100;
    int gold = 0;
    float health = 100.0f;
    float max_health = 100.0f;
    float mana = 100.0f;
    float max_mana = 100.0f;

    // Skills (server branch)
    SkillSlot skill_slots[5] = {};

    // Skill bar (client branch - up to 8 slots)
    static constexpr int MAX_SKILL_SLOTS = 8;
    SkillBarSlot skill_bar_slots[MAX_SKILL_SLOTS] = {};

    // Quest tracker (server branch)
    std::vector<QuestTrackerEntry> tracked_quests;

    // Quest tracker (client branch)
    std::vector<TrackedQuest> client_tracked_quests;

    // Floating damage numbers
    std::vector<DamageNumber> damage_numbers;

    // Notifications
    std::vector<Notification> notifications;

    // NPC dialogue
    NPCDialogueState dialogue;

    // Minimap
    struct MinimapState {
        float player_x = 0;
        float player_z = 0;

        struct MapIcon {
            float world_x = 0;
            float world_z = 0;
            uint32_t color = 0xFFFFFFFF;
            bool is_objective = false;  // show as diamond shape
        };
        std::vector<MapIcon> icons;

        struct ObjectiveArea {
            float world_x = 0;
            float world_z = 0;
            float radius = 100;
        };
        std::vector<ObjectiveArea> objective_areas;
    };
    MinimapState minimap;

    // Zone
    std::string current_zone = "";
    float zone_display_timer = 0.0f;

    // Notifications (server branch)
    float level_up_timer = 0.0f;
    int level_up_level = 0;

    // Loot feed
    std::vector<LootFeedEntry> loot_feed;

    void update(float dt) {
        if (zone_display_timer > 0) zone_display_timer -= dt;
        if (level_up_timer > 0) level_up_timer -= dt;
        for (auto& slot : skill_slots) {
            if (slot.cooldown > 0.0f) {
                slot.cooldown -= dt;
                if (slot.cooldown < 0.0f) slot.cooldown = 0.0f;
            }
        }
        for (auto& entry : loot_feed) entry.timer -= dt;
        loot_feed.erase(
            std::remove_if(loot_feed.begin(), loot_feed.end(),
                [](const LootFeedEntry& e) { return e.timer <= 0; }),
            loot_feed.end());
    }

    void add_loot(const std::string& text, uint32_t color = 0xFFFFFFFF) {
        loot_feed.push_back({text, color, 4.0f});
        if (loot_feed.size() > 5) loot_feed.erase(loot_feed.begin());
    }

    void show_level_up(int new_level) {
        level_up_timer = 3.0f;
        level_up_level = new_level;
    }

    void set_zone(const std::string& zone) {
        if (zone != current_zone) {
            current_zone = zone;
            zone_display_timer = 4.0f;
        }
    }
};

// ============================================================================
// Helper: compute alpha-faded color
// ============================================================================

inline uint32_t fade_color(uint32_t color, float alpha) {
    if (alpha >= 1.0f) return color;
    if (alpha <= 0.0f) return color & 0x00FFFFFF;
    uint32_t a = static_cast<uint32_t>(((color >> 24) & 0xFF) * alpha);
    return (a << 24) | (color & 0x00FFFFFF);
}

// ============================================================================
// XP bar - below the health bar
// ============================================================================

inline void build_xp_bar(UIScene& ui, const HUDState& hud, float /*screen_w*/, float screen_h) {
    // Health bar is at y = screen_h - 45, height 25, so its bottom edge is at screen_h - 20.
    // Place XP bar just below the health bar.
    float bar_width = 250.0f;
    float bar_height = 12.0f;
    float padding = 20.0f;
    float x = padding;
    float y = screen_h - padding - bar_height - 30.0f; // Above health bar area

    // Background frame
    ui.add_filled_rect(x - 2, y - 2, bar_width + 4, bar_height + 4, hud_colors::XP_FRAME);
    ui.add_rect_outline(x - 2, y - 2, bar_width + 4, bar_height + 4, BORDER, 1.0f);

    // Background fill
    ui.add_filled_rect(x, y, bar_width, bar_height, hud_colors::XP_BG);

    // XP fill
    float xp_ratio = (hud.xp_to_next_level > 0)
        ? static_cast<float>(hud.xp) / static_cast<float>(hud.xp_to_next_level)
        : 0.0f;
    xp_ratio = std::min(xp_ratio, 1.0f);
    if (xp_ratio > 0.0f) {
        ui.add_filled_rect(x, y, bar_width * xp_ratio, bar_height, hud_colors::XP_FILL);
    }

    // Level badge to the left of the bar
    float badge_size = bar_height + 4;
    float badge_x = x - badge_size - 4;
    float badge_y = y - 2;
    ui.add_filled_rect(badge_x, badge_y, badge_size, badge_size, PANEL_BG);
    ui.add_rect_outline(badge_x, badge_y, badge_size, badge_size, hud_colors::XP_FILL, 1.0f);

    char level_text[8];
    snprintf(level_text, sizeof(level_text), "%d", hud.level);
    ui.add_text(level_text, badge_x + 3, badge_y + 1, 0.7f, hud_colors::XP_TEXT);

    // XP text on the bar
    char xp_text[32];
    snprintf(xp_text, sizeof(xp_text), "XP: %d/%d", hud.xp, hud.xp_to_next_level);
    ui.add_text(xp_text, x + 8, y, 0.6f, WHITE);
}

// ============================================================================
// Mana bar - next to the health bar
// ============================================================================

inline void build_mana_bar(UIScene& ui, const HUDState& hud, float /*screen_w*/, float screen_h) {
    // Health bar: x=20, y=screen_h-45, w=250, h=25
    // Place mana bar directly to the right with a gap
    float health_bar_width = 250.0f;
    float padding = 20.0f;
    float gap = 10.0f;
    float bar_width = 160.0f;
    float bar_height = 25.0f;
    float x = padding + health_bar_width + gap;
    float y = screen_h - padding - bar_height;

    // Frame
    ui.add_filled_rect(x - 2, y - 2, bar_width + 4, bar_height + 4, hud_colors::MANA_FRAME);
    ui.add_rect_outline(x - 2, y - 2, bar_width + 4, bar_height + 4, BORDER, 1.0f);

    // Background
    ui.add_filled_rect(x, y, bar_width, bar_height, hud_colors::MANA_BG);

    // Fill
    float mana_ratio = (hud.max_mana > 0) ? hud.mana / hud.max_mana : 0.0f;
    mana_ratio = std::min(mana_ratio, 1.0f);
    if (mana_ratio > 0.0f) {
        ui.add_filled_rect(x, y, bar_width * mana_ratio, bar_height, hud_colors::MANA_FILL);
    }

    // Text
    char mp_text[32];
    snprintf(mp_text, sizeof(mp_text), "MP: %.0f/%.0f", hud.mana, hud.max_mana);
    ui.add_text(mp_text, x + 8, y + 5, 1.0f, WHITE);
}

// ============================================================================
// Gold display - top-right corner
// ============================================================================

inline void build_gold_display(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
    float padding = 15.0f;
    float panel_w = 130.0f;
    float panel_h = 30.0f;
    float x = screen_w - padding - panel_w;
    float y = 210.0f;  // Below the minimap (180px + padding)

    // Panel background
    ui.add_filled_rect(x, y, panel_w, panel_h, PANEL_BG);
    ui.add_rect_outline(x, y, panel_w, panel_h, BORDER, 1.0f);

    // Gold coin icon (small filled circle)
    float coin_r = 7.0f;
    float coin_x = x + 16.0f;
    float coin_y = y + panel_h / 2.0f;
    ui.add_circle(coin_x, coin_y, coin_r, hud_colors::GOLD_COIN, 12);
    ui.add_circle_outline(coin_x, coin_y, coin_r, 0xFF009999, 1.0f, 12);
    // "G" label on coin
    ui.add_text("G", coin_x - 4, coin_y - 6, 0.6f, 0xFF003366);

    // Amount text
    char gold_text[32];
    snprintf(gold_text, sizeof(gold_text), "%d", hud.gold);
    ui.add_text(gold_text, x + 32, y + 7, 1.0f, hud_colors::GOLD_TEXT);
}

// ============================================================================
// Skill bar - bottom center, 5 slots
// ============================================================================

inline void build_skill_bar(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
    float slot_size = 50.0f;
    float slot_gap = 6.0f;
    int slot_count = 5;
    float total_width = slot_count * slot_size + (slot_count - 1) * slot_gap;
    float start_x = (screen_w - total_width) / 2.0f;
    float y = screen_h - slot_size - 30.0f;

    // Panel background behind all slots
    float panel_pad = 8.0f;
    ui.add_filled_rect(start_x - panel_pad, y - panel_pad,
                       total_width + panel_pad * 2, slot_size + panel_pad * 2 + 16.0f,
                       PANEL_BG);
    ui.add_rect_outline(start_x - panel_pad, y - panel_pad,
                        total_width + panel_pad * 2, slot_size + panel_pad * 2 + 16.0f,
                        BORDER, 1.0f);

    for (int i = 0; i < slot_count; i++) {
        float sx = start_x + i * (slot_size + slot_gap);
        const auto& slot = hud.skill_slots[i];

        // Slot background
        uint32_t bg_color = slot.available ? hud_colors::SKILL_READY : hud_colors::SKILL_BG;
        ui.add_filled_rect(sx, y, slot_size, slot_size, bg_color);
        ui.add_rect_outline(sx, y, slot_size, slot_size, hud_colors::SKILL_BORDER, 1.0f);

        // Key number in top-left corner
        char key_text[12];
        int key = slot.key_number > 0 ? slot.key_number : (i + 1);
        snprintf(key_text, sizeof(key_text), "%d", key);
        ui.add_text(key_text, sx + 3, y + 2, 0.6f, hud_colors::KEY_NUMBER);

        // Cooldown overlay
        if (slot.cooldown > 0.0f && slot.max_cooldown > 0.0f) {
            float cd_ratio = slot.cooldown / slot.max_cooldown;
            cd_ratio = std::min(cd_ratio, 1.0f);
            // Darkened overlay from bottom up based on remaining cooldown
            float overlay_h = slot_size * cd_ratio;
            float overlay_y = y + (slot_size - overlay_h);
            ui.add_filled_rect(sx, overlay_y, slot_size, overlay_h, hud_colors::COOLDOWN_OVERLAY);

            // Cooldown timer text centered
            char cd_text[8];
            snprintf(cd_text, sizeof(cd_text), "%.1f", slot.cooldown);
            ui.add_text(cd_text, sx + 12, y + 18, 0.8f, WHITE);
        }

        // Skill name below slot
        if (!slot.name.empty()) {
            std::string display_name = slot.name;
            if (display_name.length() > 7) {
                display_name = display_name.substr(0, 6) + ".";
            }
            ui.add_text(display_name, sx + 2, y + slot_size + 2, 0.5f, hud_colors::SKILL_NAME);
        }
    }
}

// ============================================================================
// Quest tracker - right side
// ============================================================================

inline void build_quest_tracker(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
    if (hud.tracked_quests.empty()) return;

    float panel_w = 220.0f;
    float padding = 15.0f;
    float x = screen_w - padding - panel_w;
    float y = 250.0f;  // Below minimap and gold display
    float line_h = 16.0f;

    // Compute panel height based on content
    int max_quests = std::min(static_cast<int>(hud.tracked_quests.size()), 3);
    float content_h = 24.0f; // header
    for (int q = 0; q < max_quests; q++) {
        content_h += 20.0f; // quest name
        content_h += static_cast<float>(hud.tracked_quests[q].objectives.size()) * line_h;
        if (q < max_quests - 1) content_h += 6.0f; // gap between quests
    }
    content_h += 8.0f; // bottom padding

    // Panel background
    ui.add_filled_rect(x, y, panel_w, content_h, hud_colors::QUEST_PANEL);
    ui.add_rect_outline(x, y, panel_w, content_h, BORDER, 1.0f);

    // Header
    float cy = y + 5.0f;
    ui.add_text("QUESTS", x + 8, cy, 0.8f, hud_colors::QUEST_HEADER);
    cy += 20.0f;

    // Separator line
    ui.add_line(x + 6, cy, x + panel_w - 6, cy, BORDER, 1.0f);
    cy += 6.0f;

    // Quest entries
    for (int q = 0; q < max_quests; q++) {
        const auto& quest = hud.tracked_quests[q];

        // Quest name
        ui.add_text(quest.quest_name, x + 10, cy, 0.7f, hud_colors::QUEST_TITLE);
        cy += 18.0f;

        // Objectives
        for (const auto& obj : quest.objectives) {
            // Truncate description to fit panel width at scale 0.6 (~4.8px/char, 196px available)
            std::string desc = obj.description;
            if (desc.length() > 28) desc = desc.substr(0, 27) + "~";
            char obj_text[128];
            snprintf(obj_text, sizeof(obj_text), "  %s: %d/%d",
                     desc.c_str(), obj.current, obj.required);

            uint32_t obj_color = obj.complete ? hud_colors::QUEST_DONE : hud_colors::QUEST_OBJ;
            ui.add_text(obj_text, x + 12, cy, 0.6f, obj_color);
            cy += line_h;
        }

        if (q < max_quests - 1) cy += 6.0f;
    }
}

// ============================================================================
// Zone name - top center, fades in/out
// ============================================================================

inline void build_zone_name(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
    if (hud.zone_display_timer <= 0.0f || hud.current_zone.empty()) return;

    // Full opacity for first 2.5s, then fade out over 1.5s
    float alpha = 1.0f;
    if (hud.zone_display_timer < 1.5f) {
        alpha = hud.zone_display_timer / 1.5f;
    }

    // Estimate text width (roughly 15px per char at scale 1.5)
    float text_w = static_cast<float>(hud.current_zone.length()) * 15.0f;
    float x = (screen_w - text_w) / 2.0f;
    float y = 60.0f;

    // Shadow background
    float pad = 12.0f;
    ui.add_filled_rect(x - pad, y - pad, text_w + pad * 2, 36.0f + pad * 2,
                       fade_color(hud_colors::ZONE_SHADOW, alpha * 0.6f));

    // Zone name text (large)
    ui.add_text(hud.current_zone, x, y, 1.5f, fade_color(hud_colors::ZONE_TEXT, alpha));

    // Decorative underline
    float line_y = y + 32.0f;
    float line_w = text_w * 0.6f;
    float line_x = (screen_w - line_w) / 2.0f;
    ui.add_line(line_x, line_y, line_x + line_w, line_y,
                fade_color(hud_colors::ZONE_TEXT, alpha * 0.4f), 1.0f);
}

// ============================================================================
// Level up notification - center screen, fades out
// ============================================================================

inline void build_level_up_notification(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
    if (hud.level_up_timer <= 0.0f) return;

    // Fade out over the last 1.5 seconds
    float alpha = 1.0f;
    if (hud.level_up_timer < 1.5f) {
        alpha = hud.level_up_timer / 1.5f;
    }

    // Float upward as it fades
    float drift = (3.0f - hud.level_up_timer) * 15.0f;

    float cx = screen_w / 2.0f;
    float cy = screen_h / 2.0f - 60.0f - drift;

    // Background glow
    float glow_w = 260.0f;
    float glow_h = 80.0f;
    ui.add_filled_rect(cx - glow_w / 2, cy - 10,
                       glow_w, glow_h,
                       fade_color(0x60000000, alpha));

    // "LEVEL UP!" text
    float text_w = 9.0f * 16.0f;
    ui.add_text("LEVEL UP!", cx - text_w / 2, cy, 2.0f,
                fade_color(hud_colors::LEVEL_UP_TEXT, alpha));

    // Level number below
    char level_text[32];
    snprintf(level_text, sizeof(level_text), "Level %d", hud.level_up_level);
    float sub_w = static_cast<float>(std::strlen(level_text)) * 10.0f;
    ui.add_text(level_text, cx - sub_w / 2, cy + 36.0f, 1.2f,
                fade_color(hud_colors::LEVEL_UP_SUB, alpha));
}

// ============================================================================
// Loot feed - bottom right, fading text lines
// ============================================================================

inline void build_loot_feed(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
    if (hud.loot_feed.empty()) return;

    float padding = 15.0f;
    float line_h = 20.0f;
    float x = screen_w - padding - 250.0f;
    float base_y = screen_h - padding - 60.0f;

    // Draw from bottom to top (newest at bottom)
    int count = static_cast<int>(hud.loot_feed.size());
    for (int i = 0; i < count; i++) {
        const auto& entry = hud.loot_feed[i];
        float y = base_y - static_cast<float>(count - 1 - i) * line_h;

        // Fade out in last 1 second
        float alpha = 1.0f;
        if (entry.timer < 1.0f) {
            alpha = entry.timer;
        }

        ui.add_text(entry.text, x, y, 0.7f, fade_color(entry.color, alpha));
    }
}

// ============================================================================
// Minimap - top-right corner
// ============================================================================

inline void build_minimap(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
    float map_size = 180.0f;
    float padding = 10.0f;
    float mx = screen_w - map_size - padding;
    float my = padding;
    float cx = mx + map_size / 2;
    float cy = my + map_size / 2;
    float map_radius = map_size / 2 - 5;
    float world_radius = 2000.0f;  // world units visible on minimap

    // Background
    ui.add_filled_rect(mx, my, map_size, map_size, 0xCC111122);
    ui.add_rect_outline(mx, my, map_size, map_size, 0xFF4466AA, 2.0f);

    // Inner border for visual polish
    ui.add_rect_outline(mx + 3, my + 3, map_size - 6, map_size - 6, 0xFF223344, 1.0f);

    // Compass labels
    ui.add_text("N", cx - 3, my + 5, 0.7f, 0xFF8899BB);
    ui.add_text("S", cx - 3, my + map_size - 18, 0.7f, 0xFF8899BB);
    ui.add_text("W", mx + 5, cy - 6, 0.7f, 0xFF8899BB);
    ui.add_text("E", mx + map_size - 14, cy - 6, 0.7f, 0xFF8899BB);

    // Helper: world pos to minimap pos
    auto world_to_map = [&](float wx, float wz, float& out_x, float& out_y) -> bool {
        float dx = wx - hud.minimap.player_x;
        float dz = wz - hud.minimap.player_z;
        float map_dx = (dx / world_radius) * map_radius;
        float map_dz = (dz / world_radius) * map_radius;
        out_x = cx + map_dx;
        out_y = cy + map_dz;
        float dist_from_center = std::sqrt(map_dx * map_dx + map_dz * map_dz);
        return dist_from_center < map_radius - 3;
    };

    // Draw quest objective areas as translucent rectangles
    for (const auto& area : hud.minimap.objective_areas) {
        float ax = 0.0f, ay = 0.0f;
        if (world_to_map(area.world_x, area.world_z, ax, ay)) {
            float area_r = (area.radius / world_radius) * map_radius;
            area_r = std::max(4.0f, std::min(area_r, map_radius * 0.5f));
            ui.add_filled_rect(ax - area_r, ay - area_r, area_r * 2, area_r * 2, 0x3300DDFF);
            ui.add_rect_outline(ax - area_r, ay - area_r, area_r * 2, area_r * 2, 0x8800DDFF, 1.0f);
        }
    }

    // Draw map icons (NPCs, monsters, etc.)
    for (const auto& icon : hud.minimap.icons) {
        float ix = 0.0f, iy = 0.0f;
        if (world_to_map(icon.world_x, icon.world_z, ix, iy)) {
            if (icon.is_objective) {
                // Diamond shape for objectives (rotated square)
                float s = 5.0f;
                ui.add_line(ix, iy - s, ix + s, iy, icon.color, 2.0f);
                ui.add_line(ix + s, iy, ix, iy + s, icon.color, 2.0f);
                ui.add_line(ix, iy + s, ix - s, iy, icon.color, 2.0f);
                ui.add_line(ix - s, iy, ix, iy - s, icon.color, 2.0f);
            } else {
                // Dot for entities
                float dot = 3.0f;
                ui.add_filled_rect(ix - dot, iy - dot, dot * 2, dot * 2, icon.color);
            }
        }
    }

    // Player dot (white, always at center)
    float pd = 4.0f;
    ui.add_filled_rect(cx - pd, cy - pd, pd * 2, pd * 2, 0xFFFFFFFF);
    ui.add_rect_outline(cx - pd - 1, cy - pd - 1, pd * 2 + 2, pd * 2 + 2, 0xFF000000, 1.0f);

    // Zone name under minimap
    if (!hud.current_zone.empty()) {
        ui.add_text(hud.current_zone, mx + 5, my + map_size + 3, 0.7f, 0xFF8899BB);
    }
}

// ============================================================================
// Master HUD builder - renders all elements
// ============================================================================

inline void build_gameplay_hud(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
    build_xp_bar(ui, hud, screen_w, screen_h);
    build_mana_bar(ui, hud, screen_w, screen_h);
    build_minimap(ui, hud, screen_w, screen_h);
    build_gold_display(ui, hud, screen_w, screen_h);
    build_skill_bar(ui, hud, screen_w, screen_h);
    build_quest_tracker(ui, hud, screen_w, screen_h);
    build_zone_name(ui, hud, screen_w, screen_h);
    build_level_up_notification(ui, hud, screen_w, screen_h);
    build_loot_feed(ui, hud, screen_w, screen_h);
}
} // namespace mmo::client

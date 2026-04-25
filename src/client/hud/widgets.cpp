#include "client/hud/widgets.hpp"
#include "client/hud/hud_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mmo::client {

using engine::scene::UIScene;
using namespace engine::ui_colors;
using hud_layout::fade_color;
using hud_layout::bar_ratio;
using hud_layout::linear_fade;

// ============================================================================
// Health bar - bottom-left
// ============================================================================

void build_health_bar(UIScene& ui, const HUDState& hud, float /*screen_w*/, float screen_h) {
    float bar_width = 250.0f;
    float bar_height = 25.0f;
    float padding = 20.0f;
    float x = padding;
    float y = screen_h - padding - bar_height;

    // Frame
    ui.add_filled_rect(x - 2, y - 2, bar_width + 4, bar_height + 4, 0xFF000000);
    ui.add_rect_outline(x - 2, y - 2, bar_width + 4, bar_height + 4, BORDER, 1.0f);

    // Background
    ui.add_filled_rect(x, y, bar_width, bar_height, 0xFF1A0000);

    // Fill (color shifts green -> orange -> red as health drops)
    const float hp_ratio = bar_ratio(hud.health, hud.max_health);
    if (hp_ratio > 0.0f) {
        ui.add_filled_rect(x, y, bar_width * hp_ratio, bar_height,
                           hud_layout::health_bar_color(hp_ratio));
    }

    // Text
    char hp_text[32];
    snprintf(hp_text, sizeof(hp_text), "HP: %.0f/%.0f", hud.health, hud.max_health);
    ui.add_text(hp_text, x + 8, y + 5, 1.0f, WHITE);
}

// ============================================================================
// XP bar - below the health bar
// ============================================================================

void build_xp_bar(UIScene& ui, const HUDState& hud, float /*screen_w*/, float screen_h) {
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
    const float xp_ratio = bar_ratio(static_cast<float>(hud.xp),
                                     static_cast<float>(hud.xp_to_next_level));
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

void build_mana_bar(UIScene& ui, const HUDState& hud, float /*screen_w*/, float screen_h) {
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
    const float mana_ratio = bar_ratio(hud.mana, hud.max_mana);
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

void build_gold_display(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
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

void build_skill_bar(UIScene& ui, const HUDState& hud, MouseUI& mui, float screen_w, float screen_h) {
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
            const std::string display_name = hud_layout::truncate_with_ellipsis(slot.name, 7, '.');
            ui.add_text(display_name, sx + 2, y + slot_size + 2, 0.5f, hud_colors::SKILL_NAME);
        }

        // Register hit region only when the slot is actually usable.
        if (slot.available && slot.cooldown <= 0.0f) {
            mui.push_region(skill_slot_id(i), WidgetId::None, sx, y, slot_size, slot_size);
        }
    }
}

// ============================================================================
// Quest tracker - right side
// ============================================================================

void build_quest_tracker(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
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
            const std::string desc = hud_layout::truncate_with_ellipsis(obj.description, 28);
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

void build_zone_name(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
    if (hud.zone_display_timer <= 0.0f || hud.current_zone.empty()) return;

    // Full opacity for first 2.5s, then fade out over the last 1.5s.
    const float alpha = linear_fade(hud.zone_display_timer, 1.5f);

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

void build_level_up_notification(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
    if (hud.level_up_timer <= 0.0f) return;

    // Fade out over the last 1.5 seconds.
    const float alpha = linear_fade(hud.level_up_timer, 1.5f);

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

void build_loot_feed(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
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

        // Fade out over the last second of the entry's lifetime.
        const float alpha = linear_fade(entry.timer, 1.0f);

        ui.add_text(entry.text, x, y, 0.7f, fade_color(entry.color, alpha));
    }
}

// ============================================================================
// Minimap - top-right corner
// ============================================================================

void build_minimap(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
    const float map_size = 180.0f;
    const float padding = 10.0f;
    const float mx = screen_w - map_size - padding;
    const float my = padding;
    const float cx = mx + map_size / 2;
    const float cy = my + map_size / 2;
    const float map_radius = map_size / 2 - 5;

    hud_layout::MinimapView view;
    view.center_x = cx;
    view.center_y = cy;
    view.map_radius = map_radius;
    view.world_radius = 2000.0f;  // world units visible on minimap
    view.player_world_x = hud.minimap.player_x;
    view.player_world_z = hud.minimap.player_z;

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

    // Draw quest objective areas as translucent rectangles
    for (const auto& area : hud.minimap.objective_areas) {
        const auto p = hud_layout::world_to_minimap(view, area.world_x, area.world_z);
        if (p.in_bounds) {
            const float area_r = hud_layout::minimap_area_pixel_radius(
                area.radius, view.world_radius, map_radius);
            ui.add_filled_rect(p.x - area_r, p.y - area_r, area_r * 2, area_r * 2, 0x3300DDFF);
            ui.add_rect_outline(p.x - area_r, p.y - area_r, area_r * 2, area_r * 2, 0x8800DDFF, 1.0f);
        }
    }

    // Draw map icons (NPCs, monsters, etc.)
    for (const auto& icon : hud.minimap.icons) {
        const auto p = hud_layout::world_to_minimap(view, icon.world_x, icon.world_z);
        if (!p.in_bounds) continue;
        if (icon.is_objective) {
            // Diamond shape for objectives (rotated square)
            const float s = 5.0f;
            ui.add_line(p.x, p.y - s, p.x + s, p.y, icon.color, 2.0f);
            ui.add_line(p.x + s, p.y, p.x, p.y + s, icon.color, 2.0f);
            ui.add_line(p.x, p.y + s, p.x - s, p.y, icon.color, 2.0f);
            ui.add_line(p.x - s, p.y, p.x, p.y - s, icon.color, 2.0f);
        } else {
            const float dot = 3.0f;
            ui.add_filled_rect(p.x - dot, p.y - dot, dot * 2, dot * 2, icon.color);
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
// Status effect icons - row of small tiles above the bottom-left HP bar
// ============================================================================

void build_status_effect_row(UIScene& ui, const HUDState& hud, float /*screen_w*/, float screen_h) {
    uint16_t m = hud.effects_mask;
    if (m == 0) return;

    // Icon tiles next to each other, just above the XP bar (which sits above
    // the HP bar at the bottom-left).
    float icon_size = 22.0f;
    float icon_gap = 4.0f;
    float x = 20.0f + 250.0f + 10.0f;  // right of the HP/XP stack
    float y = screen_h - 70.0f - icon_size - 10.0f;

    struct IconSpec { uint16_t bit; uint32_t color; const char* label; };
    static const IconSpec specs[] = {
        { status_bits::INVULN,       0xFF44FFFF, "INV" },  // cyan
        { status_bits::SHIELD,       0xFF00DDFF, "SHD" },  // gold
        { status_bits::DAMAGE_BOOST, 0xFF3388FF, "DMG" },  // orange
        { status_bits::SPEED_BOOST,  0xFF00FF88, "SPD" },  // green
        { status_bits::DEF_BOOST,    0xFFDDDD00, "DEF" },  // teal
        { status_bits::STUN,         0xFF4444FF, "STN" },  // red
        { status_bits::ROOT,         0xFF2266AA, "RT" },
        { status_bits::SLOW,         0xFFCC66FF, "SLO" },  // purple
        { status_bits::BURN,         0xFF0000FF, "BRN" },  // red
    };

    for (const auto& s : specs) {
        if ((m & s.bit) == 0) continue;
        ui.add_filled_rect(x, y, icon_size, icon_size, 0xAA000000);
        ui.add_rect_outline(x, y, icon_size, icon_size, s.color, 2.0f);
        ui.add_filled_rect(x + 3, y + 3, icon_size - 6, icon_size - 6, s.color & 0x80FFFFFF);
        ui.add_text(s.label, x + 3, y + 5, 0.55f, 0xFFFFFFFF);
        x += icon_size + icon_gap;
    }
}

// ============================================================================
// Master HUD builder - renders all elements
// chat / vendor / party widgets live in their own translation units
// (hud/chat_window.cpp, hud/vendor_window.cpp, hud/party_widgets.cpp).
// ============================================================================

void build_gameplay_hud(UIScene& ui, const HUDState& hud, MouseUI& mui, float screen_w, float screen_h) {
    // Health bar is rendered in game.cpp's build_gameplay_ui (from entity data)
    build_xp_bar(ui, hud, screen_w, screen_h);
    build_mana_bar(ui, hud, screen_w, screen_h);
    build_minimap(ui, hud, screen_w, screen_h);
    build_gold_display(ui, hud, screen_w, screen_h);
    build_skill_bar(ui, hud, mui, screen_w, screen_h);
    build_quest_tracker(ui, hud, screen_w, screen_h);
    build_zone_name(ui, hud, screen_w, screen_h);
    build_level_up_notification(ui, hud, screen_w, screen_h);
    build_loot_feed(ui, hud, screen_w, screen_h);
    build_party_frames(ui, hud, mui, screen_w, screen_h);
    build_status_effect_row(ui, hud, screen_w, screen_h);
    build_chat_window(ui, hud, mui, screen_w, screen_h);
    build_vendor_window(ui, hud, mui, screen_w, screen_h);
    build_party_invite_popup(ui, hud, screen_w, screen_h);
}

} // namespace mmo::client

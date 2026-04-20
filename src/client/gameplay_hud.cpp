#include "gameplay_hud.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mmo::client {

using engine::scene::UIScene;
using namespace engine::ui_colors;

// ============================================================================
// Helper: compute alpha-faded color
// ============================================================================

uint32_t fade_color(uint32_t color, float alpha) {
    if (alpha >= 1.0f) return color;
    if (alpha <= 0.0f) return color & 0x00FFFFFF;
    uint32_t a = static_cast<uint32_t>(((color >> 24) & 0xFF) * alpha);
    return (a << 24) | (color & 0x00FFFFFF);
}

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

    // Fill
    float hp_ratio = (hud.max_health > 0) ? hud.health / hud.max_health : 0.0f;
    hp_ratio = std::min(hp_ratio, 1.0f);
    if (hp_ratio > 0.0f) {
        // Color shifts from green to red as health decreases
        uint32_t fill_color = 0xFF0000FF; // green in ABGR
        if (hp_ratio < 0.3f) fill_color = 0xFF0000CC; // red
        else if (hp_ratio < 0.6f) fill_color = 0xFF00AAFF; // yellow/orange
        ui.add_filled_rect(x, y, bar_width * hp_ratio, bar_height, fill_color);
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

void build_skill_bar(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
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

void build_zone_name(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
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

void build_level_up_notification(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
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

void build_minimap(UIScene& ui, const HUDState& hud, float screen_w, float /*screen_h*/) {
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
// Chat window - bottom-left, above the skill bar
// ============================================================================

static uint32_t chat_channel_color(uint8_t channel) {
    switch (channel) {
        case 0: return 0xFFCCCCCC;  // Say - light gray
        case 1: return 0xFF88FFFF;  // Zone - cyan
        case 2: return 0xFF00CCFF;  // Global - yellow
        case 3: return 0xFF0088FF;  // System - orange
        case 4: return 0xFFFF88FF;  // Whisper - magenta
        default: return 0xFFCCCCCC;
    }
}

static const char* chat_channel_prefix(uint8_t channel) {
    switch (channel) {
        case 0: return "[Say]";
        case 1: return "[Zone]";
        case 2: return "[Global]";
        case 3: return "[System]";
        case 4: return "[Whisper]";
        default: return "";
    }
}

void build_chat_window(UIScene& ui, const HUDState& hud, float /*screen_w*/, float screen_h) {
    const auto& chat = hud.chat;
    float padding = 20.0f;
    float width = 460.0f;
    float line_height = 14.0f;
    int visible = ChatState::VISIBLE_LINES;
    // Position: above skill bar (skill bar is ~80px tall at the bottom)
    float height = visible * line_height + 10.0f + (chat.input_active ? 22.0f : 0.0f);
    float x = padding;
    float y = screen_h - 160.0f - height;

    // Background
    ui.add_filled_rect(x, y, width, height, 0xAA000000);
    ui.add_rect_outline(x, y, width, height, 0xFF555555, 1.0f);

    // Recent chat lines
    int start = static_cast<int>(chat.lines.size()) - visible;
    if (start < 0) start = 0;
    float ty = y + 6.0f;
    for (int i = start; i < static_cast<int>(chat.lines.size()); ++i) {
        const auto& line = chat.lines[i];
        uint32_t color = chat_channel_color(line.channel);
        std::string prefix = chat_channel_prefix(line.channel);
        std::string rendered;
        if (line.channel == 3) {
            rendered = prefix + std::string(" ") + line.text;
        } else {
            rendered = prefix + std::string(" ") + line.sender + ": " + line.text;
        }
        if (rendered.size() > 80) rendered = rendered.substr(0, 80) + "...";
        ui.add_text(rendered, x + 6, ty, 0.75f, color);
        ty += line_height;
    }

    // Input line
    if (chat.input_active) {
        float iy = y + height - 20.0f;
        ui.add_filled_rect(x + 4, iy, width - 8, 18.0f, 0xDD222233);
        ui.add_rect_outline(x + 4, iy, width - 8, 18.0f, 0xFF666677, 1.0f);
        std::string prefix = chat_channel_prefix(chat.selected_channel);
        std::string display = prefix + std::string(" > ") + chat.input_buffer + "_";
        ui.add_text(display, x + 8, iy + 3, 0.8f, 0xFFFFFFFF);
    } else {
        ui.add_text("[Enter] to chat", x + 6, y + height - 14.0f, 0.65f, 0xFF888899);
    }
}

// ============================================================================
// Vendor window - center of screen
// ============================================================================

void build_vendor_window(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
    const auto& v = hud.vendor;
    if (!v.visible) return;

    float w = 460.0f;
    float h = 360.0f;
    float x = (screen_w - w) * 0.5f;
    float y = (screen_h - h) * 0.5f;

    // Panel background
    ui.add_filled_rect(x, y, w, h, 0xEE1A1A22);
    ui.add_rect_outline(x, y, w, h, 0xFF999999, 2.0f);

    // Title bar
    ui.add_filled_rect(x, y, w, 26.0f, 0xFF222244);
    std::string title = v.vendor_name.empty() ? std::string("Vendor") : v.vendor_name;
    ui.add_text(title, x + 12, y + 6, 1.0f, 0xFFFFEECC);
    ui.add_text("[Esc] close  [Tab] buy/sell", x + w - 200, y + 6, 0.7f, 0xFFAAAAAA);

    // Mode tab indicator
    float tab_y = y + 34.0f;
    ui.add_text(v.buying ? "BUY" : "SELL", x + 12, tab_y, 0.9f,
                v.buying ? 0xFF00DDFF : 0xFF66FFFF);

    // Gold
    char gbuf[48];
    snprintf(gbuf, sizeof(gbuf), "Gold: %d", 0); // Server sends gold separately; UI shows from HUDState.gold
    (void)gbuf;
    char gold_buf[48];
    snprintf(gold_buf, sizeof(gold_buf), "Gold: %d", hud.gold);
    ui.add_text(gold_buf, x + w - 120, tab_y, 0.85f, 0xFF00DDFF);

    // List of items
    float list_y = y + 58.0f;
    float row_h = 22.0f;
    int max_rows = 12;
    int count = static_cast<int>(v.stock.size());
    int cursor = std::max(0, std::min(v.cursor, count - 1));

    for (int i = 0; i < count && i < max_rows; ++i) {
        float row_y = list_y + i * row_h;
        bool selected = (i == cursor);
        uint32_t row_color = selected ? 0xFF334466 : 0xFF222233;
        ui.add_filled_rect(x + 10, row_y, w - 20, row_h - 2, row_color);
        const auto& slot = v.stock[i];
        uint32_t name_color = 0xFFCCCCCC;
        if (slot.rarity == "uncommon")       name_color = 0xFF00CC00;
        else if (slot.rarity == "rare")      name_color = 0xFF0088FF;
        else if (slot.rarity == "epic")      name_color = 0xFFCC00CC;
        else if (slot.rarity == "legendary") name_color = 0xFF00AAFF;
        char buf[128];
        if (slot.stock < 0) {
            snprintf(buf, sizeof(buf), "%s", slot.item_name.c_str());
        } else {
            snprintf(buf, sizeof(buf), "%s (stock: %d)", slot.item_name.c_str(), slot.stock);
        }
        ui.add_text(buf, x + 18, row_y + 4, 0.85f, name_color);
        char price_buf[32];
        snprintf(price_buf, sizeof(price_buf), "%d g", slot.price);
        ui.add_text(price_buf, x + w - 70, row_y + 4, 0.85f, 0xFF00DDFF);
    }

    ui.add_text("[Up/Down] select  [Enter] purchase", x + 12, y + h - 22, 0.7f, 0xFFAAAAAA);
}

// ============================================================================
// Master HUD builder - renders all elements
// ============================================================================

void build_gameplay_hud(UIScene& ui, const HUDState& hud, float screen_w, float screen_h) {
    // Health bar is rendered in game.cpp's build_gameplay_ui (from entity data)
    build_xp_bar(ui, hud, screen_w, screen_h);
    build_mana_bar(ui, hud, screen_w, screen_h);
    build_minimap(ui, hud, screen_w, screen_h);
    build_gold_display(ui, hud, screen_w, screen_h);
    build_skill_bar(ui, hud, screen_w, screen_h);
    build_quest_tracker(ui, hud, screen_w, screen_h);
    build_zone_name(ui, hud, screen_w, screen_h);
    build_level_up_notification(ui, hud, screen_w, screen_h);
    build_loot_feed(ui, hud, screen_w, screen_h);
    build_chat_window(ui, hud, screen_w, screen_h);
    build_vendor_window(ui, hud, screen_w, screen_h);
}

} // namespace mmo::client

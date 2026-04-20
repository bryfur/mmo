#pragma once

#include "engine/scene/ui_scene.hpp"
#include "client/ui_colors.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace mmo::client {

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

// ============================================================================
// Chat
// ============================================================================

struct ChatLine {
    uint8_t channel = 0;
    std::string sender;
    std::string text;
};

struct ChatState {
    static constexpr int MAX_LINES = 80;
    static constexpr int VISIBLE_LINES = 8;
    std::vector<ChatLine> lines;
    bool input_active = false;
    std::string input_buffer;
    uint8_t selected_channel = 1; // Default: Zone

    void add_line(uint8_t channel, const std::string& sender, const std::string& text) {
        lines.push_back({channel, sender, text});
        if (static_cast<int>(lines.size()) > MAX_LINES) {
            lines.erase(lines.begin(), lines.begin() + (lines.size() - MAX_LINES));
        }
    }
};

// ============================================================================
// Vendor (shop)
// ============================================================================

struct VendorStockSlot {
    std::string item_id;
    std::string item_name;
    std::string rarity = "common";
    int price = 0;
    int stock = -1;
};

// ============================================================================
// Crafting
// ============================================================================

struct CraftIngredientClient {
    std::string item_id;
    int count = 0;
};

struct CraftRecipe {
    std::string id;
    std::string name;
    std::string output_item_id;
    int output_count = 1;
    int gold_cost = 0;
    int required_level = 1;
    std::vector<CraftIngredientClient> ingredients;
};

struct CraftingState {
    std::vector<CraftRecipe> recipes;
    std::string last_result;
    float last_result_timer = 0.0f;
    uint32_t last_result_color = 0xFFFFFFFF;
};

// ============================================================================
// Party
// ============================================================================

struct PartyMember {
    uint32_t player_id = 0;
    std::string name;
    uint8_t player_class = 0;
    uint8_t level = 1;
    float health = 100.0f;
    float max_health = 100.0f;
    float mana = 0.0f;
    float max_mana = 0.0f;
};

struct PartyState {
    uint32_t leader_id = 0;
    std::vector<PartyMember> members;

    // Pending invite shown as a popup
    uint32_t pending_inviter_id = 0;
    std::string pending_inviter_name;

    bool has_party() const { return !members.empty(); }
    void clear() { leader_id = 0; members.clear(); }
};

struct VendorState {
    bool visible = false;
    uint32_t npc_id = 0;
    std::string vendor_name;
    float buy_mult = 4.0f;
    float sell_mult = 0.25f;
    std::vector<VendorStockSlot> stock;
    int cursor = 0;               // Index into stock for buying
    int sell_cursor = -1;         // Inventory slot index for selling (-1 = none)
    bool buying = true;           // true = buy mode, false = sell mode

    void close() { visible = false; stock.clear(); cursor = 0; sell_cursor = -1; }
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

    // Chat
    ChatState chat;

    // Vendor
    VendorState vendor;

    // Party
    PartyState party;

    // Crafting
    CraftingState crafting;

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
// Function declarations
// ============================================================================

uint32_t fade_color(uint32_t color, float alpha);

void build_health_bar(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_xp_bar(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_mana_bar(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_gold_display(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_skill_bar(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_quest_tracker(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_zone_name(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_level_up_notification(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_loot_feed(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_minimap(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_chat_window(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_vendor_window(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_party_frames(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_party_invite_popup(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);
void build_gameplay_hud(engine::scene::UIScene& ui, const HUDState& hud, float screen_w, float screen_h);

} // namespace mmo::client

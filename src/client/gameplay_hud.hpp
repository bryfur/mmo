#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mmo::client {

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

// HUD state aggregated for rendering
struct HUDState {
    // Health/mana
    float health = 100.0f;
    float max_health = 100.0f;
    float mana = 100.0f;
    float max_mana = 100.0f;

    // Level/XP
    uint16_t level = 1;
    uint32_t xp = 0;
    uint32_t xp_to_next = 100;

    // Quest tracker
    std::vector<TrackedQuest> tracked_quests;

    // Skill bar (up to 8 slots)
    static constexpr int MAX_SKILL_SLOTS = 8;
    SkillBarSlot skill_slots[MAX_SKILL_SLOTS] = {};

    // Floating damage numbers
    std::vector<DamageNumber> damage_numbers;

    // Notifications
    std::vector<Notification> notifications;

    // NPC dialogue
    NPCDialogueState dialogue;
};

} // namespace mmo::client

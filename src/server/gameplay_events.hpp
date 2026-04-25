#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace mmo::server::events {

// Per-player progression events. Each carries the player_id of the recipient
// (the killer / earner / quest-doer); fan-out is one event per recipient.
struct XPGain {
    uint32_t player_id = 0;
    int xp_gained = 0;
    int total_xp = 0;
    int xp_to_next = 0;
    int new_level = 0;
};

struct LevelUp {
    uint32_t player_id = 0;
    int new_level = 0;
    float new_max_health = 0.0f;
    float new_damage = 0.0f;
    int total_xp = 0;
    int xp_to_next = 0;
};

struct LootItem {
    std::string name;
    std::string rarity;
    int count = 0;
};

struct LootDrop {
    uint32_t player_id = 0;
    int loot_gold = 0;
    int total_gold = 0;
    std::vector<LootItem> items;
};

struct ZoneChange {
    uint32_t player_id = 0;
    std::string zone_name;
};

struct QuestProgress {
    uint32_t player_id = 0;
    std::string quest_id;
    uint8_t objective_index = 0;
    int current = 0;
    int required = 0;
    bool complete = false;
};

struct QuestComplete {
    uint32_t player_id = 0;
    std::string quest_id;
    std::string quest_name;
};

struct InventoryUpdate {
    uint32_t player_id = 0;
};

// Broadcast events (no recipient — server fans out via AOI from a source pos).
struct CombatHitEvent {
    uint32_t attacker_id = 0;
    uint32_t target_id = 0;
    float damage = 0.0f;
};

struct EntityDeathEvent {
    uint32_t dead_id = 0;
    uint32_t killer_id = 0;
};

using GameplayEvent = std::variant<
    XPGain,
    LevelUp,
    LootDrop,
    ZoneChange,
    QuestProgress,
    QuestComplete,
    InventoryUpdate,
    CombatHitEvent,
    EntityDeathEvent
>;

} // namespace mmo::server::events

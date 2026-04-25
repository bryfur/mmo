#pragma once

#include "database.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mmo::server::persistence {

struct InventoryRow {
    std::string item_id;
    int count = 0;
};

struct ActiveQuestRow {
    std::string quest_id;
    // JSON-encoded objectives — repository is intentionally schema-agnostic
    // about quest internals to avoid coupling to game_components.hpp.
    std::string objectives_json;
};

// All persistable player state. Repositories convert between this and the live
// ECS components in the server's higher-level adapter (player_persistence.cpp).
struct PlayerSnapshot {
    std::string name;
    int player_class = 0;
    int level = 1;
    int xp = 0;
    int gold = 0;

    float pos_x = 0, pos_y = 0, pos_z = 0;
    float rotation = 0;

    float health = 100, max_health = 100;
    float mana = 0, max_mana = 0, mana_regen = 0;

    int talent_points = 0;

    // True if the player was dead at save time. On load, the server forces
    // a town respawn rather than silently reviving in place. Default false
    // so existing rows (pre-migration) load as alive.
    bool was_dead = false;

    std::string equipped_weapon;
    std::string equipped_armor;

    std::vector<InventoryRow> inventory;
    std::vector<std::string> unlocked_talents;
    std::vector<std::string> completed_quests;
    std::vector<ActiveQuestRow> active_quests;

    int64_t last_seen_unix = 0;
};

class PlayerRepository {
public:
    explicit PlayerRepository(Database& db) : db_(db) {}

    /// Load a player by name. Returns nullopt if no row exists.
    std::optional<PlayerSnapshot> load(const std::string& name);

    /// Insert-or-replace a snapshot. Wraps everything in one transaction.
    void save(const PlayerSnapshot& snap);

    /// Quickly check if a player exists (without loading the full snapshot).
    bool exists(const std::string& name);

private:
    Database& db_;
};

} // namespace mmo::server::persistence

#pragma once

#include "game_types.hpp"
#include "protocol/protocol.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>

namespace mmo::server {


struct ClassConfig {
    std::string name;
    std::string model;
    std::string animation;  // Animation config name (e.g. "humanoid")
    float health = 100.0f;
    float damage = 10.0f;
    float attack_range = 50.0f;
    float attack_cooldown = 1.0f;
    uint32_t color = 0xFFFFFFFF;
    uint32_t select_color = 0xFFFFFFFF;
    uint32_t ui_color = 0xFFFFFFFF;
    std::string short_desc;
    std::string desc_line1;
    std::string desc_line2;
    bool shows_reticle = false;
    std::string effect_type;
    float cone_angle = 0.5f;
    float speed = 200.0f;
    float size = 32.0f;
};

struct MonsterConfig {
    float size = 36.0f;
    float speed = 100.0f;
    float health = 100.0f;
    float damage = 15.0f;
    float attack_range = 50.0f;
    float attack_cooldown = 1.2f;
    float aggro_range = 300.0f;
    int count = 10;
    std::string model;
    std::string animation;  // Animation config name (e.g. "humanoid")
    uint32_t color = 0xFF4444FF;
};

struct TownNPCConfig {
    std::string type;
    float x = 0.0f;
    float y = 0.0f;
    std::string name;
    bool wanders = false;
    std::string model;
    uint32_t color = 0xFFAAAAAA;
};

struct BuildingConfig {
    std::string type;
    std::string model;
    float x = 0.0f;
    float y = 0.0f;
    std::string name;
    float rotation = 0.0f;
    float target_size = 100.0f;
};

struct WallConfig {
    std::string model;
    float distance = 500.0f;
    float spacing = 35.0f;
    float gate_width = 80.0f;
    float target_size = 60.0f;
};

struct TowerConfig {
    std::string model;
    float target_size = 140.0f;
};

// ============================================================================
// Monster Types (varied monsters from monster_types.json)
// ============================================================================

struct MonsterTypeConfig {
    std::string id;
    std::string name;
    std::string model;
    std::string animation;
    uint32_t color = 0xFF4444FF;
    float health = 100.0f;
    float damage = 15.0f;
    float attack_range = 50.0f;
    float attack_cooldown = 1.2f;
    float aggro_range = 300.0f;
    float speed = 100.0f;
    float size = 36.0f;
    int xp_reward = 25;
    int gold_reward = 5;
    int level = 1;
    std::string description;
};

// ============================================================================
// Zones (from zones.json)
// ============================================================================

struct ZoneConfig {
    std::string id;
    std::string name;
    float center_x = 0.0f;
    float center_z = 0.0f;
    float radius = 1000.0f;
    int level_min = 1;
    int level_max = 5;
    float monster_density = 0.5f;
    std::vector<std::string> monster_types;
    std::string description;
};

// ============================================================================
// Items (from items.json)
// ============================================================================

struct ItemStats {
    float damage_bonus = 0.0f;
    float attack_speed_bonus = 0.0f;
    float health_bonus = 0.0f;
    float defense = 0.0f;
    float speed_bonus = 0.0f;
    // Consumable effects
    float heal_amount = 0.0f;
    float buff_duration = 0.0f;
    float buff_multiplier = 0.0f;
};

struct ItemConfig {
    std::string id;
    std::string name;
    std::string type;      // weapon, armor, consumable, material
    std::string subtype;
    std::string rarity;    // common, uncommon, rare, epic, legendary
    int level_req = 1;
    std::vector<std::string> classes;
    ItemStats stats;
    std::string description;
    int sell_value = 0;
    int stack_size = 1;
};

// ============================================================================
// Loot Tables (from loot_tables.json)
// ============================================================================

struct LootDropConfig {
    std::string item_id;
    float chance = 0.1f;
    int count_min = 1;
    int count_max = 1;
};

struct LootTableConfig {
    std::string id;
    std::string monster_type;
    int gold_min = 0;
    int gold_max = 10;
    std::vector<LootDropConfig> drops;
};

// ============================================================================
// Leveling (from leveling.json)
// ============================================================================

struct ClassGrowth {
    float health = 0.0f;
    float damage = 0.0f;
    float speed = 0.0f;
    float attack_range = 0.0f;
    float attack_cooldown_reduction = 0.0f;
};

struct LevelingConfig {
    int max_level = 20;
    std::vector<int> xp_curve;  // XP needed to reach each level
    std::vector<ClassGrowth> class_growth;  // indexed by PlayerClass
    float death_xp_loss_percent = 5.0f;
};

// ============================================================================
// Skills (from skills.json)
// ============================================================================

struct SkillConfig {
    std::string id;
    std::string name;
    std::string class_name;
    int unlock_level = 1;
    float cooldown = 10.0f;
    std::string description;
    std::string effect_type;
    float damage_multiplier = 1.0f;
    float range = 100.0f;
    float mana_cost = 20.0f;
    float cone_angle = 0.5f;
    float heal_percent = 0.0f;
    float duration = 0.0f;

    // Status effect fields
    float stun_duration = 0.0f;
    float slow_percent = 0.0f;
    float slow_duration = 0.0f;
    float freeze_duration = 0.0f;
    float burn_duration = 0.0f;
    float burn_damage = 0.0f;
    float root_duration = 0.0f;
    float buff_duration = 0.0f;
    float damage_reduction = 0.0f;
    float invulnerable_duration = 0.0f;
    float speed_boost = 0.0f;
    float speed_boost_duration = 0.0f;
    float lifesteal_percent = 0.0f;
    float enemy_damage_reduction = 0.0f;
    float debuff_duration = 0.0f;
};

// ============================================================================
// Talents (from talents.json)
// ============================================================================

struct TalentEffect {
    float damage_mult = 1.0f;
    float speed_mult = 1.0f;
    float health_mult = 1.0f;
    float crit_chance = 0.0f;
    float kill_heal_pct = 0.0f;
    float defense_mult = 1.0f;
    float mana_mult = 1.0f;
    float cooldown_mult = 1.0f;
};

struct TalentConfig {
    std::string id;
    std::string name;
    int tier = 1;
    std::string description;
    TalentEffect effect;
    std::string prerequisite;  // prerequisite talent id
};

struct TalentBranch {
    std::string name;
    std::string description;
    std::vector<TalentConfig> talents;
};

struct TalentTreeConfig {
    std::string class_name;
    std::vector<TalentBranch> branches;
};

// ============================================================================
// Quests (from quests.json)
// ============================================================================

struct QuestObjectiveConfig {
    std::string type;       // kill, visit, gather
    std::string target;
    int count = 1;
    std::string description;
    float location_x = 0.0f;
    float location_z = 0.0f;
    float radius = 100.0f;
};

struct QuestRewards {
    int xp = 0;
    int gold = 0;
    std::string item_reward;
};

struct QuestDialogue {
    std::string offer;
    std::string progress;
    std::string complete;
};

struct QuestConfig {
    std::string id;
    std::string name;
    std::string description;
    std::string giver_npc;
    std::string giver_type;
    std::string type;
    int min_level = 1;
    std::string prerequisite_quest;
    std::vector<QuestObjectiveConfig> objectives;
    QuestRewards rewards;
    QuestDialogue dialogue;
    bool repeatable = false;
};

// ============================================================================
// Server Config
// ============================================================================

struct ServerConfig {
    float tick_rate = 60.0f;
    uint16_t default_port = 7777;
};

struct WorldConfig {
    float width = 32000.0f;
    float height = 32000.0f;
};

struct NetworkConfig {
    float player_view_distance = 1500.0f;
    float npc_view_distance = 1200.0f;
    float town_npc_view_distance = 1000.0f;
    float building_view_distance = 3000.0f;
    float environment_view_distance = 2000.0f;
    float spatial_grid_cell_size = 500.0f;

    float max_view_distance() const {
        return std::max({player_view_distance, npc_view_distance, town_npc_view_distance,
                         building_view_distance, environment_view_distance});
    }

    // Helper to get view distance for an entity type
    float get_view_distance_for_type(mmo::protocol::EntityType type) const {
        switch(type) {
            case mmo::protocol::EntityType::Building:
                return building_view_distance;
            case mmo::protocol::EntityType::Environment:
                return environment_view_distance;
            case mmo::protocol::EntityType::Player:
                return player_view_distance;
            case mmo::protocol::EntityType::NPC:
                return npc_view_distance;
            case mmo::protocol::EntityType::TownNPC:
                return town_npc_view_distance;
            default:
                return player_view_distance;
        }
    }
};

class GameConfig {
public:
    bool load(const std::string& data_dir);

    // Server
    const ServerConfig& server() const { return server_; }

    // World
    const WorldConfig& world() const { return world_; }

    // Network
    const NetworkConfig& network() const { return network_; }

    // Classes
    const std::vector<ClassConfig>& classes() const { return classes_; }
    const ClassConfig& get_class(int index) const;
    int class_count() const { return static_cast<int>(classes_.size()); }

    // NPCs
    const MonsterConfig& monster() const { return monster_; }
    const std::vector<TownNPCConfig>& town_npcs() const { return town_npcs_; }

    // Buildings
    const std::vector<BuildingConfig>& buildings() const { return buildings_; }

    // Town
    const WallConfig& wall() const { return wall_; }
    const TowerConfig& corner_towers() const { return corner_towers_; }
    float safe_zone_radius() const { return safe_zone_radius_; }

    // Monster Types
    const std::vector<MonsterTypeConfig>& monster_types() const { return monster_types_; }
    const MonsterTypeConfig* find_monster_type(const std::string& id) const;

    // Zones
    const std::vector<ZoneConfig>& zones() const { return zones_; }
    const ZoneConfig* find_zone_at(float x, float z) const;

    // Items
    const std::vector<ItemConfig>& items() const { return items_; }
    const ItemConfig* find_item(const std::string& id) const;

    // Loot Tables
    const std::vector<LootTableConfig>& loot_tables() const { return loot_tables_; }
    const LootTableConfig* find_loot_table(const std::string& monster_type) const;

    // Leveling
    const LevelingConfig& leveling() const { return leveling_; }

    // Skills
    const std::vector<SkillConfig>& skills() const { return skills_; }
    std::vector<const SkillConfig*> skills_for_class(const std::string& class_name) const;
    const SkillConfig* find_skill(const std::string& id) const;

    // Talents
    const std::vector<TalentTreeConfig>& talent_trees() const { return talent_trees_; }
    const TalentConfig* find_talent(const std::string& id) const;

    // Quests
    const std::vector<QuestConfig>& quests() const { return quests_; }
    const QuestConfig* find_quest(const std::string& id) const;
    std::vector<const QuestConfig*> quests_for_npc(const std::string& npc_type) const;

    // Build a ClassInfo for sending to clients
    mmo::protocol::ClassInfo build_class_info(int index) const;

private:
    bool load_server(const std::string& path);
    bool load_world(const std::string& path);
    bool load_network(const std::string& path);
    bool load_classes(const std::string& path);
    bool load_monsters(const std::string& path);
    bool load_town(const std::string& path);
    bool load_monster_types(const std::string& path);
    bool load_zones(const std::string& path);
    bool load_items(const std::string& path);
    bool load_loot_tables(const std::string& path);
    bool load_leveling(const std::string& path);
    bool load_skills(const std::string& path);
    bool load_talents(const std::string& path);
    bool load_quests(const std::string& path);

    static uint32_t parse_color(const std::string& s);

    ServerConfig server_;
    WorldConfig world_;
    NetworkConfig network_;
    std::vector<ClassConfig> classes_;
    MonsterConfig monster_;
    std::vector<TownNPCConfig> town_npcs_;
    std::vector<BuildingConfig> buildings_;
    WallConfig wall_;
    TowerConfig corner_towers_;
    float safe_zone_radius_ = 400.0f;

    // New gameplay configs
    std::vector<MonsterTypeConfig> monster_types_;
    std::vector<ZoneConfig> zones_;
    std::vector<ItemConfig> items_;
    std::vector<LootTableConfig> loot_tables_;
    LevelingConfig leveling_;
    std::vector<SkillConfig> skills_;
    std::vector<TalentTreeConfig> talent_trees_;
    std::vector<QuestConfig> quests_;
};

} // namespace mmo::server

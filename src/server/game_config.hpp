#pragma once

#include "game_types.hpp"
#include "protocol/protocol.hpp"
#include <algorithm>
#include <string>
#include <unordered_map>
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
    // Per-class mana (loaded from skills.json mana_system section)
    float base_mana = 100.0f;
    float mana_regen = 5.0f;
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

    // AoE / multi-target fields
    float aoe_radius = 0.0f;
    int projectile_count = 1;
    int chain_targets = 0;
    float chain_range = 0.0f;

    // Conditional damage fields
    float health_threshold = 0.0f;
    float damage_multiplier_below_threshold = 0.0f;

    // Bleed/DoT fields
    float bleed_percent = 0.0f;
    float bleed_duration = 0.0f;

    // Self-buff fields
    float damage_bonus = 0.0f;
    float attack_speed_bonus = 0.0f;
    float self_shield_percent = 0.0f;
};

// ============================================================================
// Talents (from talents.json)
// ============================================================================

struct TalentEffect {
    // === Existing base stats ===
    float damage_mult = 1.0f;
    float speed_mult = 1.0f;
    float health_mult = 1.0f;
    float crit_chance = 0.0f;
    float kill_heal_pct = 0.0f;
    float defense_mult = 1.0f;
    float mana_mult = 1.0f;
    float cooldown_mult = 1.0f;
    float attack_speed_mult = 1.0f;

    // === Additional stat modifiers ===
    float crit_damage_mult = 1.0f;
    float mana_cost_mult = 1.0f;
    float skill_damage_mult = 1.0f;
    float attack_range_bonus = 0.0f;
    float attack_range_mult = 1.0f;
    float healing_received_mult = 1.0f;
    float global_cdr = 0.0f;

    // === Boolean flags (OR'd when aggregating) ===
    bool cc_immunity = false;
    bool has_cheat_death = false;
    bool has_avenge = false;

    // === On-hit procs ===
    float slow_on_hit_chance = 0.0f;
    float slow_on_hit_value = 0.0f;
    float slow_on_hit_dur = 0.0f;
    float burn_on_hit_pct = 0.0f;
    float burn_on_hit_dur = 0.0f;
    float poison_on_hit_pct = 0.0f;
    float poison_on_hit_dur = 0.0f;
    float mana_on_hit_pct = 0.0f;
    float hit_speed_bonus = 0.0f;
    float hit_speed_dur = 0.0f;

    // === Kill effects ===
    float kill_explosion_pct = 0.0f;
    float kill_explosion_radius = 0.0f;
    float kill_damage_bonus = 0.0f;
    float kill_damage_dur = 0.0f;
    float kill_speed_bonus = 0.0f;
    float kill_speed_dur = 0.0f;
    float burn_spread_radius = 0.0f;

    // === Cheat death ===
    float cheat_death_hp = 0.1f;
    float cheat_death_cooldown = 60.0f;

    // === Damage reflect ===
    float reflect_percent = 0.0f;

    // === Stationary mechanics ===
    float stationary_damage_mult = 1.0f;
    float stationary_damage_reduction = 0.0f;
    float stationary_heal_pct = 0.0f;
    float stationary_delay = 9999.0f;

    // === Low HP effects ===
    float low_health_regen_pct = 0.0f;
    float low_health_threshold = 0.0f;

    // === Fury / rage ===
    float fury_threshold = 0.0f;
    float fury_damage_mult = 1.0f;
    float fury_attack_speed_mult = 1.0f;

    // === Combo stacks ===
    float combo_damage_bonus = 0.0f;
    int combo_max_stacks = 0;
    float combo_window = 3.0f;

    // === Empowered attacks ===
    int empowered_every = 0;
    float empowered_damage_mult = 1.0f;
    float empowered_stun_dur = 0.0f;

    // === Passive aura ===
    float aura_damage_pct = 0.0f;
    float aura_range = 0.0f;

    // === Nearby enemy debuff ===
    float nearby_debuff_range = 0.0f;
    float nearby_damage_reduction = 0.0f;

    // === Panic freeze ===
    float panic_freeze_radius = 0.0f;
    float panic_freeze_duration = 0.0f;
    float panic_freeze_threshold = 0.0f;
    float panic_freeze_cooldown = 60.0f;

    // === Periodic shield ===
    float shield_regen_pct = 0.0f;
    float shield_regen_cooldown = 15.0f;

    // === Spell echo ===
    float spell_echo_chance = 0.0f;

    // === Frozen target vulnerability ===
    float frozen_vulnerability = 0.0f;

    // === Mana conditionals ===
    float high_mana_damage_mult = 1.0f;
    float high_mana_threshold = 0.0f;
    float low_mana_regen_mult = 1.0f;
    float low_mana_threshold = 0.0f;

    // === Conditional damage ===
    float high_hp_bonus_damage = 0.0f;
    float high_hp_threshold = 0.0f;
    float max_range_damage_bonus = 0.0f;

    // === Damage sharing (Martyr) ===
    float damage_share_percent = 0.0f;
    float share_radius = 0.0f;

    // === Avenge ===
    float avenge_damage_mult = 1.0f;
    float avenge_attack_speed_mult = 1.0f;
    float avenge_duration = 0.0f;

    // === Dodge ===
    float moving_dodge_chance = 0.0f;

    // === Trap modifications ===
    int max_traps = 1;
    float trap_lifetime_mult = 1.0f;
    float trap_radius_mult = 1.0f;
    float trap_vulnerability = 0.0f;
    float trap_vulnerability_dur = 0.0f;
    float trap_cdr = 0.0f;
    float trap_cloud_damage = 0.0f;
    float trap_cloud_duration = 0.0f;
    float trap_cloud_radius = 0.0f;
    float poison_death_explosion_pct = 0.0f;
    float poison_explosion_radius = 0.0f;
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

struct TalentGlobalConfig {
    int first_talent_point_level = 3;
    int max_talent_points = 28;
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
// Vendors (NPC merchants) - per-NPC-type stock lists
// ============================================================================

struct VendorStockConfig {
    std::string item_id;
    int price = 0;     // Override price in gold. 0 = use item's sell_value * markup
    int stock = -1;    // -1 = infinite
};

struct VendorConfig {
    std::string npc_type;          // Matches NPCType::{Merchant,Blacksmith,...} lowercase
    std::string display_name;      // "General Goods", "Blacksmith", etc.
    float buy_price_multiplier = 4.0f;    // What the vendor charges to sell to you
    float sell_price_multiplier = 0.25f;  // What vendor pays for your items
    std::vector<VendorStockConfig> stock;
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
    const TalentGlobalConfig& talent_config() const { return talent_config_; }
    const TalentConfig* find_talent(const std::string& id) const;

    // Quests
    const std::vector<QuestConfig>& quests() const { return quests_; }
    const QuestConfig* find_quest(const std::string& id) const;
    std::vector<const QuestConfig*> quests_for_npc(const std::string& npc_type) const;

    // Vendors
    const std::vector<VendorConfig>& vendors() const { return vendors_; }
    const VendorConfig* find_vendor(const std::string& npc_type) const;

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
    bool load_vendors(const std::string& path);

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

    void apply_mana_system();  // Post-load: apply mana_system data to class configs

    // Mana system per-class data (from skills.json)
    struct ManaSystemEntry {
        float base_mana = 100.0f;
        float mana_per_level = 5.0f;
        float mana_regen = 5.0f;
    };
    std::unordered_map<std::string, ManaSystemEntry> mana_system_;

    // New gameplay configs
    std::vector<MonsterTypeConfig> monster_types_;
    std::vector<ZoneConfig> zones_;
    std::vector<ItemConfig> items_;
    std::vector<LootTableConfig> loot_tables_;
    LevelingConfig leveling_;
    std::vector<SkillConfig> skills_;
    std::vector<TalentTreeConfig> talent_trees_;
    TalentGlobalConfig talent_config_;
    std::vector<QuestConfig> quests_;
    std::vector<VendorConfig> vendors_;

    // O(1) lookup indexes (built after loading)
    std::unordered_map<std::string, const MonsterTypeConfig*> monster_type_index_;
    std::unordered_map<std::string, const ItemConfig*> item_index_;
    std::unordered_map<std::string, const LootTableConfig*> loot_table_index_;
    std::unordered_map<std::string, const SkillConfig*> skill_index_;
    std::unordered_map<std::string, const QuestConfig*> quest_index_;
    std::unordered_map<std::string, const VendorConfig*> vendor_index_;

    void build_indexes();
};

} // namespace mmo::server

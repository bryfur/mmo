#pragma once

#include "game_types.hpp"
#include "protocol/protocol.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
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

struct EnvironmentTypeConfig {
    std::string model;
    float target_scale = 25.0f;
    bool is_tree = false;
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

struct ServerConfig {
    float tick_rate = 60.0f;
    uint16_t default_port = 7777;
};

struct WorldConfig {
    float width = 8000.0f;
    float height = 8000.0f;
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

    // Environment
    const EnvironmentTypeConfig& get_env_type(const std::string& name) const;
    const std::vector<std::string>& rock_types() const { return rock_types_; }
    const std::vector<std::string>& tree_types() const { return tree_types_; }

    // Town
    const WallConfig& wall() const { return wall_; }
    const TowerConfig& corner_towers() const { return corner_towers_; }
    float safe_zone_radius() const { return safe_zone_radius_; }

    // Build a ClassInfo for sending to clients
    mmo::protocol::ClassInfo build_class_info(int index) const;

private:
    bool load_server(const std::string& path);
    bool load_world(const std::string& path);
    bool load_network(const std::string& path);
    bool load_classes(const std::string& path);
    bool load_monsters(const std::string& path);
    bool load_environment(const std::string& path);
    bool load_town(const std::string& path);

    static uint32_t parse_color(const std::string& s);

    ServerConfig server_;
    WorldConfig world_;
    NetworkConfig network_;
    std::vector<ClassConfig> classes_;
    MonsterConfig monster_;
    std::vector<TownNPCConfig> town_npcs_;
    std::vector<BuildingConfig> buildings_;
    std::unordered_map<std::string, EnvironmentTypeConfig> env_types_;
    std::vector<std::string> rock_types_;
    std::vector<std::string> tree_types_;
    WallConfig wall_;
    TowerConfig corner_towers_;
    float safe_zone_radius_ = 400.0f;
};

} // namespace mmo::server

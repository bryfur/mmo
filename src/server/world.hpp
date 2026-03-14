#pragma once

#include "protocol/protocol.hpp"
#include "server/ecs/game_components.hpp"
#include "game_types.hpp"
#include "game_config.hpp"
#include "heightmap_generator.hpp"
#include "systems/physics_system.hpp"
#include "systems/zone_system.hpp"
#include "spatial_grid.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <random>

namespace mmo::server {

class World {
public:
    explicit World(const GameConfig& config);
    ~World();

    uint32_t add_player(const std::string& name, PlayerClass player_class);
    void remove_player(uint32_t player_id);
    void update_player_input(uint32_t player_id, const mmo::protocol::PlayerInput& input);

    void update(float dt);

    std::vector<mmo::protocol::NetEntityState> get_all_entities() const;
    mmo::protocol::NetEntityState get_entity_state(uint32_t network_id) const;
    entt::entity find_entity_by_network_id(uint32_t id) const;

    // Spatial queries
    std::vector<uint32_t> query_entities_near(float x, float y, float radius) const;

    size_t player_count() const;
    size_t npc_count() const;

    // Gameplay events (consumed by Server each tick to send to clients)
    struct GameplayEvent {
        enum class Type {
            XPGain, LevelUp, LootDrop, ZoneChange,
            QuestProgress, QuestComplete, InventoryUpdate,
            CombatEvent, EntityDeath
        };
        Type type;
        uint32_t player_id = 0;

        // XP/Level data
        int xp_gained = 0;
        int total_xp = 0;
        int xp_to_next = 0;
        int new_level = 0;
        float new_max_health = 0;
        float new_damage = 0;

        // Loot
        struct LootItem { std::string name; std::string rarity; int count; };
        std::vector<LootItem> loot_items;
        int loot_gold = 0;
        int total_gold = 0;

        // Zone
        std::string zone_name;

        // Quest progress
        std::string quest_id;
        std::string quest_name;
        uint8_t objective_index = 0;
        int obj_current = 0;
        int obj_required = 0;
        bool obj_complete = false;

        // Combat event (damage dealt)
        uint32_t attacker_id = 0;
        uint32_t target_id = 0;
        float damage_amount = 0;

        // Entity death
        uint32_t dead_entity_id = 0;
        uint32_t killer_entity_id = 0;
    };

    std::vector<GameplayEvent> take_events();
    
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    
    // Physics system access
    systems::PhysicsSystem& physics() { return physics_; }
    const systems::PhysicsSystem& physics() const { return physics_; }
    
    // Heightmap access
    const mmo::protocol::HeightmapChunk& heightmap() const { return heightmap_; }
    float get_terrain_height(float x, float z) const { return heightmap_get_world(heightmap_, x, z); }
    
private:
    void load_heightmap();
    bool spawn_from_world_data();
    void setup_collision_callbacks();
    void populate_render_data(mmo::protocol::NetEntityState& state, const ecs::EntityInfo& info, const ecs::Combat& combat) const;
    uint32_t generate_color(PlayerClass player_class);
    uint32_t next_network_id();
    
    const GameConfig* config_ = nullptr;
    mutable std::mutex mutex_;
    entt::registry registry_;
    systems::PhysicsSystem physics_;
    systems::ZoneSystem zone_system_;
    SpatialGrid spatial_grid_;
    mmo::protocol::HeightmapChunk heightmap_;
    std::unordered_map<uint32_t, entt::entity> network_id_to_entity_;
    uint32_t next_id_ = 1;
    std::mt19937 rng_;
    std::vector<GameplayEvent> pending_events_;
    std::unordered_map<uint32_t, std::string> player_zones_;  // track last zone per player
};

} // namespace mmo::server

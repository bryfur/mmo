#pragma once

#include "protocol/protocol.hpp"
#include "server/ecs/game_components.hpp"
#include "game_types.hpp"
#include "game_config.hpp"
#include "heightmap_generator.hpp"
#include "systems/physics_system.hpp"
#include "spatial_grid.hpp"
#include <entt/entt.hpp>
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
    SpatialGrid spatial_grid_;
    mmo::protocol::HeightmapChunk heightmap_;
    uint32_t next_id_ = 1;
    std::mt19937 rng_;
};

} // namespace mmo::server

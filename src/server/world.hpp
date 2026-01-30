#pragma once

#include "common/protocol.hpp"
#include "common/ecs/components.hpp"
#include "common/heightmap.hpp"
#include "game_types.hpp"
#include "game_config.hpp"
#include "systems/physics_system.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <mutex>
#include <random>

namespace mmo {

class World {
public:
    explicit World(const GameConfig& config);
    ~World();
    
    uint32_t add_player(const std::string& name, PlayerClass player_class);
    void remove_player(uint32_t player_id);
    void update_player_input(uint32_t player_id, const PlayerInput& input);
    
    void update(float dt);
    
    std::vector<NetEntityState> get_all_entities() const;
    entt::entity find_entity_by_network_id(uint32_t id) const;
    
    size_t player_count() const;
    size_t npc_count() const;
    
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    
    // Physics system access
    systems::PhysicsSystem& physics() { return physics_; }
    const systems::PhysicsSystem& physics() const { return physics_; }
    
    // Heightmap access
    const HeightmapChunk& heightmap() const { return heightmap_; }
    float get_terrain_height(float x, float z) const { return heightmap_.get_height_world(x, z); }
    
private:
    void generate_heightmap();
    void spawn_town();
    void spawn_npcs();
    void spawn_environment();
    void setup_collision_callbacks();
    void populate_render_data(NetEntityState& state, const ecs::EntityInfo& info, const ecs::Combat& combat) const;
    uint32_t generate_color(PlayerClass player_class);
    uint32_t next_network_id();
    
    const GameConfig* config_ = nullptr;
    mutable std::mutex mutex_;
    entt::registry registry_;
    systems::PhysicsSystem physics_;
    HeightmapChunk heightmap_;
    uint32_t next_id_ = 1;
    std::mt19937 rng_;
};

} // namespace mmo

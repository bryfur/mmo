#pragma once

#include "game_config.hpp"
#include "game_types.hpp"
#include "gameplay_events.hpp"
#include "heightmap_generator.hpp"
#include "protocol/protocol.hpp"
#include "server/ecs/game_components.hpp"
#include "spatial_grid.hpp"
#include "systems/combat_system.hpp"
#include "systems/physics_system.hpp"
#include "systems/zone_system.hpp"
#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <random>
#include <unordered_map>
#include <vector>

namespace mmo::server {

// Threading contract: all World operations (and all Server/registry access)
// run on the asio::io_context thread. The server is single-threaded by
// design — handlers, the tick loop, and physics all execute serially via
// the io_context. No locks are required for registry access; do NOT call
// these methods from another thread without first introducing a posting
// mechanism (asio::post) onto the io_context.
class World {
public:
    explicit World(const GameConfig& config);
    ~World();

    uint32_t add_player(const std::string& name, PlayerClass player_class);
    void remove_player(uint32_t player_id);
    void update_player_input(uint32_t player_id, const mmo::protocol::PlayerInput& input);

    /// Run non-physics gameplay systems with wall-clock dt.
    void update(float dt);

    /// Advance the physics simulation by a fixed timestep. Call one or more
    /// times per frame from the server tick loop's accumulator. Thread-safe
    /// with respect to other World operations.
    void update_physics_step(float fixed_dt);

    std::vector<mmo::protocol::NetEntityState> get_all_entities() const;
    mmo::protocol::NetEntityState get_entity_state(uint32_t network_id) const;
    /// Like get_entity_state, but also fills static render fields (model_name,
    /// target_size, effect_type, animation, cone_angle, shows_reticle).
    /// Use only for the initial EntityEnter; per-tick deltas don't need them.
    mmo::protocol::NetEntityState get_entity_state_full(uint32_t network_id) const;
    entt::entity find_entity_by_network_id(uint32_t id) const;

    // Spatial queries
    std::vector<uint32_t> query_entities_near(float x, float y, float radius) const;

    size_t player_count() const;
    size_t npc_count() const;

    // Gameplay events flow Server-side: World produces, Server consumes per
    // tick. See gameplay_events.hpp for the variant alternatives.
    using GameplayEvent = events::GameplayEvent;
    std::vector<GameplayEvent> take_events();

    /// Inject combat hits (e.g. from skills) to generate CombatEvent/EntityDeath events
    void add_combat_hits(const std::vector<systems::CombatHit>& hits);

    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

    // Physics system access
    systems::PhysicsSystem& physics() { return physics_; }
    const systems::PhysicsSystem& physics() const { return physics_; }

    // Heightmap access
    const mmo::protocol::HeightmapChunk& heightmap() const { return heightmap_; }
    float get_terrain_height(float x, float z) const { return physics_.terrain_height(x, z); }

    // Cached town center position (from "town_safe_zone" config)
    const glm::vec2& town_center() const { return town_center_; }

private:
    void load_heightmap();
    bool spawn_from_world_data();
    void setup_collision_callbacks();
    void populate_render_data(mmo::protocol::NetEntityState& state, const ecs::EntityInfo& info,
                              const ecs::Combat& combat) const;
    mmo::protocol::NetEntityState build_entity_state(entt::entity entity, bool include_render_static) const;
    uint32_t generate_color(PlayerClass player_class);
    uint32_t next_network_id();

    const GameConfig* config_ = nullptr;
    entt::registry registry_;
    systems::PhysicsSystem physics_;
    systems::ZoneSystem zone_system_;
    SpatialGrid spatial_grid_;
    mmo::protocol::HeightmapChunk heightmap_;
    std::unordered_map<uint32_t, entt::entity> network_id_to_entity_;
    uint32_t next_id_ = 1;
    std::mt19937 rng_;
    std::vector<GameplayEvent> pending_events_;
    std::unordered_map<uint32_t, std::string> player_zones_; // track last zone per player
    glm::vec2 town_center_{4000.0f, 4000.0f};                // cached from "town_safe_zone" config
};

} // namespace mmo::server

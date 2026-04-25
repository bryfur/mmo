#pragma once

#include "server/ecs/game_components.hpp"
#include "protocol/heightmap.hpp"
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <vector>
#include <functional>
#include <cstdint>

namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BodyInterface;
    class Body;
    class BodyID;
}

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

// Collision layers for broadphase filtering
namespace CollisionLayers {
    constexpr uint8_t NON_MOVING = 0;
    constexpr uint8_t MOVING = 1;
    constexpr uint8_t TRIGGER = 2;
    constexpr uint8_t NUM_LAYERS = 3;
}

using CollisionCallback = std::function<void(entt::entity, entt::entity, const ecs::CollisionEvent&)>;

struct RaycastHit {
    bool hit = false;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
    entt::entity entity = entt::null;
};

/**
 * @brief Physics system wrapping JoltPhysics for 3D collision detection
 *
 * Owns:
 * - A single static HeightFieldShape body representing the terrain.
 * - Dynamic rigid bodies for world props (buildings, trees, etc.).
 * - Per-character JPH::CharacterVirtual controllers for players & NPCs.
 */
class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    void initialize(uint32_t max_bodies = 10240,
                   uint32_t max_body_pairs = 65536,
                   uint32_t max_contact_constraints = 10240);

    void shutdown();

    /// Build terrain collision from a heightmap chunk. Call once after
    /// the heightmap is loaded, before spawning entities.
    void build_terrain(const mmo::protocol::HeightmapChunk& chunk);

    /// Returns terrain height at (x, z) using the cooked HeightFieldShape if
    /// available, falling back to the heightmap CPU sampler. Any consumer
    /// that needs ground-level Y should go through this.
    float terrain_height(float x, float z) const;

    /// Creates Jolt bodies / CharacterVirtuals for all entities that have
    /// physics components but no PhysicsBody yet. Called internally each
    /// tick; scan is cheap because entries are skipped once tracked.
    void create_bodies(entt::registry& registry);

    void destroy_body(entt::registry& registry, entt::entity entity);

    /// Step physics with a FIXED dt (seconds). Callers are expected to
    /// accumulate wall-clock time and call this a whole number of times per
    /// frame. Do not pass variable dt.
    void update(entt::registry& registry, float fixed_dt);

    void sync_transforms(entt::registry& registry);

    void apply_impulse(entt::registry& registry, entt::entity entity,
                      float impulse_x, float impulse_y, float impulse_z);

    void set_velocity(entt::registry& registry, entt::entity entity,
                     float vel_x, float vel_y, float vel_z);

    void move_kinematic(entt::registry& registry, entt::entity entity,
                       float x, float y, float z, float dt);

    bool are_colliding(entt::registry& registry, entt::entity a, entt::entity b);

    void set_collision_callback(CollisionCallback callback);

    std::vector<ecs::CollisionEvent> get_collision_events() const;

    /// Ray cast against terrain + rigid bodies. Characters are included via
    /// their CharacterVirtual inner bodies.
    bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                 float max_distance, RaycastHit& out) const;

    /// Collect entities whose physics body overlaps a sphere.
    bool overlap_sphere(const glm::vec3& center, float radius,
                        std::vector<entt::entity>& out) const;

    void get_gravity(float& x, float& y, float& z) const;
    void set_gravity(float x, float y, float z);

    void update_body_shape(entt::registry& registry, entt::entity entity,
                          float radius, float half_height);

    void optimize_broadphase();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

void update_physics(PhysicsSystem& physics, entt::registry& registry, float dt);

} // namespace mmo::server::systems

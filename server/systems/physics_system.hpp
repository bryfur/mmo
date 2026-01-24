#pragma once

#include "common/ecs/components.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <vector>
#include <functional>
#include <cstdint>

// Forward declarations for Jolt types
namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BodyInterface;
    class Body;
    class BodyID;
}

namespace mmo::systems {

// Collision layers for broadphase filtering
namespace CollisionLayers {
    constexpr uint8_t NON_MOVING = 0;    // Static objects
    constexpr uint8_t MOVING = 1;         // Dynamic/kinematic objects
    constexpr uint8_t TRIGGER = 2;        // Trigger volumes
    constexpr uint8_t NUM_LAYERS = 3;
}

// Callback for collision events
using CollisionCallback = std::function<void(entt::entity, entt::entity, const ecs::CollisionEvent&)>;

/**
 * @brief Physics system wrapping JoltPhysics for 3D collision detection
 * 
 * This system manages physics simulation including:
 * - Rigid body dynamics
 * - Collision detection and response
 * - Trigger volume detection
 * - Character movement with collision
 */
class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    // Non-copyable
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    /**
     * @brief Initialize the physics system
     * @param max_bodies Maximum number of physics bodies
     * @param max_body_pairs Maximum body pairs for collision
     * @param max_contact_constraints Maximum contact constraints
     */
    void initialize(uint32_t max_bodies = 10240,
                   uint32_t max_body_pairs = 65536,
                   uint32_t max_contact_constraints = 10240);

    /**
     * @brief Shutdown and cleanup physics system
     */
    void shutdown();

    /**
     * @brief Create physics bodies for entities with Collider + RigidBody components
     * @param registry ECS registry
     */
    void create_bodies(entt::registry& registry);

    /**
     * @brief Destroy physics body for an entity
     * @param registry ECS registry
     * @param entity Entity to remove physics from
     */
    void destroy_body(entt::registry& registry, entt::entity entity);

    /**
     * @brief Step the physics simulation
     * @param registry ECS registry
     * @param dt Delta time in seconds
     */
    void update(entt::registry& registry, float dt);

    /**
     * @brief Sync transforms from physics to ECS
     * @param registry ECS registry
     */
    void sync_transforms(entt::registry& registry);

    /**
     * @brief Apply impulse to a physics body
     * @param registry ECS registry
     * @param entity Target entity
     * @param impulse_x X component of impulse
     * @param impulse_y Y component (up)
     * @param impulse_z Z component  
     */
    void apply_impulse(entt::registry& registry, entt::entity entity,
                      float impulse_x, float impulse_y, float impulse_z);

    /**
     * @brief Set linear velocity directly
     * @param registry ECS registry
     * @param entity Target entity
     * @param vel_x X velocity
     * @param vel_y Y velocity (up)
     * @param vel_z Z velocity
     */
    void set_velocity(entt::registry& registry, entt::entity entity,
                     float vel_x, float vel_y, float vel_z);

    /**
     * @brief Move a kinematic body to a position
     * @param registry ECS registry
     * @param entity Target entity
     * @param x Target X position
     * @param y Target Y position (up)
     * @param z Target Z position
     * @param dt Delta time for interpolation
     */
    void move_kinematic(entt::registry& registry, entt::entity entity,
                       float x, float y, float z, float dt);

    /**
     * @brief Check if two bodies are currently colliding
     */
    bool are_colliding(entt::registry& registry, entt::entity a, entt::entity b);

    /**
     * @brief Set collision callback
     * @param callback Function called on collision events
     */
    void set_collision_callback(CollisionCallback callback);

    /**
     * @brief Get current collision events from last update
     */
    const std::vector<ecs::CollisionEvent>& get_collision_events() const;

    /**
     * @brief Ray cast into the physics world
     * @param origin_x Ray origin X
     * @param origin_y Ray origin Y
     * @param origin_z Ray origin Z
     * @param dir_x Ray direction X
     * @param dir_y Ray direction Y
     * @param dir_z Ray direction Z
     * @param max_distance Maximum ray distance
     * @param out_hit_entity Output: entity hit (or entt::null)
     * @param out_hit_x Output: hit point X
     * @param out_hit_y Output: hit point Y
     * @param out_hit_z Output: hit point Z
     * @return true if hit something
     */
    bool ray_cast(float origin_x, float origin_y, float origin_z,
                 float dir_x, float dir_y, float dir_z,
                 float max_distance,
                 entt::entity& out_hit_entity,
                 float& out_hit_x, float& out_hit_y, float& out_hit_z);

    /**
     * @brief Get gravity
     */
    void get_gravity(float& x, float& y, float& z) const;

    /**
     * @brief Set gravity
     */
    void set_gravity(float x, float y, float z);

private:
    // Internal implementation
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Update physics for the world (convenience function)
 * @param physics Physics system instance
 * @param registry ECS registry
 * @param dt Delta time
 */
void update_physics(PhysicsSystem& physics, entt::registry& registry, float dt);

} // namespace mmo::systems

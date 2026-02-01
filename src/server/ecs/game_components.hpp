#pragma once

#include "protocol/protocol.hpp"
#include <string>
#include <cstdint>

namespace mmo::server::ecs {

// ============================================================================
// Core Components
// ============================================================================

// Coordinate system: Y-up. x,z form the horizontal ground plane; y is vertical (height/elevation)
struct Transform {
    float x = 0.0f;
    float y = 0.0f;      // Height/elevation
    float z = 0.0f;
    float rotation = 0.0f;  // Rotation in radians (around vertical axis)
};

struct Velocity {
    float x = 0.0f;
    float y = 0.0f;      // Vertical velocity
    float z = 0.0f;
};

struct Health {
    float current = 100.0f;
    float max = 100.0f;

    bool is_alive() const { return current > 0.0f; }
    float ratio() const { return max > 0 ? current / max : 0.0f; }
};

struct Combat {
    float damage = 0.0f;
    float attack_range = 0.0f;
    float attack_cooldown = 0.0f;
    float current_cooldown = 0.0f;
    bool is_attacking = false;

    bool can_attack() const { return current_cooldown <= 0.0f; }
};

struct NetworkId {
    uint32_t id = 0;
};

struct EntityInfo {
    mmo::protocol::EntityType type = mmo::protocol::EntityType::Player;
    uint8_t player_class = 0;
    uint8_t npc_type = 0;
    uint8_t building_type = 0;
    uint8_t environment_type = 0;
    uint32_t color = 0xFFFFFFFF;

    // Render data (sent to client via protocol)
    std::string model_name;
    float target_size = 0.0f;
    std::string effect_type;
    float cone_angle = 0.0f;
    bool shows_reticle = false;
};

struct Name {
    std::string value;
};

// Static entities don't move
struct StaticTag {};

// Attack direction for effects
struct AttackDirection {
    float x = 0.0f;
    float y = 1.0f;
};

// Per-instance scale multiplier
struct Scale {
    float value = 1.0f;
};

// ============================================================================
// Physics Components (JoltPhysics integration)
// ============================================================================

enum class ColliderType : uint8_t {
    Sphere = 0,
    Box = 1,
    Capsule = 2,
    Cylinder = 3,
};

enum class PhysicsMotionType : uint8_t {
    Static = 0,      // Never moves (buildings, terrain)
    Kinematic = 1,   // Moved by code, affects dynamic bodies
    Dynamic = 2,     // Fully simulated
};

struct Collider {
    ColliderType type = ColliderType::Sphere;
    float radius = 16.0f;
    float half_height = 16.0f;
    float half_extents_x = 16.0f;
    float half_extents_y = 16.0f;
    float half_extents_z = 16.0f;
    float offset_y = 0.0f;
    bool is_trigger = false;
};

struct RigidBody {
    PhysicsMotionType motion_type = PhysicsMotionType::Dynamic;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    float linear_damping = 0.1f;
    float angular_damping = 0.1f;
    bool lock_rotation = true;
};

struct PhysicsBody {
    uint32_t body_id = 0xFFFFFFFF;
    bool needs_sync = true;
    bool needs_teleport = false;
};

struct CollisionEvent {
    uint32_t entity_a_network_id = 0;
    uint32_t entity_b_network_id = 0;
    float contact_point_x = 0.0f;
    float contact_point_y = 0.0f;
    float contact_point_z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float normal_z = 0.0f;
    float penetration_depth = 0.0f;
};

// ============================================================================
// Game Logic Components
// ============================================================================

struct PlayerTag {};

struct NPCTag {};

struct InputState {
    mmo::protocol::PlayerInput input;
};

struct AIState {
    uint32_t target_id = 0;
    float aggro_range = 300.0f;
};

// Town NPC AI - wanders around home position
struct TownNPCAI {
    float home_x = 0.0f;
    float home_z = 0.0f;
    float wander_radius = 50.0f;
    float idle_timer = 0.0f;
    float move_timer = 0.0f;
    float target_x = 0.0f;
    float target_z = 0.0f;
    bool is_moving = false;
};

// Safe zone marker
struct SafeZone {
    float center_x = 0.0f;
    float center_z = 0.0f;
    float radius = 0.0f;
};

} // namespace mmo::server::ecs

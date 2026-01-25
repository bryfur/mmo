#pragma once

#include "common/protocol.hpp"
#include <string>
#include <cstdint>

namespace mmo::ecs {

struct Transform {
    float x = 0.0f;      // World X position
    float y = 0.0f;      // World Z position (horizontal plane)
    float z = 0.0f;      // World Y position (height/elevation)
    float rotation = 0.0f;  // Rotation in radians (around vertical axis)
};

struct Velocity {
    float x = 0.0f;      // X velocity
    float y = 0.0f;      // Z velocity (horizontal)
    float z = 0.0f;      // Y velocity (vertical)
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
    EntityType type = EntityType::Player;
    PlayerClass player_class = PlayerClass::Warrior;
    NPCType npc_type = NPCType::Monster;
    BuildingType building_type = BuildingType::Tavern;
    EnvironmentType environment_type = EnvironmentType::RockBoulder;
    uint32_t color = 0xFFFFFFFF;
};

struct PlayerTag {};

struct NPCTag {};

struct Name {
    std::string value;
};

struct InputState {
    PlayerInput input;
};

struct AIState {
    uint32_t target_id = 0;
    float aggro_range = config::NPC_AGGRO_RANGE;
};

// Town NPC AI - wanders around home position
struct TownNPCAI {
    float home_x = 0.0f;
    float home_y = 0.0f;
    float wander_radius = 50.0f;
    float idle_timer = 0.0f;
    float move_timer = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    bool is_moving = false;
};

// Static buildings don't move
struct StaticTag {};

// Safe zone marker
struct SafeZone {
    float center_x = 0.0f;
    float center_y = 0.0f;
    float radius = 0.0f;
};

// Attack direction for rendering effects (sent from server)
struct AttackDirection {
    float x = 0.0f;
    float y = 1.0f;
};

struct LocalPlayer {};

struct Interpolation {
    float prev_x = 0.0f;
    float prev_y = 0.0f;
    float prev_z = 0.0f;  // Height interpolation
    float target_x = 0.0f;
    float target_y = 0.0f;
    float target_z = 0.0f;  // Target height from server
    float alpha = 1.0f;
};

// Attack visual effects - client-side only
struct AttackEffect {
    PlayerClass attacker_class = PlayerClass::Warrior;
    float x = 0.0f;  // Origin position
    float y = 0.0f;
    float direction_x = 0.0f;  // Facing direction (normalized)
    float direction_y = 1.0f;
    float timer = 0.0f;  // Time remaining for effect
    float duration = 0.3f;  // Total duration
    
    // For paladin AOE
    float target_x = 0.0f;
    float target_y = 0.0f;
};

// Facing direction for entities (used for attack direction)
struct Facing {
    float x = 0.0f;
    float y = 1.0f;  // Default facing down
};

// Per-instance scale multiplier
// 1.0 = normal size, 2.0 = double size, 0.5 = half size
struct Scale {
    float value = 1.0f;
};

// ============================================================================
// Physics Components (JoltPhysics integration)
// ============================================================================

// Collider shape types
enum class ColliderType : uint8_t {
    Sphere = 0,
    Box = 1,
    Capsule = 2,
    Cylinder = 3,
};

// Physics body motion type
enum class PhysicsMotionType : uint8_t {
    Static = 0,      // Never moves (buildings, terrain)
    Kinematic = 1,   // Moved by code, affects dynamic bodies
    Dynamic = 2,     // Fully simulated
};

// Collider component - defines collision shape
struct Collider {
    ColliderType type = ColliderType::Sphere;
    float radius = 16.0f;           // For sphere/capsule
    float half_height = 16.0f;      // For capsule/cylinder
    float half_extents_x = 16.0f;   // For box
    float half_extents_y = 16.0f;   // For box  
    float half_extents_z = 16.0f;   // For box
    float offset_y = 0.0f;          // Vertical offset from transform
    bool is_trigger = false;        // Trigger colliders don't block movement
};

// RigidBody component - physics simulation properties
struct RigidBody {
    PhysicsMotionType motion_type = PhysicsMotionType::Dynamic;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;       // Bounciness
    float linear_damping = 0.1f;
    float angular_damping = 0.1f;
    bool lock_rotation = true;      // Lock rotation for characters
};

// PhysicsBody component - stores Jolt body ID (set by physics system)
struct PhysicsBody {
    uint32_t body_id = 0xFFFFFFFF;  // Invalid ID by default
    bool needs_sync = true;          // Whether to sync transform from physics
    bool needs_teleport = false;     // Set true to teleport body to current transform (e.g., respawn)
};

// Collision event data
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

} // namespace mmo::ecs

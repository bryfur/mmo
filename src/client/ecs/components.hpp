#pragma once

#include "protocol/protocol.hpp"
#include "engine/animation/animation_player.hpp"
#include "engine/animation/animation_state_machine.hpp"
#include <string>
#include <cstdint>

namespace mmo::client::ecs {

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

    // Server-provided render data
    std::string model_name;
    float target_size = 0.0f;
    std::string effect_type;
    std::string animation;     // Animation config name (e.g. "humanoid")
    float cone_angle = 0.0f;
    bool shows_reticle = false;
};

struct Name {
    std::string value;
};

// Attack direction for rendering effects (sent from server)
struct AttackDirection {
    float x = 0.0f;
    float y = 1.0f;
};

struct LocalPlayer {};

struct Interpolation {
    float prev_x = 0.0f;
    float prev_y = 0.0f;  // Height interpolation
    float prev_z = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;  // Target height from server
    float target_z = 0.0f;
    float alpha = 1.0f;
};

// Attack visual effects
struct AttackEffect {
    std::string effect_type;   // "melee_swing", "projectile", "orbit", "arrow"
    std::string effect_model;  // Model name for the effect (e.g. "weapon_sword")
    float x = 0.0f;  // Origin position
    float y = 0.0f;
    float direction_x = 0.0f;  // Facing direction (normalized)
    float direction_y = 1.0f;
    float timer = 0.0f;  // Time remaining for effect
    float duration = 0.3f;  // Total duration
    float range = 1.0f;  // Attack range for scaling effects
    float cone_angle = 0.0f;  // Attack cone angle

    // For orbit/AOE effects
    float target_x = 0.0f;
    float target_y = 0.0f;
};

// Facing direction for entities (used for attack direction)
struct Facing {
    float x = 0.0f;
    float y = 1.0f;  // Default facing down
};

// Smooth visual rotation — thin alias for engine type so it works as an ECS component
using SmoothRotation = mmo::engine::animation::RotationSmoother;

// Per-instance scale multiplier
struct Scale {
    float value = 1.0f;
};

// Marks an entity as renderable with a 3D model
struct ModelRenderable {
    std::string model_name;  // Key in ModelManager
    float tint_r = 1.0f;
    float tint_g = 1.0f;
    float tint_b = 1.0f;
    float tint_a = 1.0f;
    float scale = 1.0f;
};

// For 2D sprites/billboards
struct SpriteRenderable {
    std::string texture_name;
    float width = 1.0f;
    float height = 1.0f;
};

// Health bar display component
struct HealthBarRenderable {
    float width = 1.0f;
    float y_offset = 2.0f;  // Height above entity
    bool show_always = false;
};

// Per-entity animation state (each entity gets independent animation)
struct AnimationInstance {
    mmo::engine::animation::AnimationPlayer player;
    mmo::engine::animation::AnimationStateMachine state_machine;
    mmo::engine::animation::ProceduralConfig procedural;
    bool bound = false;
    float attack_tilt = 0.0f;
};

} // namespace mmo::client::ecs

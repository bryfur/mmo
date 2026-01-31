#include "world.hpp"
#include "ecs/game_components.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "protocol/heightmap.hpp"
#include "protocol/protocol.hpp"
#include "server/game_config.hpp"
#include "server/game_types.hpp"
#include "server/heightmap_generator.hpp"
#include "systems/movement_system.hpp"
#include "systems/combat_system.hpp"
#include "systems/ai_system.hpp"
#include "systems/physics_system.hpp"
#include "entity_config.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mmo::server {

using namespace mmo::protocol;

World::World(const GameConfig& config) : config_(&config), rng_(std::random_device{}()) {
    // Generate heightmap first (needed for terrain-aware spawning)
    generate_heightmap();
    
    // Initialize physics with gravity for 3D game
    physics_.initialize();
    physics_.set_gravity(0.0f, -9.81f, 0.0f);
    
    // Set terrain height callback for ground snapping
    physics_.set_terrain_height_callback([this](float x, float z) {
        return get_terrain_height(x, z);
    });
    
    // Setup collision callbacks
    setup_collision_callbacks();
    
    spawn_town();
    spawn_npcs();
    spawn_environment();
    
    // Create physics bodies for all spawned entities  
    physics_.create_bodies(registry_);
    
    // Count physics bodies by type
    int static_boxes = 0, static_capsules = 0, dynamic_capsules = 0;
    auto view = registry_.view<ecs::PhysicsBody, ecs::Collider, ecs::RigidBody>();
    for (auto entity : view) {
        const auto& collider = view.get<ecs::Collider>(entity);
        const auto& rb = view.get<ecs::RigidBody>(entity);
        if (rb.motion_type == ecs::PhysicsMotionType::Static) {
            if (collider.type == ecs::ColliderType::Box) static_boxes++;
            else if (collider.type == ecs::ColliderType::Capsule) static_capsules++;
        } else {
            if (collider.type == ecs::ColliderType::Capsule) dynamic_capsules++;
        }
    }
    std::cout << "[Physics] Bodies created - static boxes: " << static_boxes 
              << ", static capsules: " << static_capsules 
              << ", dynamic capsules: " << dynamic_capsules << std::endl;
    
    // Optimize broadphase now that all static bodies are added
    // This is critical for efficient collision detection with static objects
    physics_.optimize_broadphase();
}

World::~World() {
    physics_.shutdown();
}

void World::setup_collision_callbacks() {
    physics_.set_collision_callback([this](entt::entity a, entt::entity b, 
                                           const ecs::CollisionEvent& event) {
        // Handle collision events here
        // For example: damage on collision, trigger effects, etc.
        
        // Check collision types for gameplay logic
        bool a_is_player = registry_.all_of<ecs::PlayerTag>(a);
        bool b_is_player = registry_.all_of<ecs::PlayerTag>(b);
        bool a_is_npc = registry_.all_of<ecs::NPCTag>(a);
        bool b_is_npc = registry_.all_of<ecs::NPCTag>(b);
        
        if ((a_is_player && b_is_npc) || (b_is_player && a_is_npc)) {
            // Player touched an NPC - could trigger aggro or damage
            // This is handled by combat system, but physics gives us precise collision
        }
    });
}

void World::spawn_town() {
    const float TOWN_CENTER_X = config_->world().width / 2.0f;
    const float TOWN_CENTER_Z = config_->world().height / 2.0f;

    // Create buildings around town center
    struct BuildingPlacement {
        BuildingType type;
        float offset_x;
        float offset_z;
        const char* name;
        float rotation = 0.0f;  // Rotation in degrees
    };
    
    std::vector<BuildingPlacement> buildings;
    for (const auto& b : config_->buildings()) {
        BuildingType bt = BuildingType::House;
        if (b.type == "tavern") bt = BuildingType::Tavern;
        else if (b.type == "blacksmith") bt = BuildingType::Blacksmith;
        else if (b.type == "tower") bt = BuildingType::Tower;
        else if (b.type == "shop") bt = BuildingType::Shop;
        else if (b.type == "well") bt = BuildingType::Well;
        else if (b.type == "house") bt = BuildingType::House;
        else if (b.type == "inn") bt = BuildingType::Inn;
        buildings.push_back({bt, b.x, b.y, b.name.c_str(), b.rotation});
    }
    
    // Generate log palisade walls - individual logs spaced closely
    const float WALL_DIST = config_->wall().distance;
    const float LOG_SPACING = config_->wall().spacing;
    const float GATE_WIDTH = config_->wall().gate_width;
    
    // South wall (with gate)
    for (float x = -WALL_DIST + 60.0f; x <= WALL_DIST - 60.0f; x += LOG_SPACING) {
        if (std::abs(x) < GATE_WIDTH / 2.0f) continue;  // Skip gate area
        buildings.push_back({BuildingType::WoodenLog, x, -WALL_DIST, "Log", 0.0f});
    }
    
    // North wall (with gate)
    for (float x = -WALL_DIST + 60.0f; x <= WALL_DIST - 60.0f; x += LOG_SPACING) {
        if (std::abs(x) < GATE_WIDTH / 2.0f) continue;  // Skip gate area
        buildings.push_back({BuildingType::WoodenLog, x, WALL_DIST, "Log", 0.0f});
    }
    
    // West wall (solid)
    for (float z = -WALL_DIST + 60.0f; z <= WALL_DIST - 60.0f; z += LOG_SPACING) {
        buildings.push_back({BuildingType::WoodenLog, -WALL_DIST, z, "Log", 90.0f});
    }

    // East wall (with gate)
    for (float z = -WALL_DIST + 60.0f; z <= WALL_DIST - 60.0f; z += LOG_SPACING) {
        if (std::abs(z) < GATE_WIDTH / 2.0f) continue;  // Skip gate area
        buildings.push_back({BuildingType::WoodenLog, WALL_DIST, z, "Log", 90.0f});
    }
    
    for (const auto& b : buildings) {
        auto entity = registry_.create();
        
        float world_x = TOWN_CENTER_X + b.offset_x;
        float world_z = TOWN_CENTER_Z + b.offset_z;
        float world_y = get_terrain_height(world_x, world_z);  // Get terrain height

        registry_.emplace<ecs::NetworkId>(entity, next_network_id());
        ecs::Transform transform;
        transform.x = world_x;
        transform.y = world_y;
        transform.z = world_z;
        transform.rotation = b.rotation * 3.14159f / 180.0f;  // Convert degrees to radians
        registry_.emplace<ecs::Transform>(entity, transform);
        registry_.emplace<ecs::Velocity>(entity);
        registry_.emplace<ecs::Health>(entity, 9999.0f, 9999.0f);
        registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);
        
        ecs::EntityInfo info;
        info.type = EntityType::Building;
        info.building_type = static_cast<uint8_t>(b.type);
        info.color = 0xFFBB9977;
        registry_.emplace<ecs::EntityInfo>(entity, info);
        
        registry_.emplace<ecs::Name>(entity, b.name);
        registry_.emplace<ecs::StaticTag>(entity);
        registry_.emplace<ecs::Scale>(entity);  // Default scale = 1.0
        
        // Add physics collider for buildings (box shape, static)
        // Size is calculated from actual model dimensions with scaling applied
        ecs::Collider collider;
        collider.type = ecs::ColliderType::Box;
        
        // Calculate collision size based on model bounds and visual scale
        config::get_building_collision_size(
            b.type,
            collider.half_extents_x,
            collider.half_extents_y,
            collider.half_extents_z
        );
        // Offset so box is centered at building's visual center (sitting on terrain)
        collider.offset_y = collider.half_extents_y;
        registry_.emplace<ecs::Collider>(entity, collider);
        
        ecs::RigidBody rb;
        rb.motion_type = ecs::PhysicsMotionType::Static;
        registry_.emplace<ecs::RigidBody>(entity, rb);
    }
    
    // Create town NPCs
    struct TownNPCPlacement {
        NPCType type;
        float offset_x;
        float offset_z;
        const char* name;
        bool wanders;
        uint32_t color = 0xFFAAAAAA;
    };
    
    std::vector<TownNPCPlacement> town_npcs;
    for (const auto& n : config_->town_npcs()) {
        NPCType nt = NPCType::Villager;
        if (n.type == "innkeeper") nt = NPCType::Innkeeper;
        else if (n.type == "blacksmith") nt = NPCType::Blacksmith;
        else if (n.type == "merchant") nt = NPCType::Merchant;
        else if (n.type == "guard") nt = NPCType::Guard;
        else if (n.type == "villager") nt = NPCType::Villager;
        town_npcs.push_back({nt, n.x, n.y, n.name.c_str(), n.wanders, n.color});
    }
    
    for (const auto& npc : town_npcs) {
        auto entity = registry_.create();
        float x = TOWN_CENTER_X + npc.offset_x;
        float z = TOWN_CENTER_Z + npc.offset_z;
        float y = get_terrain_height(x, z);  // Get terrain height at spawn position

        registry_.emplace<ecs::NetworkId>(entity, next_network_id());

        ecs::Transform transform;
        transform.x = x;
        transform.y = y;
        transform.z = z;
        registry_.emplace<ecs::Transform>(entity, transform);
        registry_.emplace<ecs::Velocity>(entity);
        registry_.emplace<ecs::Health>(entity, 1000.0f, 1000.0f);
        registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);
        
        ecs::EntityInfo info;
        info.type = EntityType::TownNPC;
        info.npc_type = static_cast<uint8_t>(npc.type);

        info.color = npc.color;
        registry_.emplace<ecs::EntityInfo>(entity, info);
        
        registry_.emplace<ecs::Name>(entity, npc.name);
        registry_.emplace<ecs::Scale>(entity);  // Default scale = 1.0
        
        // Add physics collider for town NPCs (based on visual size)
        float npc_target_size = config::get_character_target_size(EntityType::TownNPC);
        ecs::Collider collider;
        collider.type = ecs::ColliderType::Capsule;
        collider.radius = config::get_collision_radius(npc_target_size);
        collider.half_height = config::get_collision_half_height(npc_target_size);
        // Offset so capsule is centered at NPC mid-height (feet at transform.y)
        collider.offset_y = collider.half_height + collider.radius;
        registry_.emplace<ecs::Collider>(entity, collider);
        
        ecs::RigidBody rb;
        rb.motion_type = npc.wanders ? ecs::PhysicsMotionType::Dynamic : ecs::PhysicsMotionType::Static;
        rb.lock_rotation = true;
        rb.mass = 70.0f;
        rb.linear_damping = 0.9f;
        registry_.emplace<ecs::RigidBody>(entity, rb);
        
        if (npc.wanders) {
            ecs::TownNPCAI ai;
            ai.home_x = x;
            ai.home_z = z;
            ai.wander_radius = 80.0f;
            registry_.emplace<ecs::TownNPCAI>(entity, ai);
        } else {
            registry_.emplace<ecs::StaticTag>(entity);
        }
    }
}

void World::spawn_npcs() {
    const float TOWN_CENTER_X = config_->world().width / 2.0f;
    const float TOWN_CENTER_Z = config_->world().height / 2.0f;
    const float TOWN_SAFE_RADIUS = config_->safe_zone_radius();

    // Spawn hostile NPCs OUTSIDE the town safe zone
    std::uniform_real_distribution<float> dist_x(100.0f, config_->world().width - 100.0f);
    std::uniform_real_distribution<float> dist_z(100.0f, config_->world().height - 100.0f);

    int spawned = 0;
    while (spawned < config_->monster().count) {
        float x = dist_x(rng_);
        float z = dist_z(rng_);

        // Skip if inside town safe zone
        float dx = x - TOWN_CENTER_X;
        float dz = z - TOWN_CENTER_Z;
        if (dx * dx + dz * dz < (TOWN_SAFE_RADIUS + 100.0f) * (TOWN_SAFE_RADIUS + 100.0f)) {
            continue;
        }

        // Get terrain height at spawn position
        float y = get_terrain_height(x, z);

        auto entity = registry_.create();

        registry_.emplace<ecs::NetworkId>(entity, next_network_id());

        ecs::Transform transform;
        transform.x = x;
        transform.y = y;
        transform.z = z;
        registry_.emplace<ecs::Transform>(entity, transform);
        registry_.emplace<ecs::Velocity>(entity);
        registry_.emplace<ecs::Health>(entity, config_->monster().health, config_->monster().health);
        registry_.emplace<ecs::Combat>(entity, config_->monster().damage, config_->monster().attack_range, 
                                       config_->monster().attack_cooldown, 0.0f, false);
        
        ecs::EntityInfo info;
        info.type = EntityType::NPC;
        info.npc_type = static_cast<uint8_t>(NPCType::Monster);
        info.color = config_->monster().color;
        registry_.emplace<ecs::EntityInfo>(entity, info);
        
        registry_.emplace<ecs::Name>(entity, "Monster_" + std::to_string(spawned + 1));
        registry_.emplace<ecs::NPCTag>(entity);
        registry_.emplace<ecs::AIState>(entity);
        registry_.emplace<ecs::Scale>(entity);  // Default scale = 1.0
        
        // Add physics collider for hostile NPCs (dynamic for collision response)
        // Size matches visual scale from client rendering
        float npc_target_size = config::get_character_target_size(EntityType::NPC);
        ecs::Collider collider;
        collider.type = ecs::ColliderType::Capsule;
        collider.radius = config::get_collision_radius(npc_target_size);
        collider.half_height = config::get_collision_half_height(npc_target_size);
        // Offset so capsule is centered at NPC mid-height (feet at transform.y)
        collider.offset_y = collider.half_height + collider.radius;
        registry_.emplace<ecs::Collider>(entity, collider);
        
        ecs::RigidBody rb;
        rb.motion_type = ecs::PhysicsMotionType::Dynamic;
        rb.lock_rotation = true;
        rb.mass = 80.0f;
        rb.linear_damping = 0.9f;
        registry_.emplace<ecs::RigidBody>(entity, rb);
        
        spawned++;
    }
}

uint32_t World::add_player(const std::string& name, PlayerClass player_class) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto entity = registry_.create();
    uint32_t net_id = next_network_id();
    
    // Spawn players in the town (safe zone)
    const float town_center_x = config_->world().width / 2.0f;
    const float town_center_z = config_->world().height / 2.0f;
    std::uniform_real_distribution<float> dist_offset(-50.0f, 50.0f);
    float spawn_x = town_center_x + dist_offset(rng_);
    float spawn_z = town_center_z + dist_offset(rng_);

    // Get terrain height at spawn position (y = height/elevation)
    float spawn_y = get_terrain_height(spawn_x, spawn_z);

    registry_.emplace<ecs::NetworkId>(entity, net_id);

    ecs::Transform transform;
    transform.x = spawn_x;
    transform.y = spawn_y;
    transform.z = spawn_z;
    registry_.emplace<ecs::Transform>(entity, transform);
    registry_.emplace<ecs::Velocity>(entity);
    
    const auto& cls = config_->get_class(static_cast<int>(player_class));
    float max_health = cls.health;
    float damage = cls.damage;
    float range = cls.attack_range;
    float cooldown = cls.attack_cooldown;
    
    registry_.emplace<ecs::Health>(entity, max_health, max_health);
    registry_.emplace<ecs::Combat>(entity, damage, range, cooldown, 0.0f, false);
    
    ecs::EntityInfo info;
    info.type = EntityType::Player;
    info.player_class = static_cast<uint8_t>(player_class);
    info.color = generate_color(player_class);
    registry_.emplace<ecs::EntityInfo>(entity, info);
    
    registry_.emplace<ecs::Name>(entity, name);
    registry_.emplace<ecs::PlayerTag>(entity);
    registry_.emplace<ecs::InputState>(entity);
    registry_.emplace<ecs::Scale>(entity);  // Default scale = 1.0
    
    // Add physics collider for player (dynamic body for collision response)
    // Size matches visual scale from client rendering
    float player_target_size = config::get_character_target_size(EntityType::Player);
    ecs::Collider collider;
    collider.type = ecs::ColliderType::Capsule;
    collider.radius = config::get_collision_radius(player_target_size);
    collider.half_height = config::get_collision_half_height(player_target_size);
    // Offset so capsule is centered at player mid-height (feet at transform.y)
    collider.offset_y = collider.half_height + collider.radius;
    registry_.emplace<ecs::Collider>(entity, collider);
    
    ecs::RigidBody rb;
    rb.motion_type = ecs::PhysicsMotionType::Dynamic; // Dynamic for collision response
    rb.lock_rotation = true;
    rb.mass = 70.0f;  // ~70kg for a character
    rb.linear_damping = 0.9f;  // High damping for responsive control
    registry_.emplace<ecs::RigidBody>(entity, rb);
    
    return net_id;
}

void World::remove_player(uint32_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto entity = find_entity_by_network_id(player_id);
    if (entity != entt::null) {
        // Remove physics body first
        physics_.destroy_body(registry_, entity);
        registry_.destroy(entity);
    }
}

void World::update_player_input(uint32_t player_id, const PlayerInput& input) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto entity = find_entity_by_network_id(player_id);
    if (entity != entt::null && registry_.all_of<ecs::InputState>(entity)) {
        registry_.get<ecs::InputState>(entity).input = input;
        
        // Always update attack direction so player faces where mouse is pointing
        if (!registry_.all_of<ecs::AttackDirection>(entity)) {
            registry_.emplace<ecs::AttackDirection>(entity);
        }
        auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
        attack_dir.x = input.attack_dir_x;
        attack_dir.y = input.attack_dir_y;
    }
}

void World::spawn_environment() {
    // Use fixed seed for deterministic placement (same on all clients/servers)
    std::mt19937 env_rng(12345);
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> rotation_dist(0.0f, 360.0f);
    std::uniform_real_distribution<float> scale_var(0.8f, 1.2f);
    
    float world_center_x = config_->world().width / 2.0f;
    float world_center_z = config_->world().height / 2.0f;

    auto spawn_env = [&](EnvironmentType type, float x, float z, float scale, float rotation) {
        auto entity = registry_.create();

        // Get terrain height at spawn position
        float y = get_terrain_height(x, z);

        registry_.emplace<ecs::NetworkId>(entity, next_network_id());

        ecs::Transform transform;
        transform.x = x;
        transform.y = y;
        transform.z = z;
        transform.rotation = rotation * 3.14159f / 180.0f;
        registry_.emplace<ecs::Transform>(entity, transform);
        registry_.emplace<ecs::Velocity>(entity);
        registry_.emplace<ecs::Health>(entity, 9999.0f, 9999.0f);
        registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);
        
        ecs::EntityInfo info;
        info.type = EntityType::Environment;
        info.environment_type = static_cast<uint8_t>(type);
        info.color = config::is_tree_type(type) ? 0xFF228822 : 0xFF666666;
        registry_.emplace<ecs::EntityInfo>(entity, info);
        
        const char* model_name = config::get_environment_model_name(type);
        registry_.emplace<ecs::Name>(entity, model_name);
        registry_.emplace<ecs::StaticTag>(entity);
        registry_.emplace<ecs::Scale>(entity, scale);
        
        // Add physics collider
        ecs::Collider collider;
        if (config::is_tree_type(type)) {
            // Trees use cylinder/capsule for trunk
            collider.type = ecs::ColliderType::Capsule;
            collider.radius = config::get_tree_collision_radius(type, scale);
            collider.half_height = scale * 0.4f;
            // Offset so capsule is centered at tree trunk mid-height
            collider.offset_y = collider.half_height + collider.radius;
        } else {
            // Rocks use box colliders
            collider.type = ecs::ColliderType::Box;
            config::get_environment_collision_size(type, scale,
                collider.half_extents_x, collider.half_extents_y, collider.half_extents_z);
            // Offset so box is centered at rock's visual center (sitting on terrain)
            collider.offset_y = collider.half_extents_y;
        }
        registry_.emplace<ecs::Collider>(entity, collider);
        
        ecs::RigidBody rb;
        rb.motion_type = ecs::PhysicsMotionType::Static;
        registry_.emplace<ecs::RigidBody>(entity, rb);
    };
    
    // Spawn rocks in zones (matching client-side distribution)
    // Zone 1: Just outside town area
    for (int i = 0; i < 40; ++i) {
        float angle = angle_dist(env_rng);
        float dist = 800.0f + (env_rng() / static_cast<float>(env_rng.max())) * 700.0f;
        float x = world_center_x + std::cos(angle) * dist;
        float y = world_center_z + std::sin(angle) * dist;
        float scale = 15.0f + (env_rng() / static_cast<float>(env_rng.max())) * 25.0f;
        float rotation = rotation_dist(env_rng);
        EnvironmentType rock_type = static_cast<EnvironmentType>(env_rng() % 5);
        spawn_env(rock_type, x, y, scale, rotation);
    }
    
    // Zone 2: Mid distance
    for (int i = 0; i < 60; ++i) {
        float angle = angle_dist(env_rng);
        float dist = 1500.0f + (env_rng() / static_cast<float>(env_rng.max())) * 1000.0f;
        float x = world_center_x + std::cos(angle) * dist;
        float y = world_center_z + std::sin(angle) * dist;
        float scale = 25.0f + (env_rng() / static_cast<float>(env_rng.max())) * 40.0f;
        float rotation = rotation_dist(env_rng);
        EnvironmentType rock_type = static_cast<EnvironmentType>(env_rng() % 5);
        spawn_env(rock_type, x, y, scale, rotation);
    }
    
    // Zone 3: Near mountains
    for (int i = 0; i < 50; ++i) {
        float angle = angle_dist(env_rng);
        float dist = 2500.0f + (env_rng() / static_cast<float>(env_rng.max())) * 1000.0f;
        float x = world_center_x + std::cos(angle) * dist;
        float y = world_center_z + std::sin(angle) * dist;
        float scale = 40.0f + (env_rng() / static_cast<float>(env_rng.max())) * 60.0f;
        float rotation = rotation_dist(env_rng);
        EnvironmentType rock_type = static_cast<EnvironmentType>(env_rng() % 5);
        spawn_env(rock_type, x, y, scale, rotation);
    }
    
    // Use different seed for trees
    std::mt19937 tree_rng(67890);
    
    // Track tree positions for minimum distance check
    std::vector<std::pair<float, float>> tree_positions;
    auto is_too_close = [&](float x, float y, float min_dist) {
        float min_dist_sq = min_dist * min_dist;
        for (const auto& pos : tree_positions) {
            float dx = x - pos.first;
            float dy = y - pos.second;
            if (dx * dx + dy * dy < min_dist_sq) return true;
        }
        return false;
    };
    
    const float base_min_dist = 150.0f;
    
    // Zone 1: Forest patches near playable area
    for (int i = 0; i < 30; ++i) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            float angle = angle_dist(tree_rng);
            float dist = 400.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 500.0f;
            float x = world_center_x + std::cos(angle) * dist;
            float y = world_center_z + std::sin(angle) * dist;
            
            if (!is_too_close(x, y, base_min_dist)) {
                float scale = 240.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 320.0f;
                float rotation = rotation_dist(tree_rng);
                EnvironmentType tree_type = static_cast<EnvironmentType>(
                    static_cast<int>(EnvironmentType::TreeOak) + (tree_rng() % 2));
                spawn_env(tree_type, x, y, scale, rotation);
                tree_positions.push_back({x, y});
                break;
            }
        }
    }
    
    // Zone 2: Scattered trees mid distance
    for (int i = 0; i < 50; ++i) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            float angle = angle_dist(tree_rng);
            float dist = 900.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 900.0f;
            float x = world_center_x + std::cos(angle) * dist;
            float y = world_center_z + std::sin(angle) * dist;
            
            if (!is_too_close(x, y, base_min_dist * 1.5f)) {
                float scale = 320.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 400.0f;
                float rotation = rotation_dist(tree_rng);
                EnvironmentType tree_type = static_cast<EnvironmentType>(
                    static_cast<int>(EnvironmentType::TreeOak) + (tree_rng() % 2));
                spawn_env(tree_type, x, y, scale, rotation);
                tree_positions.push_back({x, y});
                break;
            }
        }
    }
    
    // Zone 3: Sparse trees near mountains
    for (int i = 0; i < 25; ++i) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            float angle = angle_dist(tree_rng);
            float dist = 1800.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 1000.0f;
            float x = world_center_x + std::cos(angle) * dist;
            float y = world_center_z + std::sin(angle) * dist;
            
            if (!is_too_close(x, y, base_min_dist * 2.0f)) {
                float scale = 400.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 480.0f;
                float rotation = rotation_dist(tree_rng);
                EnvironmentType tree_type = static_cast<EnvironmentType>(
                    static_cast<int>(EnvironmentType::TreeOak) + (tree_rng() % 2));
                spawn_env(tree_type, x, y, scale, rotation);
                tree_positions.push_back({x, y});
                break;
            }
        }
    }
    
    // Clustered groves
    for (int grove = 0; grove < 4; ++grove) {
        float grove_angle = grove * (2.0f * 3.14159f / 4.0f) + 
                           (tree_rng() / static_cast<float>(tree_rng.max())) * 0.5f;
        float grove_dist = 600.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 800.0f;
        float grove_x = world_center_x + std::cos(grove_angle) * grove_dist;
        float grove_y = world_center_z + std::sin(grove_angle) * grove_dist;
        
        int grove_size = 10 + tree_rng() % 6;
        int grove_tree_type = tree_rng() % 2;
        
        for (int i = 0; i < grove_size; ++i) {
            for (int attempt = 0; attempt < 10; ++attempt) {
                float offset_angle = angle_dist(tree_rng);
                float offset_dist = 50.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 150.0f;
                float x = grove_x + std::cos(offset_angle) * offset_dist;
                float y = grove_y + std::sin(offset_angle) * offset_dist;
                
                if (!is_too_close(x, y, base_min_dist)) {
                    float scale = 280.0f + (tree_rng() / static_cast<float>(tree_rng.max())) * 280.0f;
                    float rotation = rotation_dist(tree_rng);
                    // Mostly same tree type in grove, occasional mix
                    int final_type = (tree_rng() % 10 < 7) ? grove_tree_type : (1 - grove_tree_type);
                    EnvironmentType tree_type = static_cast<EnvironmentType>(
                        static_cast<int>(EnvironmentType::TreeOak) + final_type);
                    spawn_env(tree_type, x, y, scale, rotation);
                    tree_positions.push_back({x, y});
                    break;
                }
            }
        }
    }
    
    std::cout << "Spawned " << (150 + tree_positions.size()) << " environment objects (rocks + trees)" << std::endl;
}

void World::update(float dt) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update game systems
    systems::update_movement(registry_, dt, *config_);
    systems::update_ai(registry_, dt, *config_);
    systems::update_combat(registry_, dt, *config_);
    
    // Update physics (handles collision detection and response)
    physics_.update(registry_, dt);
}

std::vector<NetEntityState> World::get_all_entities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<NetEntityState> result;
    
    auto view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Velocity, 
                               ecs::Health, ecs::Combat, ecs::EntityInfo, ecs::Name>();
    
    for (auto entity : view) {
        const auto& net_id = view.get<ecs::NetworkId>(entity);
        const auto& transform = view.get<ecs::Transform>(entity);
        const auto& velocity = view.get<ecs::Velocity>(entity);
        const auto& health = view.get<ecs::Health>(entity);
        const auto& combat = view.get<ecs::Combat>(entity);
        const auto& info = view.get<ecs::EntityInfo>(entity);
        const auto& name = view.get<ecs::Name>(entity);
        
        NetEntityState state;
        state.id = net_id.id;
        state.type = info.type;
        state.player_class = info.player_class;
        state.npc_type = info.npc_type;
        state.building_type = info.building_type;
        state.environment_type = info.environment_type;
        state.x = transform.x;
        state.y = transform.y;
        state.z = transform.z;
        state.rotation = transform.rotation;
        state.vx = velocity.x;
        state.vy = velocity.z;
        state.health = health.current;
        state.max_health = health.max;
        state.color = info.color;
        state.is_attacking = combat.is_attacking;
        state.attack_cooldown = combat.current_cooldown;
        
        // Include attack direction if available
        if (registry_.all_of<ecs::AttackDirection>(entity)) {
            const auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
            state.attack_dir_x = attack_dir.x;
            state.attack_dir_y = attack_dir.y;
        }
        
        // Include per-instance scale if available
        if (registry_.all_of<ecs::Scale>(entity)) {
            state.scale = registry_.get<ecs::Scale>(entity).value;
        }

        std::strncpy(state.name, name.value.c_str(), 31);
        state.name[31] = '\0';

        // Populate rendering data so client can render without game knowledge
        populate_render_data(state, info, combat);

        result.push_back(state);
    }
    
    return result;
}

entt::entity World::find_entity_by_network_id(uint32_t id) const {
    auto view = registry_.view<ecs::NetworkId>();
    for (auto entity : view) {
        if (view.get<ecs::NetworkId>(entity).id == id) {
            return entity;
        }
    }
    return entt::null;
}

size_t World::player_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return registry_.view<ecs::PlayerTag>().size();
}

size_t World::npc_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return registry_.view<ecs::NPCTag>().size();
}

uint32_t World::next_network_id() {
    return next_id_++;
}

void World::populate_render_data(NetEntityState& state, const ecs::EntityInfo& info, const ecs::Combat& combat) const {
    auto strncpy_safe = [](char* dst, const char* src, size_t n) {
        std::strncpy(dst, src, n - 1);
        dst[n - 1] = '\0';
    };

    switch (info.type) {
        case EntityType::Player: {
            const auto& cls = config_->get_class(info.player_class);
            strncpy_safe(state.model_name, cls.model.c_str(), 32);
            state.target_size = config::get_character_target_size(EntityType::Player);
            strncpy_safe(state.effect_type, cls.effect_type.c_str(), 16);
            strncpy_safe(state.effect_model, cls.effect_model.c_str(), 32);
            state.effect_duration = cls.effect_duration;
            state.cone_angle = cls.cone_angle;
            state.shows_reticle = cls.shows_reticle;
            break;
        }
        case EntityType::NPC:
            strncpy_safe(state.model_name, config::get_npc_model_name(static_cast<NPCType>(info.npc_type)), 32);
            state.target_size = config::get_character_target_size(EntityType::NPC);
            break;
        case EntityType::TownNPC:
            strncpy_safe(state.model_name, config::get_npc_model_name(static_cast<NPCType>(info.npc_type)), 32);
            state.target_size = config::get_character_target_size(EntityType::TownNPC);
            break;
        case EntityType::Building:
            strncpy_safe(state.model_name, config::get_building_model_name(static_cast<BuildingType>(info.building_type)), 32);
            state.target_size = config::get_building_target_size(static_cast<BuildingType>(info.building_type));
            break;
        case EntityType::Environment:
            strncpy_safe(state.model_name, config::get_environment_model_name(static_cast<EnvironmentType>(info.environment_type)), 32);
            state.target_size = config::get_environment_target_scale(static_cast<EnvironmentType>(info.environment_type));
            break;
    }
}

uint32_t World::generate_color(PlayerClass player_class) {
    return config_->get_class(static_cast<int>(player_class)).color;
}

void World::generate_heightmap() {
    std::cout << "[World] Generating heightmap..." << std::endl;
    
    // Initialize chunk for the entire world (single chunk for now)
    heightmap_init(heightmap_, 0, 0, heightmap_config::CHUNK_RESOLUTION);
    
    // Generate using procedural algorithm
    heightmap_generator::generate_procedural(heightmap_, config_->world().width, config_->world().height);
    
    std::cout << "[World] Heightmap generated: " << heightmap_.resolution << "x" << heightmap_.resolution 
              << " (" << heightmap_.serialized_size() / 1024 << " KB)" << std::endl;
}

} // namespace mmo::server

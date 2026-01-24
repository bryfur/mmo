#include "world.hpp"
#include "systems/movement_system.hpp"
#include "systems/combat_system.hpp"
#include "systems/ai_system.hpp"
#include "systems/physics_system.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>

namespace mmo {

// Town center location
constexpr float TOWN_CENTER_X = config::WORLD_WIDTH / 2.0f;
constexpr float TOWN_CENTER_Y = config::WORLD_HEIGHT / 2.0f;
constexpr float TOWN_SAFE_RADIUS = 400.0f;

World::World() : rng_(std::random_device{}()) {
    // Initialize physics with zero gravity for top-down game
    physics_.initialize();
    physics_.set_gravity(0.0f, 0.0f, 0.0f);
    
    // Setup collision callbacks
    setup_collision_callbacks();
    
    spawn_town();
    spawn_npcs();
}

World::~World() {
    physics_.shutdown();
}

void World::setup_collision_callbacks() {
    physics_.set_collision_callback([this](entt::entity a, entt::entity b, 
                                           const ecs::CollisionEvent& event) {
        // Handle collision events here
        // For example: damage on collision, trigger effects, etc.
        
        // Check if this is a player-NPC collision
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
    // Create buildings around town center
    struct BuildingPlacement {
        BuildingType type;
        float offset_x;
        float offset_y;
        const char* name;
    };
    
    std::vector<BuildingPlacement> buildings = {
        {BuildingType::Tavern,     -180.0f, -140.0f, "The Golden Flagon"},
        {BuildingType::Blacksmith,  200.0f,  -80.0f, "Iron Forge"},
        {BuildingType::Tower,      -220.0f,  160.0f, "Guard Tower"},
        {BuildingType::Shop,        140.0f,  150.0f, "General Store"},
        {BuildingType::Well,          0.0f,    0.0f, "Town Well"},
        {BuildingType::House,       240.0f,  180.0f, "Cottage"},
    };
    
    for (const auto& b : buildings) {
        auto entity = registry_.create();
        
        registry_.emplace<ecs::NetworkId>(entity, next_network_id());
        registry_.emplace<ecs::Transform>(entity, TOWN_CENTER_X + b.offset_x, TOWN_CENTER_Y + b.offset_y);
        registry_.emplace<ecs::Velocity>(entity);
        registry_.emplace<ecs::Health>(entity, 9999.0f, 9999.0f);
        registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);
        
        ecs::EntityInfo info;
        info.type = EntityType::Building;
        info.building_type = b.type;
        info.color = 0xFFBB9977;
        registry_.emplace<ecs::EntityInfo>(entity, info);
        
        registry_.emplace<ecs::Name>(entity, b.name);
        registry_.emplace<ecs::StaticTag>(entity);
        
        // Add physics collider for buildings (box shape, static)
        ecs::Collider collider;
        collider.type = ecs::ColliderType::Box;
        collider.half_extents_x = 40.0f;  // Building width/2
        collider.half_extents_y = 20.0f;  // Building height (vertical)
        collider.half_extents_z = 40.0f;  // Building depth/2
        registry_.emplace<ecs::Collider>(entity, collider);
        
        ecs::RigidBody rb;
        rb.motion_type = ecs::PhysicsMotionType::Static;
        registry_.emplace<ecs::RigidBody>(entity, rb);
    }
    
    // Create town NPCs
    struct TownNPCPlacement {
        NPCType type;
        float offset_x;
        float offset_y;
        const char* name;
        bool wanders;
    };
    
    std::vector<TownNPCPlacement> town_npcs = {
        {NPCType::Innkeeper,  -180.0f, -100.0f, "Barthos the Innkeeper", false},
        {NPCType::Blacksmith,  200.0f,  -40.0f, "Grimhammer", false},
        {NPCType::Merchant,    140.0f,  100.0f, "Elara the Merchant", false},
        {NPCType::Guard,      -220.0f,  120.0f, "Guard Captain", false},
        {NPCType::Guard,       160.0f, -160.0f, "Town Guard", true},
        {NPCType::Villager,    -60.0f,  120.0f, "Peasant", true},
        {NPCType::Villager,     80.0f, -100.0f, "Farmer", true},
        {NPCType::Villager,   -200.0f,  -60.0f, "Wanderer", true},
    };
    
    for (const auto& npc : town_npcs) {
        auto entity = registry_.create();
        float x = TOWN_CENTER_X + npc.offset_x;
        float y = TOWN_CENTER_Y + npc.offset_y;
        
        registry_.emplace<ecs::NetworkId>(entity, next_network_id());
        registry_.emplace<ecs::Transform>(entity, x, y);
        registry_.emplace<ecs::Velocity>(entity);
        registry_.emplace<ecs::Health>(entity, 1000.0f, 1000.0f);
        registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);
        
        ecs::EntityInfo info;
        info.type = EntityType::TownNPC;
        info.npc_type = npc.type;
        
        // Color based on NPC type
        switch (npc.type) {
            case NPCType::Innkeeper:  info.color = 0xFF88AA55; break;
            case NPCType::Blacksmith: info.color = 0xFF5555AA; break;
            case NPCType::Merchant:   info.color = 0xFFAA8855; break;
            case NPCType::Guard:      info.color = 0xFF5588AA; break;
            case NPCType::Villager:   info.color = 0xFF888888; break;
            default:                  info.color = 0xFFAAAAAA; break;
        }
        registry_.emplace<ecs::EntityInfo>(entity, info);
        
        registry_.emplace<ecs::Name>(entity, npc.name);
        
        // Add physics collider for town NPCs
        ecs::Collider collider;
        collider.type = ecs::ColliderType::Capsule;
        collider.radius = config::NPC_SIZE / 2.0f;
        collider.half_height = 16.0f;
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
            ai.home_y = y;
            ai.wander_radius = 80.0f;
            registry_.emplace<ecs::TownNPCAI>(entity, ai);
        } else {
            registry_.emplace<ecs::StaticTag>(entity);
        }
    }
}

void World::spawn_npcs() {
    // Spawn hostile NPCs OUTSIDE the town safe zone
    std::uniform_real_distribution<float> dist_x(100.0f, config::WORLD_WIDTH - 100.0f);
    std::uniform_real_distribution<float> dist_y(100.0f, config::WORLD_HEIGHT - 100.0f);
    
    int spawned = 0;
    while (spawned < config::NPC_COUNT) {
        float x = dist_x(rng_);
        float y = dist_y(rng_);
        
        // Skip if inside town safe zone
        float dx = x - TOWN_CENTER_X;
        float dy = y - TOWN_CENTER_Y;
        if (dx * dx + dy * dy < (TOWN_SAFE_RADIUS + 100.0f) * (TOWN_SAFE_RADIUS + 100.0f)) {
            continue;
        }
        
        auto entity = registry_.create();
        
        registry_.emplace<ecs::NetworkId>(entity, next_network_id());
        registry_.emplace<ecs::Transform>(entity, x, y);
        registry_.emplace<ecs::Velocity>(entity);
        registry_.emplace<ecs::Health>(entity, config::NPC_HEALTH, config::NPC_HEALTH);
        registry_.emplace<ecs::Combat>(entity, config::NPC_DAMAGE, config::NPC_ATTACK_RANGE, 
                                       config::NPC_ATTACK_COOLDOWN, 0.0f, false);
        
        ecs::EntityInfo info;
        info.type = EntityType::NPC;
        info.npc_type = NPCType::Monster;
        info.color = 0xFF4444FF;
        registry_.emplace<ecs::EntityInfo>(entity, info);
        
        registry_.emplace<ecs::Name>(entity, "Monster_" + std::to_string(spawned + 1));
        registry_.emplace<ecs::NPCTag>(entity);
        registry_.emplace<ecs::AIState>(entity);
        
        // Add physics collider for hostile NPCs (dynamic for collision response)
        ecs::Collider collider;
        collider.type = ecs::ColliderType::Capsule;
        collider.radius = config::NPC_SIZE / 2.0f;
        collider.half_height = 16.0f;
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
    std::uniform_real_distribution<float> dist_offset(-50.0f, 50.0f);
    float spawn_x = TOWN_CENTER_X + dist_offset(rng_);
    float spawn_y = TOWN_CENTER_Y + dist_offset(rng_);
    
    registry_.emplace<ecs::NetworkId>(entity, net_id);
    registry_.emplace<ecs::Transform>(entity, spawn_x, spawn_y);
    registry_.emplace<ecs::Velocity>(entity);
    
    float max_health, damage, range, cooldown;
    switch (player_class) {
        case PlayerClass::Warrior:
            max_health = config::WARRIOR_HEALTH;
            damage = config::WARRIOR_DAMAGE;
            range = config::WARRIOR_ATTACK_RANGE;
            cooldown = config::WARRIOR_ATTACK_COOLDOWN;
            break;
        case PlayerClass::Mage:
            max_health = config::MAGE_HEALTH;
            damage = config::MAGE_DAMAGE;
            range = config::MAGE_ATTACK_RANGE;
            cooldown = config::MAGE_ATTACK_COOLDOWN;
            break;
        case PlayerClass::Paladin:
            max_health = config::PALADIN_HEALTH;
            damage = config::PALADIN_DAMAGE;
            range = config::PALADIN_ATTACK_RANGE;
            cooldown = config::PALADIN_ATTACK_COOLDOWN;
            break;
        case PlayerClass::Archer:
            max_health = config::ARCHER_HEALTH;
            damage = config::ARCHER_DAMAGE;
            range = config::ARCHER_ATTACK_RANGE;
            cooldown = config::ARCHER_ATTACK_COOLDOWN;
            break;
    }
    
    registry_.emplace<ecs::Health>(entity, max_health, max_health);
    registry_.emplace<ecs::Combat>(entity, damage, range, cooldown, 0.0f, false);
    
    ecs::EntityInfo info;
    info.type = EntityType::Player;
    info.player_class = player_class;
    info.color = generate_color(player_class);
    registry_.emplace<ecs::EntityInfo>(entity, info);
    
    registry_.emplace<ecs::Name>(entity, name);
    registry_.emplace<ecs::PlayerTag>(entity);
    registry_.emplace<ecs::InputState>(entity);
    
    // Add physics collider for player (dynamic body for collision response)
    ecs::Collider collider;
    collider.type = ecs::ColliderType::Capsule;
    collider.radius = config::PLAYER_SIZE / 2.0f;
    collider.half_height = 16.0f;
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

void World::update(float dt) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update game systems
    systems::update_movement(registry_, dt);
    systems::update_ai(registry_, dt);
    systems::update_combat(registry_, dt);
    
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
        state.x = transform.x;
        state.y = transform.y;
        state.vx = velocity.x;
        state.vy = velocity.y;
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
        
        std::strncpy(state.name, name.value.c_str(), 31);
        state.name[31] = '\0';
        
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

uint32_t World::generate_color(PlayerClass player_class) {
    switch (player_class) {
        case PlayerClass::Warrior: return 0xFFFF6666;
        case PlayerClass::Mage:    return 0xFF6666FF;
        case PlayerClass::Paladin: return 0xFFFFDD66;
    }
    return 0xFFFFFFFF;
}

} // namespace mmo

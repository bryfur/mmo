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
#include "systems/buff_system.hpp"
#include "systems/physics_system.hpp"
#include "systems/leveling_system.hpp"
#include "systems/loot_system.hpp"
#include "systems/quest_system.hpp"
#include "systems/skill_system.hpp"
#include "systems/zone_system.hpp"
#include "entity_config.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mmo::server {

using namespace mmo::protocol;

World::World(const GameConfig& config)
    : config_(&config)
    , zone_system_(config)
    , spatial_grid_(config.network().spatial_grid_cell_size)
    , rng_(std::random_device{}()) {
    // Load heightmap from editor save
    load_heightmap();

    // Initialize physics with gravity for 3D game
    physics_.initialize();
    physics_.set_gravity(0.0f, -9.81f, 0.0f);

    // Set terrain height callback for ground snapping
    physics_.set_terrain_height_callback([this](float x, float z) {
        return get_terrain_height(x, z);
    });

    // Setup collision callbacks
    setup_collision_callbacks();

    // Load entities from editor save
    if (!spawn_from_world_data()) {
        std::cout << "[World] No editor world data found (data/editor_save/world_entities.json). "
                  << "Use the editor to generate and save a world.\n";
    }
    
    // Spawn zone-based monsters across the world
    zone_system_.spawn_initial_monsters(
        registry_,
        [this]() { return next_network_id(); },
        [this](float x, float z) { return get_terrain_height(x, z); }
    );

    // Add zone monsters to spatial grid and network ID map
    auto zone_monsters = registry_.view<ecs::NetworkId, ecs::Transform, ecs::EntityInfo, ecs::MonsterTypeId>();
    for (auto entity : zone_monsters) {
        auto& net_id = zone_monsters.get<ecs::NetworkId>(entity);
        auto& transform = zone_monsters.get<ecs::Transform>(entity);
        auto& info = zone_monsters.get<ecs::EntityInfo>(entity);
        if (network_id_to_entity_.find(net_id.id) == network_id_to_entity_.end()) {
            network_id_to_entity_[net_id.id] = entity;
            spatial_grid_.update_entity(net_id.id, transform.x, transform.z, info.type);
        }
    }

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

uint32_t World::add_player(const std::string& name, PlayerClass player_class) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto entity = registry_.create();
    uint32_t net_id = next_network_id();
    
    // Spawn players in the town (safe zone)
    // Town is at old world center from editor save data
    const float town_center_x = 4000.0f;
    const float town_center_z = 4000.0f;
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
    registry_.emplace<ecs::BuffState>(entity);  // Empty buff state for status effects

    // Progression components
    ecs::PlayerLevel player_level;
    player_level.mana = 100.0f;
    player_level.max_mana = 100.0f;
    player_level.mana_regen = 5.0f;
    registry_.emplace<ecs::PlayerLevel>(entity, player_level);
    registry_.emplace<ecs::Inventory>(entity);
    registry_.emplace<ecs::Equipment>(entity);
    registry_.emplace<ecs::QuestState>(entity);
    registry_.emplace<ecs::SkillState>(entity);
    registry_.emplace<ecs::TalentState>(entity);

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

    // Add to spatial grid
    spatial_grid_.update_entity(net_id, spawn_x, spawn_z, EntityType::Player);

    // Add to fast lookup map
    network_id_to_entity_[net_id] = entity;

    return net_id;
}

void World::remove_player(uint32_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto entity = find_entity_by_network_id(player_id);
    if (entity != entt::null) {
        // Remove from spatial grid
        spatial_grid_.remove_entity(player_id);

        // Remove from fast lookup map
        network_id_to_entity_.erase(player_id);

        // Remove physics body first
        physics_.destroy_body(registry_, entity);
        registry_.destroy(entity);
    }
}

void World::update_player_input(uint32_t player_id, const PlayerInput& input) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto entity = find_entity_by_network_id(player_id);
    if (entity != entt::null && registry_.all_of<ecs::InputState>(entity)) {
        auto& state = registry_.get<ecs::InputState>(entity);
        // Latch attacking flag so it persists until the combat system consumes it.
        // Without this, a subsequent input packet with attacking=false can overwrite
        // a pending attack before the server tick processes it.
        bool was_attacking = state.input.attacking;
        state.input = input;
        if (was_attacking && !input.attacking) {
            state.input.attacking = true;
        }
        
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
    systems::update_movement(registry_, dt, *config_);
    systems::update_ai(registry_, dt, *config_);
    auto combat_hits = systems::update_combat(registry_, dt, *config_);

    // Generate CombatEvent and EntityDeath events from combat hits
    for (const auto& hit : combat_hits) {
        uint32_t attacker_net_id = 0;
        uint32_t target_net_id = 0;
        if (auto* net = registry_.try_get<ecs::NetworkId>(hit.attacker))
            attacker_net_id = net->id;
        if (auto* net = registry_.try_get<ecs::NetworkId>(hit.target))
            target_net_id = net->id;

        // Send CombatEvent to all nearby players (broadcast via player_id=0,
        // which send_progression_updates will broadcast)
        // For now, send to all players that are involved or nearby
        {
            // Send to all connected players - they'll filter by visibility client-side
            auto players_view = registry_.view<ecs::PlayerTag, ecs::NetworkId>();
            for (auto player : players_view) {
                auto& pnet = players_view.get<ecs::NetworkId>(player);
                GameplayEvent evt;
                evt.type = GameplayEvent::Type::CombatEvent;
                evt.player_id = pnet.id;
                evt.attacker_id = attacker_net_id;
                evt.target_id = target_net_id;
                evt.damage_amount = hit.damage;
                pending_events_.push_back(evt);
            }
        }

        // Entity death event
        if (hit.target_died) {
            auto players_view = registry_.view<ecs::PlayerTag, ecs::NetworkId>();
            for (auto player : players_view) {
                auto& pnet = players_view.get<ecs::NetworkId>(player);
                GameplayEvent evt;
                evt.type = GameplayEvent::Type::EntityDeath;
                evt.player_id = pnet.id;
                evt.dead_entity_id = target_net_id;
                evt.killer_entity_id = attacker_net_id;
                pending_events_.push_back(evt);
            }
        }
    }

    // Update progression systems
    systems::update_mana_regen(registry_, dt);
    systems::update_skill_cooldowns(registry_, dt);

    // Update quests and generate progress/complete events
    auto quest_changes = systems::update_quests(registry_, dt, *config_);
    for (auto& [entity, change] : quest_changes) {
        auto* net_id = registry_.try_get<ecs::NetworkId>(entity);
        if (!net_id) continue;

        if (change.quest_complete) {
            GameplayEvent evt;
            evt.type = GameplayEvent::Type::QuestComplete;
            evt.player_id = net_id->id;
            evt.quest_id = change.quest_id;
            evt.quest_name = change.quest_name;
            pending_events_.push_back(evt);
        } else {
            GameplayEvent evt;
            evt.type = GameplayEvent::Type::QuestProgress;
            evt.player_id = net_id->id;
            evt.quest_id = change.quest_id;
            evt.objective_index = change.objective_index;
            evt.obj_current = change.current;
            evt.obj_required = change.required;
            evt.obj_complete = change.objective_complete;
            pending_events_.push_back(evt);
        }
    }

    // Check for dead monsters - award XP/loot and respawn
    {
        auto dead_npcs = registry_.view<ecs::NPCTag, ecs::Health, ecs::MonsterTypeId>();
        for (auto entity : dead_npcs) {
            auto& health = dead_npcs.get<ecs::Health>(entity);
            if (health.is_alive()) continue;

            auto& monster_info = dead_npcs.get<ecs::MonsterTypeId>(entity);

            // Award XP/loot to nearest player who killed it
            auto& transform = registry_.get<ecs::Transform>(entity);
            float best_dist = 1000.0f;
            entt::entity killer = entt::null;
            auto players = registry_.view<ecs::PlayerTag, ecs::Transform, ecs::Health>();
            for (auto player : players) {
                auto& ph = players.get<ecs::Health>(player);
                if (!ph.is_alive()) continue;
                auto& pt = players.get<ecs::Transform>(player);
                float dx = pt.x - transform.x;
                float dz = pt.z - transform.z;
                float dist = std::sqrt(dx * dx + dz * dz);
                if (dist < best_dist) {
                    best_dist = dist;
                    killer = player;
                }
            }

            if (killer != entt::null) {
                // Snapshot XP/level before awarding so we can compute the gain
                auto& killer_level_pre = registry_.get<ecs::PlayerLevel>(killer);
                int xp_before = killer_level_pre.xp;
                int level_before = killer_level_pre.level;

                systems::award_kill_xp(registry_, killer, entity, *config_);
                auto loot = systems::roll_loot(monster_info.type_id, *config_);
                systems::give_loot(registry_, killer, loot);

                // Process quest kill objectives and generate events
                auto quest_kill_changes = systems::on_monster_killed(registry_, killer, monster_info.type_id, *config_);
                auto& killer_net_id = registry_.get<ecs::NetworkId>(killer);

                for (auto& change : quest_kill_changes) {
                    if (change.quest_complete) {
                        GameplayEvent evt;
                        evt.type = GameplayEvent::Type::QuestComplete;
                        evt.player_id = killer_net_id.id;
                        evt.quest_id = change.quest_id;
                        evt.quest_name = change.quest_name;
                        pending_events_.push_back(evt);
                    } else {
                        GameplayEvent evt;
                        evt.type = GameplayEvent::Type::QuestProgress;
                        evt.player_id = killer_net_id.id;
                        evt.quest_id = change.quest_id;
                        evt.objective_index = change.objective_index;
                        evt.obj_current = change.current;
                        evt.obj_required = change.required;
                        evt.obj_complete = change.objective_complete;
                        pending_events_.push_back(evt);
                    }
                }

                // Generate gameplay events for the client
                auto& killer_level = registry_.get<ecs::PlayerLevel>(killer);

                // XP gain event
                {
                    GameplayEvent evt;
                    evt.type = GameplayEvent::Type::XPGain;
                    evt.player_id = killer_net_id.id;
                    evt.xp_gained = killer_level.xp - xp_before;
                    evt.total_xp = killer_level.xp;
                    // Calculate XP to next level
                    const auto& xp_curve = config_->leveling().xp_curve;
                    int level_idx = killer_level.level;
                    evt.xp_to_next = (level_idx < static_cast<int>(xp_curve.size())) ? xp_curve[level_idx] : 99999;
                    evt.new_level = killer_level.level;
                    pending_events_.push_back(evt);
                }

                // Level up event (if level changed)
                if (killer_level.level > level_before) {
                    auto& killer_health = registry_.get<ecs::Health>(killer);
                    auto& killer_combat = registry_.get<ecs::Combat>(killer);

                    GameplayEvent evt;
                    evt.type = GameplayEvent::Type::LevelUp;
                    evt.player_id = killer_net_id.id;
                    evt.new_level = killer_level.level;
                    evt.new_max_health = killer_health.max;
                    evt.new_damage = killer_combat.damage;
                    evt.total_xp = killer_level.xp;
                    const auto& xp_curve = config_->leveling().xp_curve;
                    int level_idx = killer_level.level;
                    evt.xp_to_next = (level_idx < static_cast<int>(xp_curve.size())) ? xp_curve[level_idx] : 99999;
                    pending_events_.push_back(evt);
                }

                // Loot event
                if (loot.gold > 0 || !loot.items.empty()) {
                    GameplayEvent evt;
                    evt.type = GameplayEvent::Type::LootDrop;
                    evt.player_id = killer_net_id.id;
                    evt.loot_gold = loot.gold;
                    evt.total_gold = killer_level.gold;
                    for (auto& [item_id, count] : loot.items) {
                        const auto* item_cfg = config_->find_item(item_id);
                        std::string name = item_cfg ? item_cfg->name : item_id;
                        std::string rarity = item_cfg ? item_cfg->rarity : "common";
                        evt.loot_items.push_back({name, rarity, count});
                    }
                    pending_events_.push_back(evt);
                }

                // Inventory update after loot
                {
                    GameplayEvent evt;
                    evt.type = GameplayEvent::Type::InventoryUpdate;
                    evt.player_id = killer_net_id.id;
                    pending_events_.push_back(evt);
                }
            }

            // Respawn monster in its zone
            zone_system_.respawn_monster(
                registry_, entity,
                [this](float x, float z) { return get_terrain_height(x, z); }
            );
        }
    }

    // Update buff/debuff system (tick durations, apply DoT/HoT, remove expired)
    systems::update_buffs(registry_, dt);

    // Update physics (handles collision detection and response)
    physics_.update(registry_, dt);

    // Update spatial grid - only for entities that can move (have Velocity)
    // Static entities (buildings, environment) are added once at spawn and never change cell
    auto view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::EntityInfo, ecs::Velocity>();
    for (auto entity : view) {
        auto&& [net_id, transform, info, vel] = view.get(entity);
        if (vel.x == 0.0f && vel.z == 0.0f) continue;
        spatial_grid_.update_entity(net_id.id, transform.x, transform.z, info.type);
    }

    // Zone change detection
    {
        auto players = registry_.view<ecs::PlayerTag, ecs::Transform, ecs::NetworkId>();
        for (auto entity : players) {
            auto& transform = players.get<ecs::Transform>(entity);
            auto& net_id = players.get<ecs::NetworkId>(entity);

            const auto* zone = config_->find_zone_at(transform.x, transform.z);
            std::string zone_name = zone ? zone->name : "Wilderness";

            auto& last = player_zones_[net_id.id];
            if (zone_name != last) {
                last = zone_name;
                GameplayEvent evt;
                evt.type = GameplayEvent::Type::ZoneChange;
                evt.player_id = net_id.id;
                evt.zone_name = zone_name;
                pending_events_.push_back(evt);
            }
        }
    }
}

NetEntityState World::get_entity_state(uint32_t network_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto entity = find_entity_by_network_id(network_id);
    if (entity == entt::null) {
        return NetEntityState{};  // Return empty state if not found
    }

    const auto& net_id = registry_.get<ecs::NetworkId>(entity);
    const auto& transform = registry_.get<ecs::Transform>(entity);
    const auto& velocity = registry_.get<ecs::Velocity>(entity);
    const auto& health = registry_.get<ecs::Health>(entity);
    const auto& combat = registry_.get<ecs::Combat>(entity);
    const auto& info = registry_.get<ecs::EntityInfo>(entity);
    const auto& name = registry_.get<ecs::Name>(entity);

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
    if (const auto* attack_dir = registry_.try_get<ecs::AttackDirection>(entity)) {
        state.attack_dir_x = attack_dir->x;
        state.attack_dir_y = attack_dir->y;
    }

    // Include per-instance scale if available
    if (const auto* scale = registry_.try_get<ecs::Scale>(entity)) {
        state.scale = scale->value;
    }

    // Include mana for players
    if (const auto* player_level = registry_.try_get<ecs::PlayerLevel>(entity)) {
        state.mana = player_level->mana;
        state.max_mana = player_level->max_mana;
    }

    std::strncpy(state.name, name.value.c_str(), 31);
    state.name[31] = '\0';

    // Populate rendering data
    populate_render_data(state, info, combat);

    return state;
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
        if (const auto* attack_dir = registry_.try_get<ecs::AttackDirection>(entity)) {
            state.attack_dir_x = attack_dir->x;
            state.attack_dir_y = attack_dir->y;
        }

        // Include per-instance scale if available
        if (const auto* scale = registry_.try_get<ecs::Scale>(entity)) {
            state.scale = scale->value;
        }

        // Include mana for players
        if (const auto* player_level = registry_.try_get<ecs::PlayerLevel>(entity)) {
            state.mana = player_level->mana;
            state.max_mana = player_level->max_mana;
        }

        std::strncpy(state.name, name.value.c_str(), 31);
        state.name[31] = '\0';

        // Populate rendering data so client can render without game knowledge
        populate_render_data(state, info, combat);

        result.push_back(state);
    }

    return result;
}

std::vector<World::GameplayEvent> World::take_events() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(pending_events_);
}

entt::entity World::find_entity_by_network_id(uint32_t id) const {
    auto it = network_id_to_entity_.find(id);
    if (it != network_id_to_entity_.end()) {
        return it->second;
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

std::vector<uint32_t> World::query_entities_near(float x, float y, float radius) const {
    // Query spatial grid for nearby entities
    return spatial_grid_.query_radius(x, y, radius);
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
            strncpy_safe(state.animation, cls.animation.c_str(), 16);
            state.cone_angle = cls.cone_angle;
            state.shows_reticle = cls.shows_reticle;
            break;
        }
        case EntityType::NPC:
            strncpy_safe(state.model_name, config::get_npc_model_name(static_cast<NPCType>(info.npc_type)), 32);
            state.target_size = config::get_character_target_size(EntityType::NPC);
            strncpy_safe(state.animation, config_->monster().animation.c_str(), 16);
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

void World::load_heightmap() {
    const std::string path = "data/editor_save/heightmap.bin";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[World] No heightmap found at " << path
                  << ". Use the editor to generate and save a world.\n";
        // Initialize a flat heightmap so the server doesn't crash
        heightmap_init(heightmap_, 0, 0, protocol::heightmap_config::CHUNK_RESOLUTION);
        return;
    }

    // Editor binary format: resolution(u32), origin_x(f32), origin_z(f32),
    //                       world_size(f32), min_height(f32), max_height(f32),
    //                       then raw uint16 height data
    uint32_t resolution = 0;
    float origin_x = 0, origin_z = 0, world_size = 0, min_h = 0, max_h = 0;

    f.read(reinterpret_cast<char*>(&resolution), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&origin_x), sizeof(float));
    f.read(reinterpret_cast<char*>(&origin_z), sizeof(float));
    f.read(reinterpret_cast<char*>(&world_size), sizeof(float));
    f.read(reinterpret_cast<char*>(&min_h), sizeof(float));
    f.read(reinterpret_cast<char*>(&max_h), sizeof(float));

    if (!f || resolution == 0 || resolution > 4096) {
        std::cerr << "[World] Invalid editor heightmap at " << path << "\n";
        heightmap_init(heightmap_, 0, 0, protocol::heightmap_config::CHUNK_RESOLUTION);
        return;
    }

    size_t data_size = static_cast<size_t>(resolution) * resolution;
    std::vector<uint16_t> data(data_size);
    f.read(reinterpret_cast<char*>(data.data()), data_size * sizeof(uint16_t));

    if (!f) {
        std::cerr << "[World] Truncated editor heightmap at " << path << "\n";
        heightmap_init(heightmap_, 0, 0, protocol::heightmap_config::CHUNK_RESOLUTION);
        return;
    }

    // Populate server HeightmapChunk
    heightmap_.chunk_x = 0;
    heightmap_.chunk_z = 0;
    heightmap_.resolution = resolution;
    heightmap_.world_origin_x = origin_x;
    heightmap_.world_origin_z = origin_z;
    heightmap_.world_size = world_size;
    heightmap_.height_data = std::move(data);

    std::cout << "[World] Loaded editor heightmap: " << resolution << "x" << resolution
              << " (" << heightmap_.serialized_size() / 1024 << " KB)\n";
}

bool World::spawn_from_world_data() {
    const std::string path = "data/editor_save/world_entities.json";
    std::ifstream f(path);
    if (!f.is_open()) {
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "[World] Failed to parse " << path << ": " << e.what() << "\n";
        return false;
    }

    if (!j.is_array() || j.empty()) {
        return false;
    }

    int buildings = 0, environment = 0, town_npcs = 0, monsters = 0;

    for (auto& ej : j) {
        std::string entity_type_str = ej.value("entity_type", "environment");
        std::string model = ej.value("model", "");
        EntityType entity_type = config::entity_type_from_string(entity_type_str);

        float px = ej["position"][0].get<float>();
        float py = ej["position"][1].get<float>();
        float pz = ej["position"][2].get<float>();
        float rot = ej.value("rotation", 0.0f);
        float target_size = ej.value("target_size", 30.0f);
        uint32_t color = ej.value("color", (uint32_t)0xFFFFFFFF);
        std::string name = ej.value("name", model);
        bool wanders = ej.value("wanders", false);
        float wander_radius = ej.value("wander_radius", 80.0f);

        // Snap Y to terrain height (editor may have saved stale Y values)
        py = get_terrain_height(px, pz);

        auto entity = registry_.create();
        uint32_t net_id = next_network_id();
        registry_.emplace<ecs::NetworkId>(entity, net_id);
        network_id_to_entity_[net_id] = entity;

        ecs::Transform transform;
        transform.x = px;
        transform.y = py;
        transform.z = pz;
        transform.rotation = rot;
        registry_.emplace<ecs::Transform>(entity, transform);
        registry_.emplace<ecs::Velocity>(entity);

        switch (entity_type) {
            case EntityType::Building: {
                BuildingType bt = config::building_type_from_model(model);

                registry_.emplace<ecs::Health>(entity, 9999.0f, 9999.0f);
                registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);

                ecs::EntityInfo info;
                info.type = EntityType::Building;
                info.building_type = static_cast<uint8_t>(bt);
                info.color = color;
                registry_.emplace<ecs::EntityInfo>(entity, info);

                registry_.emplace<ecs::Name>(entity, name);
                registry_.emplace<ecs::StaticTag>(entity);
                registry_.emplace<ecs::Scale>(entity);

                ecs::Collider collider;
                collider.type = ecs::ColliderType::Box;
                config::get_building_collision_size(bt,
                    collider.half_extents_x, collider.half_extents_y, collider.half_extents_z);
                collider.offset_y = collider.half_extents_y;
                registry_.emplace<ecs::Collider>(entity, collider);

                ecs::RigidBody rb;
                rb.motion_type = ecs::PhysicsMotionType::Static;
                registry_.emplace<ecs::RigidBody>(entity, rb);

                spatial_grid_.update_entity(net_id, px, pz, EntityType::Building);
                buildings++;
                break;
            }
            case EntityType::Environment: {
                EnvironmentType et = config::environment_type_from_model(model);
                float scale = target_size; // editor target_size = per-instance scale

                registry_.emplace<ecs::Health>(entity, 9999.0f, 9999.0f);
                registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);

                ecs::EntityInfo info;
                info.type = EntityType::Environment;
                info.environment_type = static_cast<uint8_t>(et);
                info.color = config::is_tree_type(et) ? 0xFF228822 : 0xFF666666;
                registry_.emplace<ecs::EntityInfo>(entity, info);

                registry_.emplace<ecs::Name>(entity, name);
                registry_.emplace<ecs::StaticTag>(entity);
                registry_.emplace<ecs::Scale>(entity, scale);

                ecs::Collider collider;
                if (config::is_tree_type(et)) {
                    collider.type = ecs::ColliderType::Capsule;
                    collider.radius = config::get_tree_collision_radius(et, scale);
                    collider.half_height = scale * 0.4f;
                    collider.offset_y = collider.half_height + collider.radius;
                } else {
                    collider.type = ecs::ColliderType::Box;
                    config::get_environment_collision_size(et, scale,
                        collider.half_extents_x, collider.half_extents_y, collider.half_extents_z);
                    collider.offset_y = collider.half_extents_y;
                }
                registry_.emplace<ecs::Collider>(entity, collider);

                ecs::RigidBody rb;
                rb.motion_type = ecs::PhysicsMotionType::Static;
                registry_.emplace<ecs::RigidBody>(entity, rb);

                spatial_grid_.update_entity(net_id, px, pz, EntityType::Environment);
                environment++;
                break;
            }
            case EntityType::TownNPC: {
                NPCType nt = config::npc_type_from_model(model);

                registry_.emplace<ecs::Health>(entity, 1000.0f, 1000.0f);
                registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);

                ecs::EntityInfo info;
                info.type = EntityType::TownNPC;
                info.npc_type = static_cast<uint8_t>(nt);
                info.color = color;
                registry_.emplace<ecs::EntityInfo>(entity, info);

                registry_.emplace<ecs::Name>(entity, name);
                registry_.emplace<ecs::Scale>(entity);

                float npc_target_size = config::get_character_target_size(EntityType::TownNPC);
                ecs::Collider collider;
                collider.type = ecs::ColliderType::Capsule;
                collider.radius = config::get_collision_radius(npc_target_size);
                collider.half_height = config::get_collision_half_height(npc_target_size);
                collider.offset_y = collider.half_height + collider.radius;
                registry_.emplace<ecs::Collider>(entity, collider);

                ecs::RigidBody rb;
                rb.motion_type = wanders ? ecs::PhysicsMotionType::Dynamic : ecs::PhysicsMotionType::Static;
                rb.lock_rotation = true;
                rb.mass = 70.0f;
                rb.linear_damping = 0.9f;
                registry_.emplace<ecs::RigidBody>(entity, rb);

                if (wanders) {
                    ecs::TownNPCAI ai;
                    ai.home_x = px;
                    ai.home_z = pz;
                    ai.wander_radius = wander_radius;
                    registry_.emplace<ecs::TownNPCAI>(entity, ai);
                } else {
                    registry_.emplace<ecs::StaticTag>(entity);
                }

                spatial_grid_.update_entity(net_id, px, pz, EntityType::TownNPC);
                town_npcs++;
                break;
            }
            case EntityType::NPC: {
                // Monsters
                registry_.emplace<ecs::Health>(entity, config_->monster().health, config_->monster().health);
                registry_.emplace<ecs::Combat>(entity, config_->monster().damage, config_->monster().attack_range,
                                               config_->monster().attack_cooldown, 0.0f, false);

                ecs::EntityInfo info;
                info.type = EntityType::NPC;
                info.npc_type = static_cast<uint8_t>(NPCType::Monster);
                info.color = config_->monster().color;
                registry_.emplace<ecs::EntityInfo>(entity, info);

                registry_.emplace<ecs::Name>(entity, name);
                registry_.emplace<ecs::NPCTag>(entity);
                registry_.emplace<ecs::AIState>(entity);
                registry_.emplace<ecs::Scale>(entity);
                registry_.emplace<ecs::BuffState>(entity);  // Empty buff state for status effects

                float npc_target_size = config::get_character_target_size(EntityType::NPC);
                ecs::Collider collider;
                collider.type = ecs::ColliderType::Capsule;
                collider.radius = config::get_collision_radius(npc_target_size);
                collider.half_height = config::get_collision_half_height(npc_target_size);
                collider.offset_y = collider.half_height + collider.radius;
                registry_.emplace<ecs::Collider>(entity, collider);

                ecs::RigidBody rb;
                rb.motion_type = ecs::PhysicsMotionType::Dynamic;
                rb.lock_rotation = true;
                rb.mass = 80.0f;
                rb.linear_damping = 0.9f;
                registry_.emplace<ecs::RigidBody>(entity, rb);

                uint32_t net_id = registry_.get<ecs::NetworkId>(entity).id;
                spatial_grid_.update_entity(net_id, px, pz, EntityType::NPC);

                monsters++;
                break;
            }
            default:
                break;
        }
    }

    std::cout << "[World] Loaded " << j.size() << " entities from editor save ("
              << buildings << " buildings, " << environment << " environment, "
              << town_npcs << " town NPCs, " << monsters << " monsters)\n";
    return true;
}

} // namespace mmo::server

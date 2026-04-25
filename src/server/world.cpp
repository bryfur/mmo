#include "world.hpp"
#include "ecs/game_components.hpp"
#include "entity_config.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "log.hpp"
#include "protocol/heightmap.hpp"
#include "protocol/protocol.hpp"
#include "server/game_config.hpp"
#include "server/game_types.hpp"
#include "server/heightmap_generator.hpp"
#include "systems/ai_system.hpp"
#include "systems/buff_system.hpp"
#include "systems/combat_system.hpp"
#include "systems/death_system.hpp"
#include "systems/leveling_system.hpp"
#include "systems/loot_system.hpp"
#include "systems/movement_system.hpp"
#include "systems/physics_system.hpp"
#include "systems/quest_system.hpp"
#include "systems/skill_system.hpp"
#include "systems/talent_passive_system.hpp"
#include "systems/zone_system.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mmo::server {

using namespace mmo::protocol;

World::World(const GameConfig& config)
    : config_(&config),
      zone_system_(config),
      spatial_grid_(config.network().spatial_grid_cell_size),
      rng_(std::random_device{}()) {
    // Cache town center from zone config
    for (const auto& zone : config.zones()) {
        if (zone.id == "town_safe_zone") {
            town_center_ = glm::vec2(zone.center_x, zone.center_z);
            break;
        }
    }

    // Load heightmap from editor save
    load_heightmap();

    physics_.initialize();
    physics_.set_gravity(0.0f, -1500.0f, 0.0f);

    // Bake the heightmap into a Jolt HeightFieldShape so terrain participates
    // in collision queries (raycasts, CharacterVirtual ground detection).
    physics_.build_terrain(heightmap_);

    // Setup collision callbacks
    setup_collision_callbacks();

    // Load entities from editor save
    if (!spawn_from_world_data()) {
        LOG_WARN("World") << "No editor world data found (data/editor_save/world_entities.json). "
                          << "Use the editor to generate and save a world.";
    }

    // Spawn zone-based monsters across the world
    zone_system_.spawn_initial_monsters(
        registry_, [this]() { return next_network_id(); },
        [this](float x, float z) { return get_terrain_height(x, z); });

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
            if (collider.type == ecs::ColliderType::Box) {
                static_boxes++;
            } else if (collider.type == ecs::ColliderType::Capsule) {
                static_capsules++;
            }
        } else {
            if (collider.type == ecs::ColliderType::Capsule) {
                dynamic_capsules++;
            }
        }
    }
    LOG_INFO("Physics") << "Bodies created - static boxes: " << static_boxes << ", static capsules: " << static_capsules
                        << ", dynamic capsules: " << dynamic_capsules;

    // Optimize broadphase now that all static bodies are added
    // This is critical for efficient collision detection with static objects
    physics_.optimize_broadphase();
}

World::~World() {
    physics_.shutdown();
}

void World::setup_collision_callbacks() {
    physics_.set_collision_callback([this](entt::entity a, entt::entity b, const ecs::CollisionEvent& event) {
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

    auto entity = registry_.create();
    uint32_t net_id = next_network_id();

    // Spawn players in the town (safe zone)
    std::uniform_real_distribution<float> dist_offset(-50.0f, 50.0f);
    float spawn_x = town_center_.x + dist_offset(rng_);
    float spawn_z = town_center_.y + dist_offset(rng_);

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
    registry_.emplace<ecs::Scale>(entity);     // Default scale = 1.0
    registry_.emplace<ecs::BuffState>(entity); // Empty buff state for status effects

    // Progression components - use per-class mana from skills.json mana_system
    ecs::PlayerLevel player_level;
    player_level.mana = cls.base_mana;
    player_level.max_mana = cls.base_mana;
    player_level.mana_regen = cls.mana_regen;
    registry_.emplace<ecs::PlayerLevel>(entity, player_level);
    registry_.emplace<ecs::Inventory>(entity);
    registry_.emplace<ecs::Equipment>(entity);
    registry_.emplace<ecs::QuestState>(entity);
    registry_.emplace<ecs::SkillState>(entity);
    registry_.emplace<ecs::TalentState>(entity);
    registry_.emplace<ecs::TalentStats>(entity);
    registry_.emplace<ecs::TalentRuntimeState>(entity);

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
    rb.mass = 70.0f;          // ~70kg for a character
    rb.linear_damping = 0.9f; // High damping for responsive control
    registry_.emplace<ecs::RigidBody>(entity, rb);

    // Create physics body immediately so the player has collision from the first tick.
    // Use the per-entity helper to skip the full-registry rescan that
    // create_bodies() would otherwise do on every join.
    physics_.create_body_for_entity(registry_, entity);

    // Add to spatial grid
    spatial_grid_.update_entity(net_id, spawn_x, spawn_z, EntityType::Player);

    // Add to fast lookup map
    network_id_to_entity_[net_id] = entity;

    return net_id;
}

void World::remove_player(uint32_t player_id) {

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

    auto entity = find_entity_by_network_id(player_id);
    if (entity == entt::null || !registry_.all_of<ecs::InputState>(entity)) {
        return;
    }

    // Sanitize: clamp move_dir magnitude to <=1, normalize attack_dir to unit length.
    // Untrusted client input — never propagate raw values into the simulation.
    PlayerInput clean = input;
    auto sanitize_dir = [](float& x, float& y, bool unit) {
        if (!std::isfinite(x) || !std::isfinite(y)) {
            x = 0.0f;
            y = unit ? 1.0f : 0.0f;
            return;
        }
        float len_sq = x * x + y * y;
        if (unit) {
            if (len_sq < 1e-6f) {
                x = 0.0f;
                y = 1.0f;
                return;
            }
            float inv = 1.0f / std::sqrt(len_sq);
            x *= inv;
            y *= inv;
        } else if (len_sq > 1.0f) {
            float inv = 1.0f / std::sqrt(len_sq);
            x *= inv;
            y *= inv;
        }
    };
    sanitize_dir(clean.move_dir_x, clean.move_dir_y, false);
    sanitize_dir(clean.attack_dir_x, clean.attack_dir_y, true);

    auto& state = registry_.get<ecs::InputState>(entity);
    // Latch attacking flag so it persists until the combat system consumes it.
    bool was_attacking = state.input.attacking;
    state.input = clean;
    if (was_attacking && !clean.attacking) {
        state.input.attacking = true;
    }

    if (!registry_.all_of<ecs::AttackDirection>(entity)) {
        registry_.emplace<ecs::AttackDirection>(entity);
    }
    auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
    attack_dir.x = clean.attack_dir_x;
    attack_dir.y = clean.attack_dir_y;
}

void World::update(float dt) {

    // Update game systems
    systems::update_movement(registry_, dt, *config_);
    systems::update_ai(registry_, dt, *config_, town_center_);
    auto combat_hits = systems::update_combat(registry_, dt, *config_);

    // Generate CombatEvent / EntityDeath events from combat hits.
    // Emit ONCE per hit (player_id=0 = "broadcast"); the server filters by
    // AOI when actually sending to clients. Avoids O(hits * players) fan-out.
    auto emit_combat_pair = [this](uint32_t attacker_net_id, uint32_t target_net_id, float damage, bool died) {
        pending_events_.emplace_back(events::CombatHitEvent{attacker_net_id, target_net_id, damage});
        if (died) {
            pending_events_.emplace_back(events::EntityDeathEvent{target_net_id, attacker_net_id});
        }
    };
    auto net_id_or_zero = [this](entt::entity e) -> uint32_t {
        auto* n = registry_.try_get<ecs::NetworkId>(e);
        return n ? n->id : 0;
    };

    for (const auto& hit : combat_hits) {
        emit_combat_pair(net_id_or_zero(hit.attacker), net_id_or_zero(hit.target), hit.damage, hit.target_died);
    }

    // Update progression systems
    systems::update_mana_regen(registry_, dt);
    systems::update_skill_cooldowns(registry_, dt);

    // Tick active channeled skills (whirlwind, arcane_rain, consecrate, ...).
    {
        auto channel_hits = systems::update_channeled_skills(registry_, dt, *config_);
        for (const auto& hit : channel_hits) {
            emit_combat_pair(net_id_or_zero(hit.attacker), net_id_or_zero(hit.target), hit.damage, hit.target_died);
        }
    }

    // Update quests and generate progress/complete events
    auto quest_changes = systems::update_quests(registry_, dt, *config_);
    for (auto& [entity, change] : quest_changes) {
        auto* net_id = registry_.try_get<ecs::NetworkId>(entity);
        if (!net_id) {
            continue;
        }

        if (change.quest_complete) {
            pending_events_.emplace_back(events::QuestComplete{net_id->id, change.quest_id, change.quest_name});
        } else {
            pending_events_.emplace_back(events::QuestProgress{net_id->id, change.quest_id, change.objective_index,
                                                               change.current, change.required,
                                                               change.objective_complete});
        }
    }

    // Handle dead monsters - award XP/loot, talent kill effects, respawn
    systems::handle_monster_deaths(registry_, *config_, pending_events_, spatial_grid_, zone_system_, physics_, rng_,
                                   [this](float x, float z) { return get_terrain_height(x, z); });

    // Handle player deaths - death penalty, reset, teleport to town
    systems::handle_player_deaths(registry_, *config_, town_center_, physics_, rng_,
                                  [this](float x, float z) { return get_terrain_height(x, z); });

    // Update buff/debuff system (tick durations, apply DoT/HoT, remove expired)
    systems::update_buffs(registry_, dt);

    // Update talent passive effects (regen, auras, fury, cooldown timers, etc.)
    systems::update_talent_passives(registry_, dt, *config_);

    // Update spatial grid - only for entities that can move (have Velocity)
    // Static entities (buildings, environment) are added once at spawn and never change cell
    auto view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::EntityInfo, ecs::Velocity>();
    for (auto entity : view) {
        auto&& [net_id, transform, info, vel] = view.get(entity);
        if (vel.x == 0.0f && vel.z == 0.0f) {
            continue;
        }
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
                pending_events_.emplace_back(events::ZoneChange{net_id.id, zone_name});
            }
        }
    }
}

void World::update_physics_step(float fixed_dt) {
    physics_.update(registry_, fixed_dt);
}

NetEntityState World::build_entity_state(entt::entity entity, bool include_render_static) const {
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

    // Aggregate active status effects into a compact bitmask so the client
    // can render buff icons / tints. See NetEntityState::EffectBit.
    if (const auto* buffs = registry_.try_get<ecs::BuffState>(entity)) {
        uint16_t m = 0;
        for (const auto& e : buffs->effects) {
            if (e.duration <= 0.0f) {
                continue;
            }
            using T = ecs::StatusEffect::Type;
            switch (e.type) {
                case T::Stun:
                case T::Freeze:
                    m |= NetEntityState::EffectStun;
                    break;
                case T::Slow:
                    m |= NetEntityState::EffectSlow;
                    break;
                case T::Root:
                    m |= NetEntityState::EffectRoot;
                    break;
                case T::Burn:
                case T::Poison:
                    m |= NetEntityState::EffectBurn;
                    break;
                case T::Shield:
                    m |= NetEntityState::EffectShield;
                    break;
                case T::DamageBoost:
                    m |= NetEntityState::EffectDamageBoost;
                    break;
                case T::SpeedBoost:
                    m |= NetEntityState::EffectSpeedBoost;
                    break;
                case T::Invulnerable:
                    m |= NetEntityState::EffectInvuln;
                    break;
                case T::DefenseBoost:
                    m |= NetEntityState::EffectDefBoost;
                    break;
                default:
                    break;
            }
        }
        state.effects_mask = m;
    }

    std::strncpy(state.name, name.value.c_str(), 31);
    state.name[31] = '\0';

    // Static render fields are only needed by EntityEnter; per-tick deltas
    // never transmit them, so skip the lookup on the hot path.
    if (include_render_static) {
        populate_render_data(state, info, combat);
    }

    return state;
}

NetEntityState World::get_entity_state(uint32_t network_id) const {
    auto entity = find_entity_by_network_id(network_id);
    if (entity == entt::null) {
        return NetEntityState{};
    }
    return build_entity_state(entity, /*include_render_static=*/false);
}

NetEntityState World::get_entity_state_full(uint32_t network_id) const {
    auto entity = find_entity_by_network_id(network_id);
    if (entity == entt::null) {
        return NetEntityState{};
    }
    return build_entity_state(entity, /*include_render_static=*/true);
}

std::vector<NetEntityState> World::get_all_entities() const {
    std::vector<NetEntityState> result;

    auto view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Velocity, ecs::Health, ecs::Combat, ecs::EntityInfo,
                               ecs::Name>();

    for (auto entity : view) {
        result.push_back(build_entity_state(entity, /*include_render_static=*/true));
    }

    return result;
}

std::vector<World::GameplayEvent> World::take_events() {
    return std::move(pending_events_);
}

void World::add_combat_hits(const std::vector<systems::CombatHit>& hits) {
    auto net_id_or_zero = [this](entt::entity e) -> uint32_t {
        auto* n = registry_.try_get<ecs::NetworkId>(e);
        return n ? n->id : 0;
    };
    for (const auto& hit : hits) {
        uint32_t a = net_id_or_zero(hit.attacker);
        uint32_t t = net_id_or_zero(hit.target);
        pending_events_.emplace_back(events::CombatHitEvent{a, t, hit.damage});
        if (hit.target_died) {
            pending_events_.emplace_back(events::EntityDeathEvent{t, a});
        }
    }
}

entt::entity World::find_entity_by_network_id(uint32_t id) const {
    auto it = network_id_to_entity_.find(id);
    if (it != network_id_to_entity_.end()) {
        return it->second;
    }
    return entt::null;
}

size_t World::player_count() const {
    return registry_.view<ecs::PlayerTag>().size();
}

size_t World::npc_count() const {
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
            strncpy_safe(state.model_name,
                         config::get_building_model_name(static_cast<BuildingType>(info.building_type)), 32);
            state.target_size = config::get_building_target_size(static_cast<BuildingType>(info.building_type));
            break;
        case EntityType::Environment: {
            auto env_type = static_cast<EnvironmentType>(info.environment_type);
            std::string model_str = config::get_environment_model_name(env_type, info.tree_variant);
            strncpy_safe(state.model_name, model_str.c_str(), 32);
            state.target_size = config::get_environment_target_scale(env_type);
            break;
        }
    }
}

uint32_t World::generate_color(PlayerClass player_class) {
    return config_->get_class(static_cast<int>(player_class)).color;
}

void World::load_heightmap() {
    const std::string path = "data/editor_save/heightmap.bin";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        LOG_WARN("World") << "No heightmap found at " << path << ". Use the editor to generate and save a world.";
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
        LOG_ERROR("World") << "Invalid editor heightmap at " << path;
        heightmap_init(heightmap_, 0, 0, protocol::heightmap_config::CHUNK_RESOLUTION);
        return;
    }

    size_t data_size = static_cast<size_t>(resolution) * resolution;
    std::vector<uint16_t> data(data_size);
    f.read(reinterpret_cast<char*>(data.data()), data_size * sizeof(uint16_t));

    if (!f) {
        LOG_ERROR("World") << "Truncated editor heightmap at " << path;
        heightmap_init(heightmap_, 0, 0, protocol::heightmap_config::CHUNK_RESOLUTION);
        return;
    }

    // Re-encode height data if the file's min/max range differs from the protocol constants.
    // The editor may save with a different height range; we need to normalize to the
    // protocol's standard range so heightmap_get_local/heightmap_get_world decode correctly.
    const float proto_min = protocol::heightmap_config::MIN_HEIGHT;
    const float proto_max = protocol::heightmap_config::MAX_HEIGHT;
    if (std::abs(min_h - proto_min) > 0.01f || std::abs(max_h - proto_max) > 0.01f) {
        const float file_range = max_h - min_h;
        const float proto_range = proto_max - proto_min;
        if (file_range > 0.01f) {
            for (size_t i = 0; i < data_size; ++i) {
                // Decode from file range, re-encode to protocol range
                float height = (data[i] / 65535.0f) * file_range + min_h;
                float normalized = (height - proto_min) / proto_range;
                normalized = std::fmax(0.0f, std::fmin(1.0f, normalized));
                data[i] = static_cast<uint16_t>(normalized * 65535.0f);
            }
            LOG_INFO("World") << "Re-encoded heightmap from range [" << min_h << ", " << max_h
                              << "] to protocol range [" << proto_min << ", " << proto_max << "]";
        } else {
            // Range header disagrees with proto but is degenerate — raw bytes
            // are likely garbage. Surface this loudly instead of silently
            // proceeding with whatever happens to be in the buffer.
            LOG_ERROR("World") << "Heightmap header range [" << min_h << ", " << max_h << "] differs from proto ["
                               << proto_min << ", " << proto_max << "] but file_range=" << file_range
                               << " is too small to re-encode; raw heights will be used as-is.";
        }
    }

    // Populate server HeightmapChunk
    heightmap_.chunk_x = 0;
    heightmap_.chunk_z = 0;
    heightmap_.resolution = resolution;
    heightmap_.world_origin_x = origin_x;
    heightmap_.world_origin_z = origin_z;
    heightmap_.world_size = world_size;
    heightmap_.height_data = std::move(data);

    LOG_INFO("World") << "Loaded editor heightmap: " << resolution << "x" << resolution << " ("
                      << heightmap_.serialized_size() / 1024 << " KB)";
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
        LOG_ERROR("World") << "Failed to parse " << path << ": " << e.what();
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
                config::get_building_collision_size(bt, collider.half_extents_x, collider.half_extents_y,
                                                    collider.half_extents_z);
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
                float scale = target_size;

                // For trees from the editor, randomly diversify the tree type
                // so forests have a natural mix of species
                if (config::is_tree_type(et)) {
                    static const EnvironmentType tree_types[] = {
                        EnvironmentType::TreeOak,   EnvironmentType::TreePine,  EnvironmentType::TreeWillow,
                        EnvironmentType::TreeBirch, EnvironmentType::TreeMaple, EnvironmentType::TreeAspen,
                    };
                    // Use position as seed for deterministic but varied assignment
                    uint32_t hash = static_cast<uint32_t>(px * 73.0f + pz * 137.0f);
                    // 50% chance to keep original type, 50% chance to diversify
                    if ((hash % 2) == 0 && et != EnvironmentType::TreeDead) {
                        et = tree_types[hash % 6];
                    }
                }

                registry_.emplace<ecs::Health>(entity, 9999.0f, 9999.0f);
                registry_.emplace<ecs::Combat>(entity, 0.0f, 0.0f, 0.0f, 0.0f, false);

                // Pick a random visual variant for this tree
                uint8_t variant = 0;
                if (config::is_tree_type(et)) {
                    uint32_t vseed = static_cast<uint32_t>(px * 31.0f + pz * 97.0f);
                    variant = static_cast<uint8_t>(vseed % config::TREE_VARIANTS);
                }

                ecs::EntityInfo info;
                info.type = EntityType::Environment;
                info.environment_type = static_cast<uint8_t>(et);
                info.tree_variant = variant;
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
                    config::get_environment_collision_size(et, scale, collider.half_extents_x, collider.half_extents_y,
                                                           collider.half_extents_z);
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
                registry_.emplace<ecs::BuffState>(entity); // Empty buff state for status effects

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

    LOG_INFO("World") << "Loaded " << j.size() << " entities from editor save (" << buildings << " buildings, "
                      << environment << " environment, " << town_npcs << " town NPCs, " << monsters << " monsters)";
    return true;
}

} // namespace mmo::server

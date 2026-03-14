#include "zone_system.hpp"
#include "server/game_types.hpp"
#include <cmath>
#include <functional>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mmo::server::systems {

ZoneSystem::ZoneSystem(const GameConfig& config)
    : config_(&config)
    , rng_(std::random_device{}())
{
    for (const auto& zone : config_->zones()) {
        if (zone.monster_density <= 0.0f) continue;
        if (zone.monster_types.empty()) continue;

        float area = zone.radius * zone.radius * static_cast<float>(M_PI);
        int target = static_cast<int>(area * zone.monster_density / (1000.0f * 1000.0f));
        target = std::clamp(target, 2, 50);

        zone_spawns_.push_back({&zone, target});
        total_target_ += target;
    }
}

void ZoneSystem::spawn_initial_monsters(entt::registry& registry,
                                         std::function<uint32_t()> next_id_fn,
                                         std::function<float(float, float)> height_fn)
{
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * static_cast<float>(M_PI));
    std::uniform_real_distribution<float> unit_dist(0.0f, 1.0f);

    for (const auto& spawn : zone_spawns_) {
        const auto& zone = *spawn.zone;
        std::uniform_int_distribution<int> type_dist(0, static_cast<int>(zone.monster_types.size()) - 1);

        for (int i = 0; i < spawn.target_count; ++i) {
            // Pick random position within zone circle
            float angle = angle_dist(rng_);
            float r = zone.radius * std::sqrt(unit_dist(rng_));
            float x = zone.center_x + r * std::cos(angle);
            float z = zone.center_z + r * std::sin(angle);
            float y = height_fn(x, z);

            // Pick random monster type from zone's list
            const std::string& type_id = zone.monster_types[type_dist(rng_)];
            const MonsterTypeConfig* mt = config_->find_monster_type(type_id);
            if (!mt) continue;

            auto entity = registry.create();

            registry.emplace<ecs::NetworkId>(entity, next_id_fn());
            registry.emplace<ecs::Transform>(entity, x, y, z, angle_dist(rng_));
            registry.emplace<ecs::Velocity>(entity, 0.0f, 0.0f, 0.0f);
            registry.emplace<ecs::Health>(entity, mt->health, mt->health);
            registry.emplace<ecs::Combat>(entity, mt->damage, mt->attack_range, mt->attack_cooldown, 0.0f, false);
            registry.emplace<ecs::Name>(entity, ecs::Name{mt->name});

            ecs::EntityInfo info{};
            info.type = mmo::protocol::EntityType::NPC;
            info.npc_type = static_cast<uint8_t>(NPCType::Monster);
            info.color = mt->color;
            info.model_name = mt->model;
            info.target_size = mt->size;
            registry.emplace<ecs::EntityInfo>(entity, info);

            registry.emplace<ecs::NPCTag>(entity);
            registry.emplace<ecs::AIState>(entity, ecs::AIState{0, mt->aggro_range});
            registry.emplace<ecs::Scale>(entity, ecs::Scale{1.0f});
            registry.emplace<ecs::MonsterTypeId>(entity, ecs::MonsterTypeId{mt->id, mt->level, mt->xp_reward, mt->gold_reward});
            registry.emplace<ecs::ZoneInfo>(entity, ecs::ZoneInfo{zone.id});

            // Physics components
            float capsule_radius = mt->size * 0.35f;
            float capsule_half_height = mt->size * 0.4f;
            float offset_y = capsule_half_height + capsule_radius;

            ecs::Collider collider{};
            collider.type = ecs::ColliderType::Capsule;
            collider.radius = capsule_radius;
            collider.half_height = capsule_half_height;
            collider.offset_y = offset_y;
            collider.is_trigger = false;
            registry.emplace<ecs::Collider>(entity, collider);

            ecs::RigidBody rb{};
            rb.motion_type = ecs::PhysicsMotionType::Dynamic;
            rb.mass = 80.0f;
            rb.lock_rotation = true;
            rb.linear_damping = 0.9f;
            registry.emplace<ecs::RigidBody>(entity, rb);
        }
    }
}

void ZoneSystem::respawn_monster(entt::registry& registry, entt::entity monster,
                                  std::function<float(float, float)> height_fn)
{
    auto* zone_info = registry.try_get<ecs::ZoneInfo>(monster);
    if (!zone_info) return;

    // Find the zone config
    const ZoneConfig* zone = nullptr;
    for (const auto& z : config_->zones()) {
        if (z.id == zone_info->zone_id) {
            zone = &z;
            break;
        }
    }
    if (!zone || zone->monster_types.empty()) return;

    // Pick random monster type from zone
    std::uniform_int_distribution<int> type_dist(0, static_cast<int>(zone->monster_types.size()) - 1);
    const std::string& type_id = zone->monster_types[type_dist(rng_)];
    const MonsterTypeConfig* mt = config_->find_monster_type(type_id);
    if (!mt) return;

    // Pick random position in zone
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * static_cast<float>(M_PI));
    std::uniform_real_distribution<float> unit_dist(0.0f, 1.0f);
    float angle = angle_dist(rng_);
    float r = zone->radius * std::sqrt(unit_dist(rng_));
    float x = zone->center_x + r * std::cos(angle);
    float z_pos = zone->center_z + r * std::sin(angle);
    float y = height_fn(x, z_pos);

    // Reset transform
    auto& transform = registry.get<ecs::Transform>(monster);
    transform.x = x;
    transform.y = y;
    transform.z = z_pos;
    transform.rotation = angle_dist(rng_);

    // Reset velocity
    auto& vel = registry.get<ecs::Velocity>(monster);
    vel.x = 0.0f;
    vel.y = 0.0f;
    vel.z = 0.0f;

    // Reset health
    auto& health = registry.get<ecs::Health>(monster);
    health.current = mt->health;
    health.max = mt->health;

    // Reset combat
    auto& combat = registry.get<ecs::Combat>(monster);
    combat.damage = mt->damage;
    combat.attack_range = mt->attack_range;
    combat.attack_cooldown = mt->attack_cooldown;
    combat.current_cooldown = 0.0f;
    combat.is_attacking = false;

    // Update name and entity info
    registry.get<ecs::Name>(monster).value = mt->name;

    auto& info = registry.get<ecs::EntityInfo>(monster);
    info.color = mt->color;
    info.model_name = mt->model;
    info.target_size = mt->size;

    // Update AI
    auto& ai = registry.get<ecs::AIState>(monster);
    ai.target_id = 0;
    ai.aggro_range = mt->aggro_range;

    // Update monster type id
    auto& type_tag = registry.get<ecs::MonsterTypeId>(monster);
    type_tag.type_id = mt->id;
    type_tag.level = mt->level;
    type_tag.xp_reward = mt->xp_reward;
    type_tag.gold_reward = mt->gold_reward;

    // Update physics collider
    float capsule_radius = mt->size * 0.35f;
    float capsule_half_height = mt->size * 0.4f;
    float offset_y = capsule_half_height + capsule_radius;

    auto& collider = registry.get<ecs::Collider>(monster);
    collider.radius = capsule_radius;
    collider.half_height = capsule_half_height;
    collider.offset_y = offset_y;

    // Teleport physics body if present
    auto* physics_body = registry.try_get<ecs::PhysicsBody>(monster);
    if (physics_body) {
        physics_body->needs_teleport = true;
    }
}

std::string ZoneSystem::get_zone_name(float x, float z) const {
    const ZoneConfig* zone = config_->find_zone_at(x, z);
    if (zone) return zone->name;
    return "Wilderness";
}

} // namespace mmo::server::systems

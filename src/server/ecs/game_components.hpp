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

// ============================================================================
// Progression Components
// ============================================================================

struct PlayerLevel {
    int level = 1;
    int xp = 0;
    int gold = 0;

    // Mana for skills
    float mana = 100.0f;
    float max_mana = 100.0f;
    float mana_regen = 5.0f;  // per second
};

// ============================================================================
// Inventory Components
// ============================================================================

struct InventoryItem {
    std::string item_id;
    int count = 1;
};

struct Inventory {
    static constexpr int MAX_SLOTS = 20;
    InventoryItem slots[MAX_SLOTS] = {};
    int used_slots = 0;

    bool add_item(const std::string& id, int count = 1) {
        // Stack with existing
        for (int i = 0; i < used_slots; ++i) {
            if (slots[i].item_id == id) {
                slots[i].count += count;
                return true;
            }
        }
        // New slot
        if (used_slots < MAX_SLOTS) {
            slots[used_slots].item_id = id;
            slots[used_slots].count = count;
            ++used_slots;
            return true;
        }
        return false; // Full
    }

    bool remove_item(const std::string& id, int count = 1) {
        for (int i = 0; i < used_slots; ++i) {
            if (slots[i].item_id == id && slots[i].count >= count) {
                slots[i].count -= count;
                if (slots[i].count <= 0) {
                    // Compact
                    for (int j = i; j < used_slots - 1; ++j)
                        slots[j] = slots[j + 1];
                    slots[used_slots - 1] = {};
                    --used_slots;
                }
                return true;
            }
        }
        return false;
    }

    int count_item(const std::string& id) const {
        for (int i = 0; i < used_slots; ++i)
            if (slots[i].item_id == id) return slots[i].count;
        return 0;
    }
};

// Equipped items (one per slot)
struct Equipment {
    std::string weapon_id;
    std::string armor_id;

    // Computed bonuses from equipment
    float damage_bonus = 0.0f;
    float health_bonus = 0.0f;
    float speed_bonus = 0.0f;
    float defense = 0.0f;
};

// ============================================================================
// Quest Components
// ============================================================================

struct QuestObjectiveProgress {
    std::string type;       // "kill", "visit", "gather" - copied from config
    std::string target;     // target id - copied from config
    int current = 0;
    int required = 0;
    bool complete = false;
};

struct ActiveQuest {
    std::string quest_id;
    std::vector<QuestObjectiveProgress> objectives;
    bool all_complete = false;
};

struct QuestState {
    std::vector<ActiveQuest> active_quests;
    std::vector<std::string> completed_quests;

    bool has_completed(const std::string& id) const {
        for (const auto& q : completed_quests)
            if (q == id) return true;
        return false;
    }

    bool has_active(const std::string& id) const {
        for (const auto& q : active_quests)
            if (q.quest_id == id) return true;
        return false;
    }

    ActiveQuest* get_active(const std::string& id) {
        for (auto& q : active_quests)
            if (q.quest_id == id) return &q;
        return nullptr;
    }
};

// ============================================================================
// Skill Components
// ============================================================================

struct SkillCooldown {
    std::string skill_id;
    float remaining = 0.0f;
};

struct SkillState {
    std::vector<SkillCooldown> cooldowns;

    float get_cooldown(const std::string& id) const {
        for (const auto& cd : cooldowns)
            if (cd.skill_id == id) return cd.remaining;
        return 0.0f;
    }

    void set_cooldown(const std::string& id, float time) {
        for (auto& cd : cooldowns) {
            if (cd.skill_id == id) { cd.remaining = time; return; }
        }
        cooldowns.push_back({id, time});
    }

    void update(float dt) {
        for (auto& cd : cooldowns)
            if (cd.remaining > 0.0f) cd.remaining -= dt;
    }
};

// ============================================================================
// Talent Components
// ============================================================================

struct TalentState {
    std::vector<std::string> unlocked_talents;
    int talent_points = 0;

    bool has_talent(const std::string& id) const {
        for (const auto& t : unlocked_talents)
            if (t == id) return true;
        return false;
    }
};

// ============================================================================
// Monster Type Tag (links to monster_types.json by id)
// ============================================================================

struct MonsterTypeId {
    std::string type_id;  // e.g. "goblin_scout"
    int level = 1;
    int xp_reward = 25;
    int gold_reward = 5;
};

// ============================================================================
// Zone Tag (which zone an entity is in)
// ============================================================================

struct ZoneInfo {
    std::string zone_id;
};

// ============================================================================
// Buff/Debuff Components
// ============================================================================

struct StatusEffect {
    enum class Type : uint8_t {
        Stun,        // Cannot move or attack
        Slow,        // Reduced movement speed
        Root,        // Cannot move but can attack
        Freeze,      // Cannot move or attack (ice visual)
        Burn,        // Damage over time
        Poison,      // Damage over time (weaker but longer)
        Heal,        // Heal over time
        Shield,      // Damage absorption
        SpeedBoost,  // Increased movement speed
        DamageBoost, // Increased damage
        DefenseBoost,// Reduced damage taken
        Lifesteal,   // Heal on hit
        Invulnerable // Cannot take damage
    };

    Type type;
    float duration;      // seconds remaining
    float tick_timer;    // for DoT/HoT effects
    float tick_interval; // how often DoT/HoT ticks
    float value;         // effect magnitude (damage per tick, slow %, etc.)
    uint32_t source_id;  // who applied this
};

struct BuffState {
    std::vector<StatusEffect> effects;

    void add(StatusEffect effect) { effects.push_back(effect); }

    bool has(StatusEffect::Type type) const {
        for (auto& e : effects) if (e.type == type) return true;
        return false;
    }

    bool is_stunned() const { return has(StatusEffect::Type::Stun) || has(StatusEffect::Type::Freeze); }
    bool is_rooted() const { return has(StatusEffect::Type::Root) || is_stunned(); }

    float get_speed_multiplier() const {
        float mult = 1.0f;
        for (auto& e : effects) {
            if (e.type == StatusEffect::Type::Slow) mult *= (1.0f - e.value);
            if (e.type == StatusEffect::Type::SpeedBoost) mult *= (1.0f + e.value);
        }
        return mult;
    }

    float get_damage_multiplier() const {
        float mult = 1.0f;
        for (auto& e : effects) {
            if (e.type == StatusEffect::Type::DamageBoost) mult *= (1.0f + e.value);
        }
        return mult;
    }

    float get_defense_multiplier() const {
        float mult = 1.0f;
        for (auto& e : effects) {
            if (e.type == StatusEffect::Type::DefenseBoost) mult *= (1.0f - e.value);
        }
        return mult;
    }

    bool is_invulnerable() const { return has(StatusEffect::Type::Invulnerable); }

    float get_lifesteal() const {
        float total = 0.0f;
        for (auto& e : effects) {
            if (e.type == StatusEffect::Type::Lifesteal) total += e.value;
        }
        return total;
    }

    float get_shield_value() const {
        float total = 0.0f;
        for (auto& e : effects) {
            if (e.type == StatusEffect::Type::Shield) total += e.value;
        }
        return total;
    }
};

} // namespace mmo::server::ecs

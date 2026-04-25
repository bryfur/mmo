#pragma once

#include "protocol/protocol.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace mmo::server::ecs {

// ============================================================================
// Core Components
// ============================================================================

// Coordinate system: Y-up. x,z form the horizontal ground plane; y is vertical (height/elevation)
struct Transform {
    float x = 0.0f;
    float y = 0.0f; // Height/elevation
    float z = 0.0f;
    float rotation = 0.0f; // Rotation in radians (around vertical axis)
};

struct Velocity {
    float x = 0.0f;
    float y = 0.0f; // Vertical velocity
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
    uint8_t tree_variant = 0; // visual variant index for tree diversity
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

// ============================================================================
// State tags. Empty-struct markers so systems can query exactly the entities
// in a given state and drop per-entity branching. Use registry.emplace<T>(e)
// to enter the state and registry.remove<T>(e) to leave it.
//
// These are introduced as primitives now; existing systems still poll component
// data directly. New systems should prefer tag-driven views to avoid scanning
// every entity for a state predicate. See followup tasks for migration.
// ============================================================================

/// Health <= 0 and pending cleanup (loot drop, respawn, etc.).
struct Dead {};

/// In active combat: aggro pulled or attacked within recent timeout.
struct InCombat {};

/// Currently casting / channeling a skill (paired with ChannelingSkill).
struct Casting {};

/// Has nonzero velocity this tick. Set by movement; queried by AOI/animation
/// systems that only care about moving entities.
struct Moving {};

/// Crowd-control immobilized (stunned/frozen/rooted). Cheaper than scanning
/// BuffState every tick.
struct Immobilized {};

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
    Static = 0,    // Never moves (buildings, terrain)
    Kinematic = 1, // Moved by code, affects dynamic bodies
    Dynamic = 2,   // Fully simulated
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
    float mana_regen = 5.0f; // per second
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

    bool add_item(const std::string& id, int count = 1, int max_stack = 99) {
        // Stack with existing (respect max stack size)
        for (int i = 0; i < used_slots; ++i) {
            if (slots[i].item_id == id) {
                int can_add = max_stack - slots[i].count;
                if (can_add <= 0) {
                    {
                        continue; // This stack is full, try another or new slot
                    }
                }
                int to_add = (count <= can_add) ? count : can_add;
                slots[i].count += to_add;
                count -= to_add;
                if (count <= 0) {
                    {
                        return true;
                    }
                }
            }
        }
        // Remaining items go into new slots
        while (count > 0 && used_slots < MAX_SLOTS) {
            int to_add = (count <= max_stack) ? count : max_stack;
            slots[used_slots].item_id = id;
            slots[used_slots].count = to_add;
            ++used_slots;
            count -= to_add;
        }
        return count <= 0;
    }

    // Removes `count` of `id`, consuming across multiple stacks if needed.
    // Returns false if the inventory doesn't contain enough.
    bool remove_item(const std::string& id, int count = 1) {
        if (count_item(id) < count) {
            {
                return false;
            }
        }
        for (int i = 0; i < used_slots && count > 0;) {
            if (slots[i].item_id == id) {
                int take = (slots[i].count <= count) ? slots[i].count : count;
                slots[i].count -= take;
                count -= take;
                if (slots[i].count <= 0) {
                    // Compact: shift subsequent slots left.
                    for (int j = i; j < used_slots - 1; ++j) slots[j] = slots[j + 1];
                    slots[used_slots - 1] = {};
                    --used_slots;
                    continue; // don't advance i; new occupant is at this index
                }
            }
            ++i;
        }
        return true;
    }

    // Total count of an item across ALL stacks in the inventory.
    int count_item(const std::string& id) const {
        int total = 0;
        for (int i = 0; i < used_slots; ++i) {
            {
                if (slots[i].item_id == id) {
                    {
                        total += slots[i].count;
                    }
                }
            }
        }
        return total;
    }
};

// Equipped items (one per slot)
// Active channeled skill being ticked on the caster. Re-runs the skill's
// damage pass every `tick_interval` seconds until `time_remaining` expires.
struct ChannelingSkill {
    std::string skill_id;
    float time_remaining = 0.0f;
    float tick_timer = 0.0f; // counts down; fires when <= 0
    float tick_interval = 0.5f;
    float dir_x = 1.0f;
    float dir_z = 0.0f;
};

// Tracks per-item consumable cooldowns. Prevents potion spam.
struct ConsumableCooldowns {
    // Remaining cooldown seconds keyed by item_id.
    // We use a small fixed array to avoid a std::unordered_map allocation
    // on every frame. Up to 8 simultaneously-cooling-down items.
    struct Entry {
        std::string item_id;
        float remaining = 0.0f;
    };
    static constexpr int MAX = 8;
    Entry entries[MAX];

    void tick(float dt) {
        for (auto& e : entries) {
            {
                if (e.remaining > 0.0f) {
                    {
                        e.remaining -= dt;
                    }
                }
            }
        }
    }
    float remaining(const std::string& id) const {
        for (const auto& e : entries) {
            {
                if (e.item_id == id && e.remaining > 0.0f) {
                    {
                        return e.remaining;
                    }
                }
            }
        }
        return 0.0f;
    }
    void set(const std::string& id, float cooldown) {
        for (auto& e : entries) {
            if (e.item_id == id) {
                e.remaining = cooldown;
                return;
            }
        }
        // Otherwise, take the first empty/expired slot.
        for (auto& e : entries) {
            if (e.remaining <= 0.0f) {
                e.item_id = id;
                e.remaining = cooldown;
                return;
            }
        }
    }
};

struct Equipment {
    std::string weapon_id;
    std::string armor_id;

    // Computed bonuses from equipment
    float damage_bonus = 0.0f;
    float health_bonus = 0.0f;
    float speed_bonus = 0.0f;
    float defense = 0.0f;

    // Bookkeeping: health_bonus currently baked into Health.max. Tracked so
    // recalc_equipment can compute the delta when gear changes.
    float applied_health_bonus = 0.0f;
};

// ============================================================================
// Quest Components
// ============================================================================

struct QuestObjectiveProgress {
    std::string type;   // "kill", "visit", "gather" - copied from config
    std::string target; // target id - copied from config
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
        for (const auto& q : completed_quests) {
            {
                if (q == id) {
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool has_active(const std::string& id) const {
        for (const auto& q : active_quests) {
            {
                if (q.quest_id == id) {
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    ActiveQuest* get_active(const std::string& id) {
        for (auto& q : active_quests) {
            {
                if (q.quest_id == id) {
                    {
                        return &q;
                    }
                }
            }
        }
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
        for (const auto& cd : cooldowns) {
            {
                if (cd.skill_id == id) {
                    {
                        return cd.remaining;
                    }
                }
            }
        }
        return 0.0f;
    }

    void set_cooldown(const std::string& id, float time) {
        for (auto& cd : cooldowns) {
            if (cd.skill_id == id) {
                cd.remaining = time;
                return;
            }
        }
        cooldowns.push_back({id, time});
    }

    /// Decrement all cooldown timers. Satisfies TickableState.
    void tick(float dt) {
        for (auto& cd : cooldowns) {
            {
                if (cd.remaining > 0.0f) {
                    {
                        cd.remaining -= dt;
                    }
                }
            }
        }
    }
};
// Contract assertion lives in component_contracts.hpp (header-include
// cycle prevented).

// ============================================================================
// Talent Components
// ============================================================================

struct TalentState {
    std::vector<std::string> unlocked_talents;
    int talent_points = 0;

    bool has_talent(const std::string& id) const {
        for (const auto& t : unlocked_talents) {
            {
                if (t == id) {
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }
};

// ============================================================================
// Monster Type Tag (links to monster_types.json by id)
// ============================================================================

struct MonsterTypeId {
    std::string type_id; // e.g. "goblin_scout"
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
        Stun,         // Cannot move or attack
        Slow,         // Reduced movement speed
        Root,         // Cannot move but can attack
        Freeze,       // Cannot move or attack (ice visual)
        Burn,         // Damage over time
        Poison,       // Damage over time (weaker but longer)
        Heal,         // Heal over time
        Shield,       // Damage absorption
        SpeedBoost,   // Increased movement speed
        DamageBoost,  // Increased damage
        DefenseBoost, // Reduced damage taken
        Lifesteal,    // Heal on hit
        Invulnerable  // Cannot take damage
    };

    Type type;
    float duration;      // seconds remaining
    float tick_timer;    // for DoT/HoT effects
    float tick_interval; // how often DoT/HoT ticks
    float value;         // effect magnitude (damage per tick, slow %, etc.)
    uint32_t source_id;  // who applied this
};

// Helper to construct a StatusEffect in one call.
// For non-DoT effects, tick_interval defaults to 0 (no ticking).
// For DoT/HoT effects, pass the tick interval explicitly.
inline StatusEffect make_status_effect(StatusEffect::Type type, float duration, float value, uint32_t source_id = 0,
                                       float tick_interval = 0.0f) {
    return StatusEffect{type, duration, tick_interval, tick_interval, value, source_id};
}

struct BuffState {
    std::vector<StatusEffect> effects;

    void add(StatusEffect effect) { effects.push_back(effect); }

    bool has(StatusEffect::Type type) const {
        for (const auto& e : effects) {
            {
                if (e.type == type) {
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool is_stunned() const { return has(StatusEffect::Type::Stun) || has(StatusEffect::Type::Freeze); }
    bool is_rooted() const { return has(StatusEffect::Type::Root) || is_stunned(); }

    float get_speed_multiplier() const {
        float mult = 1.0f;
        for (const auto& e : effects) {
            if (e.type == StatusEffect::Type::Slow) {
                {
                    mult *= (1.0f - e.value);
                }
            }
            if (e.type == StatusEffect::Type::SpeedBoost) {
                {
                    mult *= (1.0f + e.value);
                }
            }
        }
        return mult;
    }

    float get_damage_multiplier() const {
        float mult = 1.0f;
        for (const auto& e : effects) {
            if (e.type == StatusEffect::Type::DamageBoost) {
                {
                    mult *= (1.0f + e.value);
                }
            }
        }
        return mult;
    }

    float get_defense_multiplier() const {
        float mult = 1.0f;
        for (const auto& e : effects) {
            if (e.type == StatusEffect::Type::DefenseBoost) {
                {
                    mult *= (1.0f - e.value);
                }
            }
        }
        return mult;
    }

    bool is_invulnerable() const { return has(StatusEffect::Type::Invulnerable); }

    float get_lifesteal() const {
        float total = 0.0f;
        for (const auto& e : effects) {
            if (e.type == StatusEffect::Type::Lifesteal) {
                {
                    total += e.value;
                }
            }
        }
        return total;
    }

    float get_shield_value() const {
        float total = 0.0f;
        for (const auto& e : effects) {
            if (e.type == StatusEffect::Type::Shield) {
                {
                    total += e.value;
                }
            }
        }
        return total;
    }
};
// ============================================================================
// Talent Stats - aggregated read-only stats recomputed when talents change.
// Set by apply_talent_effects; consumed by combat, movement, and passive systems.
// ============================================================================
struct TalentStats {
    float speed_mult = 1.0f;
    float defense_mult = 1.0f;
    float crit_chance = 0.0f;
    float crit_damage_mult = 1.0f;
    float kill_heal_pct = 0.0f;
    float mana_cost_mult = 1.0f;
    float skill_damage_mult = 1.0f;
    float attack_range_bonus = 0.0f;
    float attack_range_mult = 1.0f;
    float healing_received_mult = 1.0f;
    float global_cdr = 0.0f;
    float cooldown_mult = 1.0f;
    bool cc_immunity = false;

    // On-hit procs
    float slow_on_hit_chance = 0.0f;
    float slow_on_hit_value = 0.0f;
    float slow_on_hit_dur = 0.0f;
    float burn_on_hit_pct = 0.0f;
    float burn_on_hit_dur = 0.0f;
    float poison_on_hit_pct = 0.0f;
    float poison_on_hit_dur = 0.0f;
    float mana_on_hit_pct = 0.0f;
    float hit_speed_bonus = 0.0f;
    float hit_speed_dur = 0.0f;

    // Kill effects
    float kill_explosion_pct = 0.0f;
    float kill_explosion_radius = 0.0f;
    float kill_damage_bonus = 0.0f;
    float kill_damage_dur = 0.0f;
    float kill_speed_bonus = 0.0f;
    float kill_speed_dur = 0.0f;
    float burn_spread_radius = 0.0f;

    // Cheat death
    bool has_cheat_death = false;
    float cheat_death_hp = 0.1f;
    float cheat_death_cooldown_max = 60.0f;

    // Damage reflect
    float reflect_percent = 0.0f;

    // Stationary
    float stationary_damage_mult = 1.0f;
    float stationary_damage_reduction = 0.0f;
    float stationary_heal_pct = 0.0f;
    float stationary_delay = 9999.0f;

    // Low HP
    float low_health_regen_pct = 0.0f;
    float low_health_threshold = 0.0f;

    // Fury
    float fury_threshold = 0.0f;
    float fury_damage_mult = 1.0f;
    float fury_attack_speed_mult = 1.0f;

    // Combo
    float combo_damage_bonus = 0.0f;
    int combo_max_stacks = 0;
    float combo_window = 3.0f;

    // Empowered attacks
    int empowered_every = 0;
    float empowered_damage_mult = 1.0f;
    float empowered_stun_dur = 0.0f;

    // Passive aura
    float aura_damage_pct = 0.0f;
    float aura_range = 0.0f;

    // Nearby debuff aura
    float nearby_debuff_range = 0.0f;
    float nearby_damage_reduction = 0.0f;

    // Panic freeze
    float panic_freeze_radius = 0.0f;
    float panic_freeze_duration = 0.0f;
    float panic_freeze_threshold = 0.0f;
    float panic_freeze_cooldown_max = 60.0f;

    // Periodic shield
    float shield_regen_pct = 0.0f;
    float shield_regen_cooldown_max = 15.0f;

    // Spell echo
    float spell_echo_chance = 0.0f;

    // Frozen vulnerability
    float frozen_vulnerability = 0.0f;

    // Mana conditionals
    float high_mana_damage_mult = 1.0f;
    float high_mana_threshold = 0.0f;
    float low_mana_regen_mult = 1.0f;
    float low_mana_threshold = 0.0f;

    // Conditional damage
    float high_hp_bonus_damage = 0.0f;
    float high_hp_threshold = 0.0f;
    float max_range_damage_bonus = 0.0f;

    // Damage sharing
    float damage_share_percent = 0.0f;
    float share_radius = 0.0f;

    // Avenge
    bool has_avenge = false;
    float avenge_damage_mult = 1.0f;
    float avenge_attack_speed_mult = 1.0f;
    float avenge_duration = 0.0f;

    // Dodge
    float moving_dodge_chance = 0.0f;

    // Trap modifications
    int max_traps = 1;
    float trap_lifetime_mult = 1.0f;
    float trap_radius_mult = 1.0f;
    float trap_vulnerability = 0.0f;
    float trap_vulnerability_dur = 0.0f;
    float trap_cdr = 0.0f;
    float trap_cloud_damage = 0.0f;
    float trap_cloud_duration = 0.0f;
    float trap_cloud_radius = 0.0f;
    float poison_death_explosion_pct = 0.0f;
    float poison_explosion_radius = 0.0f;
};

// ============================================================================
// Talent Runtime State - timers, counters, cooldowns updated every tick.
// Preserved across talent changes (apply_talent_effects does NOT reset these).
// ============================================================================
struct TalentRuntimeState {
    float cheat_death_timer = 0.0f;
    float shield_regen_timer = 0.0f;
    float panic_freeze_timer = 0.0f;
    float aura_tick_timer = 0.5f;
    float debuff_aura_timer = 1.0f;
    float stationary_timer = 0.0f;
    bool was_moving_last_tick = false;
    int combo_stacks = 0;
    float combo_decay_timer = 0.0f;
    int empowered_counter = 0;
};

// Backward-compatible alias so existing code using TalentPassiveState still compiles.
// Systems that only need stats should migrate to TalentStats; systems that need
// runtime state should query TalentRuntimeState separately.
using TalentPassiveState = TalentStats;

} // namespace mmo::server::ecs

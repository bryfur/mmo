#include "server/systems/combat_system.hpp"
#include "server/ecs/game_components.hpp"
#include "server/systems/buff_system.hpp"
#include <entt/entt.hpp>
#include <gtest/gtest.h>

using namespace mmo::server;
using namespace mmo::server::systems;

namespace {

entt::entity make_target(entt::registry& r, float hp, float def = 0.0f) {
    auto e = r.create();
    auto& h = r.emplace<ecs::Health>(e);
    h.current = hp;
    h.max = hp;
    auto& eq = r.emplace<ecs::Equipment>(e);
    eq.defense = def;
    return e;
}

} // namespace

// ============================================================================
// apply_damage: basic arithmetic
// ============================================================================

TEST(CombatSystem, ApplyDamageReducesHealth) {
    entt::registry r;
    auto target = make_target(r, 100.0f);
    bool died = apply_damage(r, target, 30.0f);
    EXPECT_FALSE(died);
    EXPECT_FLOAT_EQ(r.get<ecs::Health>(target).current, 70.0f);
}

TEST(CombatSystem, ApplyDamageMarksDeathWhenBelowZero) {
    entt::registry r;
    auto target = make_target(r, 50.0f);
    bool died = apply_damage(r, target, 200.0f);
    EXPECT_TRUE(died);
    EXPECT_LE(r.get<ecs::Health>(target).current, 0.0f);
}

TEST(CombatSystem, DeadTargetCannotDieTwice) {
    entt::registry r;
    auto target = make_target(r, 100.0f);
    bool first = apply_damage(r, target, 500.0f);
    EXPECT_TRUE(first);
    bool second = apply_damage(r, target, 50.0f);
    EXPECT_FALSE(second) << "Dead target should not return target_died=true again.";
}

// Note: Equipment::defense is subtracted in the attack-caller paths
// (player attack + NPC attack loops), not inside apply_damage itself.
// apply_damage handles buffs/shields/talents only.

TEST(CombatSystem, TalentDefenseMultiplierReducesDamage) {
    entt::registry r;
    auto target = make_target(r, 100.0f);
    auto& tp = r.emplace<ecs::TalentPassiveState>(target);
    tp.defense_mult = 0.5f; // take half damage

    apply_damage(r, target, 40.0f);
    EXPECT_FLOAT_EQ(r.get<ecs::Health>(target).current, 80.0f);
}

TEST(CombatSystem, DefenseBoostBuffReducesDamage) {
    entt::registry r;
    auto target = make_target(r, 100.0f);
    // DefenseBoost value=0.5 means take 50% less damage.
    apply_effect(r, target, ecs::make_status_effect(ecs::StatusEffect::Type::DefenseBoost, 10.0f, 0.5f));

    apply_damage(r, target, 40.0f);
    EXPECT_GT(r.get<ecs::Health>(target).current, 60.0f); // less than 40 damage applied
}

// ============================================================================
// Invulnerable blocks all damage
// ============================================================================

TEST(CombatSystem, InvulnerableBlocksDamage) {
    entt::registry r;
    auto target = make_target(r, 100.0f);
    apply_effect(r, target, ecs::make_status_effect(ecs::StatusEffect::Type::Invulnerable, 5.0f, 0.0f));

    bool died = apply_damage(r, target, 1000.0f);
    EXPECT_FALSE(died);
    EXPECT_FLOAT_EQ(r.get<ecs::Health>(target).current, 100.0f);
}

// ============================================================================
// Shield absorbs before health
// ============================================================================

TEST(CombatSystem, ShieldAbsorbsDamage) {
    entt::registry r;
    auto target = make_target(r, 100.0f);
    apply_effect(r, target, ecs::make_status_effect(ecs::StatusEffect::Type::Shield, 10.0f, 50.0f));

    apply_damage(r, target, 30.0f);
    // Shield had 50; absorbs the full 30. Health should be unchanged.
    EXPECT_FLOAT_EQ(r.get<ecs::Health>(target).current, 100.0f);
}

TEST(CombatSystem, ShieldOverflowSpillsToHealth) {
    entt::registry r;
    auto target = make_target(r, 100.0f);
    apply_effect(r, target, ecs::make_status_effect(ecs::StatusEffect::Type::Shield, 10.0f, 20.0f));

    apply_damage(r, target, 30.0f);
    // Shield=20 absorbed, 10 damage carries to health → 90.
    EXPECT_FLOAT_EQ(r.get<ecs::Health>(target).current, 90.0f);
}

// ============================================================================
// Buff state queries
// ============================================================================

TEST(BuffState, IsStunnedTreatsFreezeAsStun) {
    ecs::BuffState bs;
    EXPECT_FALSE(bs.is_stunned());
    bs.add(ecs::make_status_effect(ecs::StatusEffect::Type::Freeze, 2.0f, 0.0f));
    EXPECT_TRUE(bs.is_stunned());
}

TEST(BuffState, SpeedMultiplierStacksSlowAndBoost) {
    ecs::BuffState bs;
    bs.add(ecs::make_status_effect(ecs::StatusEffect::Type::Slow, 5.0f, 0.3f));
    float slow_only = bs.get_speed_multiplier();
    EXPECT_LT(slow_only, 1.0f);

    bs.add(ecs::make_status_effect(ecs::StatusEffect::Type::SpeedBoost, 5.0f, 0.5f));
    float with_boost = bs.get_speed_multiplier();
    EXPECT_GT(with_boost, slow_only);
}

TEST(BuffSystem, ApplyEffectReplacesLongerDurationForCC) {
    entt::registry r;
    auto e = r.create();

    apply_effect(r, e, ecs::make_status_effect(ecs::StatusEffect::Type::Stun, 2.0f, 0.0f));
    apply_effect(r, e, ecs::make_status_effect(ecs::StatusEffect::Type::Stun, 5.0f, 0.0f));
    apply_effect(r, e, ecs::make_status_effect(ecs::StatusEffect::Type::Stun, 1.0f, 0.0f));

    auto& bs = r.get<ecs::BuffState>(e);
    int stun_count = 0;
    float max_dur = 0.0f;
    for (const auto& ef : bs.effects) {
        if (ef.type == ecs::StatusEffect::Type::Stun) {
            ++stun_count;
            max_dur = std::max(max_dur, ef.duration);
        }
    }
    EXPECT_EQ(stun_count, 1) << "CC types must collapse to a single effect instance.";
    EXPECT_FLOAT_EQ(max_dur, 5.0f) << "Longer duration should win.";
}

TEST(BuffSystem, BurnAndPoisonDoTCanStack) {
    entt::registry r;
    auto e = r.create();
    apply_effect(r, e, ecs::make_status_effect(ecs::StatusEffect::Type::Burn, 3.0f, 5.0f));
    apply_effect(r, e, ecs::make_status_effect(ecs::StatusEffect::Type::Burn, 3.0f, 5.0f));
    auto& bs = r.get<ecs::BuffState>(e);
    int n = 0;
    for (const auto& ef : bs.effects) {
        if (ef.type == ecs::StatusEffect::Type::Burn) {
            ++n;
        }
    }
    EXPECT_EQ(n, 2);
}

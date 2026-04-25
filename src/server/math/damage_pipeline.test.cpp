#include "server/math/damage_pipeline.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::math;
using Roll = DamagePipeline::Roll;
using Mitigation = DamagePipeline::Mitigation;

// ============================================================================
// Roll::final_damage
// ============================================================================

TEST(DamagePipeline, NonCritUsesBaseDamage) {
    Roll r{.base_damage = 100.0f, .is_crit = false, .crit_damage_mult = 1.5f};
    EXPECT_FLOAT_EQ(r.final_damage(), 100.0f);
}

TEST(DamagePipeline, CritAppliesMultiplier) {
    Roll r{.base_damage = 100.0f, .is_crit = true, .crit_damage_mult = 2.0f};
    EXPECT_FLOAT_EQ(r.final_damage(), 200.0f);
}

// ============================================================================
// roll_with_crit
// ============================================================================

TEST(DamagePipeline, RollBelowCritChanceCrits) {
    auto r = DamagePipeline::roll_with_crit(100.0f, 0.3f, 1.5f, 0.1f);
    EXPECT_TRUE(r.is_crit);
}

TEST(DamagePipeline, RollAboveCritChanceDoesNotCrit) {
    auto r = DamagePipeline::roll_with_crit(100.0f, 0.3f, 1.5f, 0.9f);
    EXPECT_FALSE(r.is_crit);
}

TEST(DamagePipeline, ZeroCritChanceNeverCrits) {
    auto r = DamagePipeline::roll_with_crit(100.0f, 0.0f, 1.5f, 0.0f);
    EXPECT_FALSE(r.is_crit);
}

TEST(DamagePipeline, FullCritChanceAlwaysCrits) {
    auto r = DamagePipeline::roll_with_crit(100.0f, 1.0f, 1.5f, 0.99f);
    EXPECT_TRUE(r.is_crit);
}

// ============================================================================
// apply_mitigation
// ============================================================================

TEST(DamagePipeline, NoMitigationPassesDamageThrough) {
    Roll r{.base_damage = 50.0f};
    Mitigation m;
    EXPECT_FLOAT_EQ(DamagePipeline::apply_mitigation(r, m), 50.0f);
}

TEST(DamagePipeline, FlatDefenseSubtracted) {
    Roll r{.base_damage = 50.0f};
    Mitigation m{.equipment_defense = 10.0f};
    EXPECT_FLOAT_EQ(DamagePipeline::apply_mitigation(r, m), 40.0f);
}

TEST(DamagePipeline, DefenseBuffReducesDamage) {
    Roll r{.base_damage = 50.0f};
    Mitigation m{.defense_buff_mult = 0.5f};
    EXPECT_FLOAT_EQ(DamagePipeline::apply_mitigation(r, m), 25.0f);
}

TEST(DamagePipeline, TalentDefenseMultStacksWithBuff) {
    Roll r{.base_damage = 100.0f};
    Mitigation m{.defense_buff_mult = 0.5f, .talent_defense_mult = 0.5f};
    EXPECT_FLOAT_EQ(DamagePipeline::apply_mitigation(r, m), 25.0f);
}

TEST(DamagePipeline, InvulnerableBlocksAllDamage) {
    Roll r{.base_damage = 1000.0f};
    Mitigation m{.is_invulnerable = true};
    EXPECT_FLOAT_EQ(DamagePipeline::apply_mitigation(r, m), 0.0f);
}

TEST(DamagePipeline, MitigationFloorAt1Damage) {
    Roll r{.base_damage = 5.0f};
    Mitigation m{.equipment_defense = 100.0f};
    EXPECT_FLOAT_EQ(DamagePipeline::apply_mitigation(r, m), 1.0f);
}

TEST(DamagePipeline, CritDamageAppliedBeforeMitigation) {
    auto r = DamagePipeline::roll_with_crit(50.0f, 1.0f, 2.0f, 0.0f);
    Mitigation m{.equipment_defense = 20.0f};
    EXPECT_FLOAT_EQ(DamagePipeline::apply_mitigation(r, m), 80.0f);
}

// ============================================================================
// absorb_with_shield
// ============================================================================

TEST(DamagePipeline, NoShieldPassesFullDamage) {
    auto r = DamagePipeline::absorb_with_shield(30.0f, 0.0f);
    EXPECT_FLOAT_EQ(r.damage_to_health, 30.0f);
    EXPECT_FLOAT_EQ(r.shield_consumed, 0.0f);
}

TEST(DamagePipeline, FullAbsorptionWhenShieldExceedsDamage) {
    auto r = DamagePipeline::absorb_with_shield(10.0f, 50.0f);
    EXPECT_FLOAT_EQ(r.damage_to_health, 0.0f);
    EXPECT_FLOAT_EQ(r.shield_consumed, 10.0f);
}

TEST(DamagePipeline, PartialAbsorptionSpillsToHealth) {
    auto r = DamagePipeline::absorb_with_shield(50.0f, 20.0f);
    EXPECT_FLOAT_EQ(r.damage_to_health, 30.0f);
    EXPECT_FLOAT_EQ(r.shield_consumed, 20.0f);
}

TEST(DamagePipeline, ZeroDamageNeverConsumesShield) {
    auto r = DamagePipeline::absorb_with_shield(0.0f, 100.0f);
    EXPECT_FLOAT_EQ(r.damage_to_health, 0.0f);
    EXPECT_FLOAT_EQ(r.shield_consumed, 0.0f);
}

// ============================================================================
// apply_to_health
// ============================================================================

TEST(DamagePipeline, ApplyDamageReducesHealth) {
    auto r = DamagePipeline::apply_to_health(100.0f, 200.0f, 30.0f, false);
    EXPECT_FLOAT_EQ(r.new_health, 70.0f);
    EXPECT_FALSE(r.died);
}

TEST(DamagePipeline, LethalDamageFlagsDeath) {
    auto r = DamagePipeline::apply_to_health(20.0f, 200.0f, 50.0f, false);
    EXPECT_FLOAT_EQ(r.new_health, 0.0f);
    EXPECT_TRUE(r.died);
    EXPECT_FALSE(r.cheat_death_eligible);
}

TEST(DamagePipeline, AlreadyDeadTargetDoesNotDieAgain) {
    auto r = DamagePipeline::apply_to_health(0.0f, 200.0f, 100.0f, false);
    EXPECT_FALSE(r.died);
}

TEST(DamagePipeline, CheatDeathEligibleOnLethalBlow) {
    auto r = DamagePipeline::apply_to_health(10.0f, 200.0f, 9999.0f, true);
    EXPECT_TRUE(r.died);
    EXPECT_TRUE(r.cheat_death_eligible);
}

TEST(DamagePipeline, CheatDeathNotEligibleWhenNotDying) {
    auto r = DamagePipeline::apply_to_health(100.0f, 200.0f, 10.0f, true);
    EXPECT_FALSE(r.died);
    EXPECT_FALSE(r.cheat_death_eligible);
}

// ============================================================================
// Lifesteal
// ============================================================================

TEST(DamagePipeline, LifestealComputes) {
    EXPECT_FLOAT_EQ(DamagePipeline::compute_lifesteal_heal(100.0f, 0.2f), 20.0f);
}

TEST(DamagePipeline, LifestealZeroPctIsZero) {
    EXPECT_FLOAT_EQ(DamagePipeline::compute_lifesteal_heal(100.0f, 0.0f), 0.0f);
}

TEST(DamagePipeline, LifestealOnZeroDamageIsZero) {
    EXPECT_FLOAT_EQ(DamagePipeline::compute_lifesteal_heal(0.0f, 0.5f), 0.0f);
}

#include <gtest/gtest.h>
#include "server/rules/skill_gate.hpp"

using mmo::server::rules::SkillGate;
using mmo::server::rules::Rule;

// Contract: compile-time enforcement that SkillGate implements Rule.
static_assert(Rule<SkillGate>);

namespace {

SkillGate::Inputs baseline() {
    return SkillGate::Inputs{
        .skill_exists = true,
        .caster_alive = true,
        .caster_class = "warrior",
        .skill_class = "warrior",
        .caster_level = 5,
        .skill_unlock_level = 2,
        .current_cooldown = 0.0f,
        .caster_mana = 100.0f,
        .mana_cost = 20.0f,
    };
}

} // namespace

// ============================================================================
// Happy path
// ============================================================================

TEST(SkillGate, BaselineIsOk) {
    EXPECT_EQ(SkillGate::check(baseline()), SkillGate::Result::Ok);
}

// ============================================================================
// Each failure branch
// ============================================================================

TEST(SkillGate, UnknownSkillRejected) {
    auto s = baseline();
    s.skill_exists = false;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::NoSuchSkill);
}

TEST(SkillGate, DeadCasterRejected) {
    auto s = baseline();
    s.caster_alive = false;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::CasterDead);
}

TEST(SkillGate, WrongClassRejected) {
    auto s = baseline();
    s.skill_class = "mage";
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::WrongClass);
}

TEST(SkillGate, LevelTooLowRejected) {
    auto s = baseline();
    s.caster_level = 1;
    s.skill_unlock_level = 5;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::LevelTooLow);
}

TEST(SkillGate, OnCooldownRejected) {
    auto s = baseline();
    s.current_cooldown = 0.1f;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::OnCooldown);
}

TEST(SkillGate, InsufficientManaRejected) {
    auto s = baseline();
    s.caster_mana = 5.0f;
    s.mana_cost = 20.0f;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::InsufficientMana);
}

TEST(SkillGate, ZeroCooldownNotRejected) {
    auto s = baseline();
    s.current_cooldown = 0.0f;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::Ok);
}

TEST(SkillGate, ExactLevelUnlockAllowed) {
    auto s = baseline();
    s.caster_level = 5;
    s.skill_unlock_level = 5;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::Ok);
}

TEST(SkillGate, ExactManaMatchAllowed) {
    auto s = baseline();
    s.caster_mana = 20.0f;
    s.mana_cost = 20.0f;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::Ok);
}

// ============================================================================
// Priority of failures
// ============================================================================

TEST(SkillGate, DeadOutranksWrongClass) {
    auto s = baseline();
    s.caster_alive = false;
    s.skill_class = "mage";
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::CasterDead);
}

TEST(SkillGate, MissingSkillOutranksAllOtherChecks) {
    auto s = baseline();
    s.skill_exists = false;
    s.caster_alive = false;
    s.caster_mana = 0.0f;
    EXPECT_EQ(SkillGate::check(s), SkillGate::Result::NoSuchSkill);
}

// ============================================================================
// Static helpers
// ============================================================================

TEST(SkillGate, EffectiveCooldownNoMultipliers) {
    EXPECT_FLOAT_EQ(SkillGate::effective_cooldown(10.0f, 1.0f, 0.0f), 10.0f);
}

TEST(SkillGate, EffectiveCooldownCombinesMultAndCdr) {
    EXPECT_FLOAT_EQ(SkillGate::effective_cooldown(10.0f, 0.5f, 0.5f), 2.5f);
}

TEST(SkillGate, EffectiveCooldownFloorsAt0p5) {
    EXPECT_FLOAT_EQ(SkillGate::effective_cooldown(10.0f, 0.05f, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(SkillGate::effective_cooldown(1.0f, 0.1f, 0.9f), 0.5f);
}

TEST(SkillGate, EffectiveManaCostScales) {
    EXPECT_FLOAT_EQ(SkillGate::effective_mana_cost(20.0f, 0.5f), 10.0f);
}

TEST(SkillGate, EffectiveManaCostClampsAtZero) {
    EXPECT_FLOAT_EQ(SkillGate::effective_mana_cost(20.0f, -1.0f), 0.0f);
}

// Everything is constexpr - evaluate at compile time.
static_assert(SkillGate::effective_cooldown(10.0f, 1.0f, 0.0f) == 10.0f);
static_assert(SkillGate::effective_mana_cost(20.0f, 0.5f) == 10.0f);

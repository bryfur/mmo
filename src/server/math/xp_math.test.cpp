#include "server/math/xp_math.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::math;

static const XPMath::Curve kCurve = {0, 100, 250, 500, 850, 1300, 1850};

// ============================================================================
// level_floor
// ============================================================================

TEST(XPMath, FloorAtLevelOneIsCurveStart) {
    EXPECT_EQ(XPMath::level_floor(kCurve, 1), 0);
}

TEST(XPMath, FloorAtLevelThreeIs250) {
    EXPECT_EQ(XPMath::level_floor(kCurve, 3), 250);
}

TEST(XPMath, FloorClampsBelowOne) {
    EXPECT_EQ(XPMath::level_floor(kCurve, 0), 0);
    EXPECT_EQ(XPMath::level_floor(kCurve, -10), 0);
}

TEST(XPMath, FloorClampsAboveMax) {
    EXPECT_EQ(XPMath::level_floor(kCurve, 99), 1850);
}

TEST(XPMath, EmptyCurveIsSafe) {
    EXPECT_EQ(XPMath::level_floor({}, 5), 0);
}

// ============================================================================
// xp_needed_for_next_level
// ============================================================================

TEST(XPMath, NeededForNextAtLevelOneIs100) {
    EXPECT_EQ(XPMath::xp_needed_for_next_level(kCurve, 1), 100);
}

TEST(XPMath, NeededForNextAtLevelThreeIs500) {
    EXPECT_EQ(XPMath::xp_needed_for_next_level(kCurve, 3), 500);
}

TEST(XPMath, NeededForNextAtCapReturnsLastThreshold) {
    EXPECT_EQ(XPMath::xp_needed_for_next_level(kCurve, 7), 1850);
}

// ============================================================================
// compute_new_level
// ============================================================================

TEST(XPMath, NoLevelUpWhenBelowThreshold) {
    EXPECT_EQ(XPMath::compute_new_level(kCurve, 1, 50, 30), 1);
}

TEST(XPMath, SingleLevelUpAtThreshold) {
    EXPECT_EQ(XPMath::compute_new_level(kCurve, 1, 100, 30), 2);
}

TEST(XPMath, SingleLevelUpJustAboveThreshold) {
    EXPECT_EQ(XPMath::compute_new_level(kCurve, 1, 150, 30), 2);
}

TEST(XPMath, DoubleLevelUpFromBigXPGain) {
    EXPECT_EQ(XPMath::compute_new_level(kCurve, 1, 350, 30), 3);
}

TEST(XPMath, LevelUpDoesntExceedMaxLevel) {
    EXPECT_EQ(XPMath::compute_new_level(kCurve, 1, 99999, 5), 5);
}

TEST(XPMath, AlreadyAtMaxLevelStaysAtMax) {
    EXPECT_EQ(XPMath::compute_new_level(kCurve, 30, 99999, 30), 30);
}

// ============================================================================
// compute_death_xp_loss
// ============================================================================

TEST(XPMath, DeathPenaltyAtLevelOne) {
    EXPECT_EQ(XPMath::compute_death_xp_loss(kCurve, 1, 50, 5.0f), 5);
}

TEST(XPMath, DeathPenaltyClampsAtFloor) {
    EXPECT_EQ(XPMath::compute_death_xp_loss(kCurve, 2, 100, 5.0f), 0);
}

TEST(XPMath, DeathPenaltyAtZeroPctIsZero) {
    EXPECT_EQ(XPMath::compute_death_xp_loss(kCurve, 3, 400, 0.0f), 0);
}

TEST(XPMath, DeathPenalty100PctIsFullBracket) {
    EXPECT_EQ(XPMath::compute_death_xp_loss(kCurve, 1, 50, 100.0f), 50);
}

TEST(XPMath, LevelOneDeathPenaltyFromFreshSpawn) {
    EXPECT_EQ(XPMath::compute_death_xp_loss(kCurve, 1, 0, 5.0f), 0);
}

// ============================================================================
// level_diff_xp_modifier
// ============================================================================

TEST(XPMath, SameLevelIsFullXP) {
    EXPECT_FLOAT_EQ(XPMath::level_diff_xp_modifier(10, 10), 1.0f);
}

TEST(XPMath, OneLevelEitherDirectionIsFullXP) {
    EXPECT_FLOAT_EQ(XPMath::level_diff_xp_modifier(10, 11), 1.0f);
    EXPECT_FLOAT_EQ(XPMath::level_diff_xp_modifier(10, 9), 1.0f);
}

TEST(XPMath, FarBelowLevelIsTrivialXP) {
    EXPECT_FLOAT_EQ(XPMath::level_diff_xp_modifier(30, 20), 0.10f);
}

TEST(XPMath, FarAboveLevelGivesBonus) {
    EXPECT_FLOAT_EQ(XPMath::level_diff_xp_modifier(5, 15), 2.50f);
}

TEST(XPMath, ModifierIsMonotonicAcrossRange) {
    float prev = 0.0f;
    for (int diff = -10; diff <= 10; ++diff) {
        float m = XPMath::level_diff_xp_modifier(10, 10 + diff);
        EXPECT_GE(m, prev) << "diff=" << diff;
        prev = m;
    }
}

static_assert(XPMath::level_diff_xp_modifier(10, 10) == 1.0f);

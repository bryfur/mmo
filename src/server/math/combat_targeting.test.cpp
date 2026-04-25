#include "server/math/combat_targeting.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::math;

// ============================================================================
// Distance helpers
// ============================================================================

TEST(CombatTargeting, DistanceXZIgnoresYAxis) {
    EXPECT_FLOAT_EQ(CombatTargeting::distance_xz(0, 0, 3, 4), 5.0f);
}

TEST(CombatTargeting, DistanceSqIsXSquaredPlusZSquared) {
    EXPECT_FLOAT_EQ(CombatTargeting::distance_sq_xz(1, 2, 4, 6), 9.0f + 16.0f);
}

// ============================================================================
// in_range
// ============================================================================

TEST(CombatTargeting, InRangeStrictlyCloserIsInside) {
    EXPECT_TRUE(CombatTargeting::in_range(0, 0, 10, 0, 20.0f));
}

TEST(CombatTargeting, InRangeFartherIsOutside) {
    EXPECT_FALSE(CombatTargeting::in_range(0, 0, 30, 0, 20.0f));
}

TEST(CombatTargeting, InRangeBoundaryIsIncluded) {
    EXPECT_TRUE(CombatTargeting::in_range(0, 0, 20, 0, 20.0f));
}

TEST(CombatTargeting, InRangeZeroRangeAlwaysFalse) {
    EXPECT_FALSE(CombatTargeting::in_range(0, 0, 0, 0, 0.0f));
}

TEST(CombatTargeting, InRangeNegativeRangeAlwaysFalse) {
    EXPECT_FALSE(CombatTargeting::in_range(0, 0, 1, 0, -5.0f));
}

// ============================================================================
// in_cone
// ============================================================================

TEST(CombatTargeting, InConeNoFilterWhenAngleZero) {
    EXPECT_TRUE(CombatTargeting::in_cone(0, 0, -100, 0, 1, 0, 0.0f));
}

TEST(CombatTargeting, InConeDirectlyForwardAlwaysHits) {
    EXPECT_TRUE(CombatTargeting::in_cone(0, 0, 10, 0, 1, 0, 0.1f));
}

TEST(CombatTargeting, InConeBehindAttackerRejected) {
    EXPECT_FALSE(CombatTargeting::in_cone(0, 0, -10, 0, 1, 0, 0.1f));
}

TEST(CombatTargeting, InConeNarrowAngleRejectsPerpendicular) {
    EXPECT_FALSE(CombatTargeting::in_cone(0, 0, 0, 10, 1, 0, 0.5f));
}

TEST(CombatTargeting, InConePerpendicularAtPiOver2) {
    EXPECT_TRUE(CombatTargeting::in_cone(0, 0, 0, 10, 1, 0, 1.5708f));
}

TEST(CombatTargeting, InConeWideAngleIsOmnidirectional) {
    EXPECT_TRUE(CombatTargeting::in_cone(0, 0, -10, 0, 1, 0, 3.15f));
}

TEST(CombatTargeting, InConeZeroDistanceAlwaysTrue) {
    EXPECT_TRUE(CombatTargeting::in_cone(5, 5, 5, 5, 1, 0, 0.5f));
}

// ============================================================================
// can_target
// ============================================================================

TEST(CombatTargeting, CanTargetRequiresBothRangeAndCone) {
    EXPECT_FALSE(CombatTargeting::can_target(0, 0, 100, 0, 1, 0, 50, 1.0f));
    EXPECT_FALSE(CombatTargeting::can_target(0, 0, -10, 0, 1, 0, 50, 0.1f));
    EXPECT_TRUE(CombatTargeting::can_target(0, 0, 10, 0, 1, 0, 50, 1.0f));
}

// ============================================================================
// effective_cone_half_angle
// ============================================================================

TEST(CombatTargeting, EffectiveConeUsesExplicitConeFirst) {
    EXPECT_FLOAT_EQ(CombatTargeting::effective_cone_half_angle(0.8f, 0.4f, false), 0.8f);
}

TEST(CombatTargeting, EffectiveConePromotesSpreadAngleByHalf) {
    EXPECT_FLOAT_EQ(CombatTargeting::effective_cone_half_angle(0.0f, 0.8f, false), 0.4f);
}

TEST(CombatTargeting, EffectiveConePiercingFallback) {
    EXPECT_FLOAT_EQ(CombatTargeting::effective_cone_half_angle(0.0f, 0.0f, true), 0.35f);
}

TEST(CombatTargeting, EffectiveConeDefaultIsUnfiltered) {
    EXPECT_FLOAT_EQ(CombatTargeting::effective_cone_half_angle(0.0f, 0.0f, false), 0.0f);
}

TEST(CombatTargeting, EffectiveConeExplicitBeatsPiercing) {
    EXPECT_FLOAT_EQ(CombatTargeting::effective_cone_half_angle(1.2f, 0.0f, true), 1.2f);
}

static_assert(CombatTargeting::in_range(0, 0, 10, 0, 20.0f));
static_assert(CombatTargeting::effective_cone_half_angle(0.0f, 0.8f, false) == 0.4f);

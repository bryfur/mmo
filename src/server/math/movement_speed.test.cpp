#include <gtest/gtest.h>
#include "server/math/movement_speed.hpp"
#include <cmath>

using namespace mmo::server::math;
using Inputs = MovementSpeed::Inputs;

// ============================================================================
// compute (final speed)
// ============================================================================

TEST(MovementSpeed, BaseClassSpeedAlone) {
    Inputs s{ .class_base_speed = 200.0f };
    EXPECT_FLOAT_EQ(MovementSpeed::compute(s), 200.0f);
}

TEST(MovementSpeed, EquipmentBonusAdds) {
    Inputs s{ .class_base_speed = 200.0f, .equipment_bonus = 25.0f };
    EXPECT_FLOAT_EQ(MovementSpeed::compute(s), 225.0f);
}

TEST(MovementSpeed, TalentMultiplierApplies) {
    Inputs s{ .class_base_speed = 200.0f, .talent_speed_mult = 1.10f };
    EXPECT_FLOAT_EQ(MovementSpeed::compute(s), 220.0f);
}

TEST(MovementSpeed, BuffMultiplierStacksWithTalent) {
    Inputs s{
        .class_base_speed = 100.0f,
        .talent_speed_mult = 1.5f,
        .buff_speed_mult = 1.2f,
    };
    EXPECT_FLOAT_EQ(MovementSpeed::compute(s), 180.0f);
}

TEST(MovementSpeed, SprintMultiplies) {
    Inputs s{ .class_base_speed = 100.0f, .is_sprinting = true };
    EXPECT_FLOAT_EQ(MovementSpeed::compute(s), 100.0f * MovementSpeed::SPRINT_MULTIPLIER);
}

TEST(MovementSpeed, RootedReturnsZero) {
    Inputs s{ .class_base_speed = 200.0f, .is_sprinting = true, .is_rooted = true };
    EXPECT_FLOAT_EQ(MovementSpeed::compute(s), 0.0f);
}

TEST(MovementSpeed, StunnedReturnsZero) {
    Inputs s{ .class_base_speed = 200.0f, .buff_speed_mult = 5.0f, .is_stunned = true };
    EXPECT_FLOAT_EQ(MovementSpeed::compute(s), 0.0f);
}

TEST(MovementSpeed, SlowDebuffViaBuffMult) {
    Inputs s{ .class_base_speed = 200.0f, .buff_speed_mult = 0.5f };
    EXPECT_FLOAT_EQ(MovementSpeed::compute(s), 100.0f);
}

TEST(MovementSpeed, NeverNegative) {
    Inputs s{ .class_base_speed = 200.0f, .equipment_bonus = -1000.0f };
    EXPECT_GE(MovementSpeed::compute(s), 0.0f);
}

TEST(MovementSpeed, FullStackExample) {
    Inputs s{
        .class_base_speed = 200.0f,
        .equipment_bonus = 20.0f,
        .talent_speed_mult = 1.10f,
        .buff_speed_mult = 0.8f,
        .is_sprinting = true,
    };
    EXPECT_NEAR(MovementSpeed::compute(s), 309.76f, 0.01f);
}

// ============================================================================
// compute_velocity
// ============================================================================

TEST(MovementSpeed, VelocityFromUnitDirection) {
    auto v = MovementSpeed::compute_velocity(100.0f, 1.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.vx, 100.0f);
    EXPECT_FLOAT_EQ(v.vz, 0.0f);
}

TEST(MovementSpeed, VelocityNormalizesNonUnitInput) {
    auto v = MovementSpeed::compute_velocity(50.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.vx, 30.0f);
    EXPECT_FLOAT_EQ(v.vz, 40.0f);
}

TEST(MovementSpeed, VelocityZeroDirIsZero) {
    auto v = MovementSpeed::compute_velocity(100.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.vx, 0.0f);
    EXPECT_FLOAT_EQ(v.vz, 0.0f);
}

TEST(MovementSpeed, DiagonalDoesNotBoost) {
    auto v = MovementSpeed::compute_velocity(100.0f, 1.0f, 1.0f);
    float total = std::sqrt(v.vx * v.vx + v.vz * v.vz);
    EXPECT_NEAR(total, 100.0f, 0.01f);
}

static_assert(MovementSpeed::compute({.class_base_speed = 100.0f}) == 100.0f);

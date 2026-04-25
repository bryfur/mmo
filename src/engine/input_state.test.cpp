#include "engine/input_state.hpp"
#include <cmath>
#include <gtest/gtest.h>

using namespace mmo::engine;

// --- InputState default values ---

TEST(InputState, DefaultMovementBoolsAreFalse) {
    InputState s;
    EXPECT_FALSE(s.move_up);
    EXPECT_FALSE(s.move_down);
    EXPECT_FALSE(s.move_left);
    EXPECT_FALSE(s.move_right);
}

TEST(InputState, DefaultMoveDirIsZero) {
    InputState s;
    EXPECT_FLOAT_EQ(s.move_dir_x, 0.0f);
    EXPECT_FLOAT_EQ(s.move_dir_y, 0.0f);
}

TEST(InputState, DefaultActionState) {
    InputState s;
    EXPECT_FALSE(s.attacking);
    EXPECT_FALSE(s.sprinting);
}

TEST(InputState, DefaultAttackDirPointsForward) {
    InputState s;
    EXPECT_FLOAT_EQ(s.attack_dir_x, 0.0f);
    EXPECT_FLOAT_EQ(s.attack_dir_y, 1.0f);
}

// --- normalize_move_dir ---

TEST(NormalizeMoveDir, ZeroVectorStaysZero) {
    float x = 0.0f;
    float y = 0.0f;
    float len = normalize_move_dir(x, y);
    EXPECT_FLOAT_EQ(x, 0.0f);
    EXPECT_FLOAT_EQ(y, 0.0f);
    EXPECT_FLOAT_EQ(len, 0.0f);
}

TEST(NormalizeMoveDir, UnitVectorUnchanged) {
    float x = 1.0f;
    float y = 0.0f;
    float len = normalize_move_dir(x, y);
    EXPECT_FLOAT_EQ(x, 1.0f);
    EXPECT_FLOAT_EQ(y, 0.0f);
    EXPECT_FLOAT_EQ(len, 1.0f);
}

TEST(NormalizeMoveDir, SubUnitPreserved) {
    // Analog stick at half tilt
    float x = 0.3f;
    float y = 0.4f;
    float original_len = std::sqrt(0.3f * 0.3f + 0.4f * 0.4f); // 0.5
    float len = normalize_move_dir(x, y);
    EXPECT_FLOAT_EQ(x, 0.3f);
    EXPECT_FLOAT_EQ(y, 0.4f);
    EXPECT_NEAR(len, original_len, 1e-6f);
}

TEST(NormalizeMoveDir, DiagonalKeyboardClamped) {
    // W+D pressed: forward + right = (1, 1), magnitude ~1.414
    float x = 1.0f;
    float y = 1.0f;
    float len = normalize_move_dir(x, y);
    EXPECT_FLOAT_EQ(len, 1.0f);

    float result_len = std::sqrt(x * x + y * y);
    EXPECT_NEAR(result_len, 1.0f, 1e-6f);

    // Direction should be preserved (45 degrees)
    EXPECT_NEAR(x, 1.0f / std::sqrt(2.0f), 1e-6f);
    EXPECT_NEAR(y, 1.0f / std::sqrt(2.0f), 1e-6f);
}

TEST(NormalizeMoveDir, LargeVectorNormalizedToUnit) {
    float x = 3.0f;
    float y = 4.0f;
    normalize_move_dir(x, y);
    float result_len = std::sqrt(x * x + y * y);
    EXPECT_NEAR(result_len, 1.0f, 1e-6f);
    // Direction preserved: 3/5 and 4/5
    EXPECT_NEAR(x, 0.6f, 1e-6f);
    EXPECT_NEAR(y, 0.8f, 1e-6f);
}

TEST(NormalizeMoveDir, NegativeComponentsHandled) {
    float x = -1.0f;
    float y = -1.0f;
    normalize_move_dir(x, y);
    float result_len = std::sqrt(x * x + y * y);
    EXPECT_NEAR(result_len, 1.0f, 1e-6f);
    EXPECT_NEAR(x, -1.0f / std::sqrt(2.0f), 1e-6f);
    EXPECT_NEAR(y, -1.0f / std::sqrt(2.0f), 1e-6f);
}

TEST(NormalizeMoveDir, ExactlyUnitMagnitudeNotModified) {
    // (0.6, 0.8) has magnitude exactly 1.0
    float x = 0.6f;
    float y = 0.8f;
    normalize_move_dir(x, y);
    EXPECT_FLOAT_EQ(x, 0.6f);
    EXPECT_FLOAT_EQ(y, 0.8f);
}

// --- Attack direction values ---

TEST(InputState, AttackDirectionCanBeSet) {
    InputState s;
    s.attack_dir_x = -1.0f;
    s.attack_dir_y = 0.0f;
    EXPECT_FLOAT_EQ(s.attack_dir_x, -1.0f);
    EXPECT_FLOAT_EQ(s.attack_dir_y, 0.0f);
}

TEST(InputState, AttackDirectionArbitraryValues) {
    InputState s;
    s.attack_dir_x = 0.707f;
    s.attack_dir_y = 0.707f;
    float len = std::sqrt(s.attack_dir_x * s.attack_dir_x + s.attack_dir_y * s.attack_dir_y);
    EXPECT_NEAR(len, 1.0f, 0.001f);
}

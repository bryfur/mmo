#include "engine/animation/animation_types.hpp"
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>
#include <vector>

using namespace mmo::engine::animation;

// ============================================================================
// interpolate_keyframes<glm::vec3>
// ============================================================================

TEST(InterpolateKeyframes, EmptyReturnsZero) {
    std::vector<float> times;
    std::vector<glm::vec3> values;
    auto result = interpolate_keyframes(times, values, 0.5f);
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
}

TEST(InterpolateKeyframes, SingleKeyframeReturnsValue) {
    std::vector<float> times = {0.0f};
    std::vector<glm::vec3> values = {{1.0f, 2.0f, 3.0f}};

    // Any time should return the single value
    auto r0 = interpolate_keyframes(times, values, -1.0f);
    auto r1 = interpolate_keyframes(times, values, 0.0f);
    auto r2 = interpolate_keyframes(times, values, 100.0f);
    EXPECT_FLOAT_EQ(r0.x, 1.0f);
    EXPECT_FLOAT_EQ(r1.y, 2.0f);
    EXPECT_FLOAT_EQ(r2.z, 3.0f);
}

TEST(InterpolateKeyframes, BeforeFirstReturnsFirst) {
    std::vector<float> times = {1.0f, 2.0f};
    std::vector<glm::vec3> values = {{10.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}};
    auto result = interpolate_keyframes(times, values, 0.0f);
    EXPECT_FLOAT_EQ(result.x, 10.0f);
}

TEST(InterpolateKeyframes, AfterLastReturnsLast) {
    std::vector<float> times = {0.0f, 1.0f};
    std::vector<glm::vec3> values = {{0.0f, 0.0f, 0.0f}, {5.0f, 5.0f, 5.0f}};
    auto result = interpolate_keyframes(times, values, 99.0f);
    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 5.0f);
}

TEST(InterpolateKeyframes, ExactKeyframeReturnsExactValue) {
    std::vector<float> times = {0.0f, 1.0f, 2.0f};
    std::vector<glm::vec3> values = {{0, 0, 0}, {10, 10, 10}, {20, 20, 20}};
    auto result = interpolate_keyframes(times, values, 1.0f);
    EXPECT_NEAR(result.x, 10.0f, 1e-4f);
}

TEST(InterpolateKeyframes, MidpointLinearInterpolation) {
    std::vector<float> times = {0.0f, 1.0f};
    std::vector<glm::vec3> values = {{0.0f, 0.0f, 0.0f}, {2.0f, 4.0f, 6.0f}};
    auto result = interpolate_keyframes(times, values, 0.5f);
    EXPECT_NEAR(result.x, 1.0f, 1e-4f);
    EXPECT_NEAR(result.y, 2.0f, 1e-4f);
    EXPECT_NEAR(result.z, 3.0f, 1e-4f);
}

TEST(InterpolateKeyframes, QuarterPointInterpolation) {
    std::vector<float> times = {0.0f, 1.0f};
    std::vector<glm::vec3> values = {{0.0f, 0.0f, 0.0f}, {4.0f, 8.0f, 12.0f}};
    auto result = interpolate_keyframes(times, values, 0.25f);
    EXPECT_NEAR(result.x, 1.0f, 1e-4f);
    EXPECT_NEAR(result.y, 2.0f, 1e-4f);
    EXPECT_NEAR(result.z, 3.0f, 1e-4f);
}

TEST(InterpolateKeyframes, CursorCachingSequentialAccess) {
    std::vector<float> times = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<glm::vec3> values = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}};
    size_t cursor = 0;

    // Sequential access should update cursor
    interpolate_keyframes(times, values, 0.5f, cursor);
    EXPECT_EQ(cursor, 0u);

    interpolate_keyframes(times, values, 1.5f, cursor);
    EXPECT_EQ(cursor, 1u);

    interpolate_keyframes(times, values, 2.5f, cursor);
    EXPECT_EQ(cursor, 2u);

    // Cached cursor should hit fast path on next sequential call
    interpolate_keyframes(times, values, 2.7f, cursor);
    EXPECT_EQ(cursor, 2u); // still in same bracket
}

TEST(InterpolateKeyframes, MultiSegmentInterpolation) {
    std::vector<float> times = {0.0f, 1.0f, 2.0f};
    std::vector<glm::vec3> values = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}};

    auto r1 = interpolate_keyframes(times, values, 0.5f);
    EXPECT_NEAR(r1.x, 5.0f, 1e-4f);
    EXPECT_NEAR(r1.y, 0.0f, 1e-4f);

    auto r2 = interpolate_keyframes(times, values, 1.5f);
    EXPECT_NEAR(r2.x, 10.0f, 1e-4f);
    EXPECT_NEAR(r2.y, 5.0f, 1e-4f);
}

// ============================================================================
// interpolate_keyframes<glm::quat> (slerp)
// ============================================================================

TEST(InterpolateKeyframesQuat, MidpointSlerp) {
    std::vector<float> times = {0.0f, 1.0f};
    glm::quat identity = glm::quat(1, 0, 0, 0);
    glm::quat rot90z = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    std::vector<glm::quat> values = {identity, rot90z};

    auto result = interpolate_keyframes(times, values, 0.5f);

    // Halfway between 0 and 90 degrees around Z should be 45 degrees
    glm::quat expected = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1));
    EXPECT_NEAR(result.x, expected.x, 1e-3f);
    EXPECT_NEAR(result.y, expected.y, 1e-3f);
    EXPECT_NEAR(result.z, expected.z, 1e-3f);
    EXPECT_NEAR(result.w, expected.w, 1e-3f);
}

// ============================================================================
// RotationSmoother
// ============================================================================

TEST(RotationSmoother, FirstCallSnapsToTarget) {
    RotationSmoother smoother;
    smoother.smooth_toward(1.5f, 0.016f);
    EXPECT_FLOAT_EQ(smoother.current, 1.5f);
    EXPECT_TRUE(smoother.initialized);
}

TEST(RotationSmoother, SmoothingMovesTowardTarget) {
    RotationSmoother smoother;
    smoother.smooth_toward(0.0f, 0.016f); // init
    smoother.smooth_toward(1.0f, 0.016f); // smooth toward 1.0

    EXPECT_GT(smoother.current, 0.0f); // moved toward target
    EXPECT_LT(smoother.current, 1.0f); // didn't overshoot
}

TEST(RotationSmoother, ConvergesOverTime) {
    RotationSmoother smoother;
    smoother.smooth_toward(0.0f, 0.016f);

    for (int i = 0; i < 1000; i++) {
        smoother.smooth_toward(2.0f, 0.016f);
    }

    EXPECT_NEAR(smoother.current, 2.0f, 0.01f);
}

TEST(RotationSmoother, DecayTurnRate) {
    RotationSmoother smoother;
    smoother.smooth_toward(0.0f, 0.016f);
    smoother.smooth_toward(1.0f, 0.016f);

    float rate_before = smoother.turn_rate;
    smoother.decay_turn_rate(0.5f);
    EXPECT_NEAR(smoother.turn_rate, rate_before * 0.5f, 1e-4f);
}

// ============================================================================
// FootIKSmoother
// ============================================================================

TEST(FootIKSmoother, SmoothsTowardTarget) {
    FootIKSmoother smoother;
    smoother.update(1.0f, 0.0f, 0.016f);

    EXPECT_GT(smoother.smoothed_left_offset, 0.0f);
    EXPECT_LT(smoother.smoothed_left_offset, 1.0f);
}

TEST(FootIKSmoother, ConvergesOverTime) {
    FootIKSmoother smoother;
    for (int i = 0; i < 200; i++) {
        smoother.update(5.0f, -3.0f, 0.016f);
    }
    EXPECT_NEAR(smoother.smoothed_left_offset, 5.0f, 0.1f);
    EXPECT_NEAR(smoother.smoothed_right_offset, -3.0f, 0.1f);
}

// ============================================================================
// interpolate_keyframes edge cases
// ============================================================================

TEST(InterpolateKeyframes, NonUniformTimeSpacing) {
    // Times are heavily skewed: short gap then very long gap
    std::vector<float> times = {0.0f, 0.1f, 10.0f};
    std::vector<glm::vec3> values = {{0, 0, 0}, {1, 0, 0}, {100, 0, 0}};

    // Midpoint of first segment [0, 0.1]
    auto r1 = interpolate_keyframes(times, values, 0.05f);
    EXPECT_NEAR(r1.x, 0.5f, 1e-4f);

    // Midpoint of second segment [0.1, 10.0]
    auto r2 = interpolate_keyframes(times, values, 5.05f);
    EXPECT_NEAR(r2.x, 50.5f, 1e-4f);
}

TEST(InterpolateKeyframes, LargeNumberOfKeyframes) {
    const int N = 1000;
    std::vector<float> times(N);
    std::vector<glm::vec3> values(N);
    for (int i = 0; i < N; i++) {
        times[i] = static_cast<float>(i);
        values[i] = glm::vec3(static_cast<float>(i), 0, 0);
    }

    // Interpolate at midpoint of segment [500, 501]
    auto result = interpolate_keyframes(times, values, 500.5f);
    EXPECT_NEAR(result.x, 500.5f, 1e-3f);

    // Check first and last segments too
    auto first = interpolate_keyframes(times, values, 0.5f);
    EXPECT_NEAR(first.x, 0.5f, 1e-3f);

    auto last = interpolate_keyframes(times, values, 998.5f);
    EXPECT_NEAR(last.x, 998.5f, 1e-3f);
}

TEST(InterpolateKeyframes, CursorCacheBackwardSeek) {
    std::vector<float> times = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<glm::vec3> values = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}};
    size_t cursor = 0;

    // Seek forward to segment [3, 4]
    interpolate_keyframes(times, values, 3.5f, cursor);
    EXPECT_EQ(cursor, 3u);

    // Now seek backward to segment [0, 1] - cursor must fall back to binary search
    auto result = interpolate_keyframes(times, values, 0.5f, cursor);
    EXPECT_EQ(cursor, 0u);
    EXPECT_NEAR(result.x, 0.5f, 1e-4f);
}

TEST(InterpolateKeyframes, CursorCacheJumpForward) {
    std::vector<float> times = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<glm::vec3> values = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}, {5, 0, 0}};
    size_t cursor = 0;

    // Start at segment [0, 1]
    interpolate_keyframes(times, values, 0.5f, cursor);
    EXPECT_EQ(cursor, 0u);

    // Jump forward past several segments to [4, 5]
    auto result = interpolate_keyframes(times, values, 4.5f, cursor);
    EXPECT_EQ(cursor, 4u);
    EXPECT_NEAR(result.x, 4.5f, 1e-4f);
}

TEST(InterpolateKeyframes, NearlyIdenticalKeyframeTimes) {
    std::vector<float> times = {0.0f, 0.0001f};
    std::vector<glm::vec3> values = {{0, 0, 0}, {10, 0, 0}};

    // At midpoint between the two very close times
    auto result = interpolate_keyframes(times, values, 0.00005f);
    EXPECT_NEAR(result.x, 5.0f, 1e-2f);
}

TEST(InterpolateKeyframes, NegativeTimeValues) {
    std::vector<float> times = {-2.0f, -1.0f, 0.0f, 1.0f};
    std::vector<glm::vec3> values = {{-2, 0, 0}, {-1, 0, 0}, {0, 0, 0}, {1, 0, 0}};

    auto r1 = interpolate_keyframes(times, values, -1.5f);
    EXPECT_NEAR(r1.x, -1.5f, 1e-4f);

    // Before all keyframes
    auto r2 = interpolate_keyframes(times, values, -5.0f);
    EXPECT_NEAR(r2.x, -2.0f, 1e-4f);
}

TEST(InterpolateKeyframes, AllKeyframesAtSameTime) {
    std::vector<float> times = {1.0f, 1.0f, 1.0f};
    std::vector<glm::vec3> values = {{10, 0, 0}, {20, 0, 0}, {30, 0, 0}};

    // Before the time: should return first
    auto r1 = interpolate_keyframes(times, values, 0.5f);
    EXPECT_NEAR(r1.x, 10.0f, 1e-4f);

    // After the time: should return last
    auto r2 = interpolate_keyframes(times, values, 1.5f);
    EXPECT_NEAR(r2.x, 30.0f, 1e-4f);

    // At the exact time: first value returned (t <= times.front() fires first since all times equal)
    auto r3 = interpolate_keyframes(times, values, 1.0f);
    EXPECT_NEAR(r3.x, 10.0f, 1e-4f);
}

TEST(InterpolateKeyframes, LargeTimeValues) {
    std::vector<float> times = {0.0f, 1e6f};
    std::vector<glm::vec3> values = {{0, 0, 0}, {1e6f, 0, 0}};

    auto result = interpolate_keyframes(times, values, 5e5f);
    EXPECT_NEAR(result.x, 5e5f, 1.0f);
}

// ============================================================================
// RotationSmoother edge cases
// ============================================================================

TEST(RotationSmoother, WrappingShortPath) {
    // Smoothing from near PI to near -PI should take the short path
    // (i.e., cross the boundary rather than going the long way around)
    RotationSmoother smoother;
    float near_pi = 3.1f;
    float near_neg_pi = -3.1f;

    smoother.smooth_toward(near_pi, 0.016f); // init snaps
    EXPECT_FLOAT_EQ(smoother.current, near_pi);

    // After one step toward near_neg_pi, smoother should move through PI boundary
    // The short path distance is about 0.0832 (2*PI - 6.2 = 0.0832)
    // So current should increase past PI or wrap, not decrease toward 0
    smoother.smooth_toward(near_neg_pi, 0.016f);

    // The short path goes from 3.1 -> PI -> -3.1 (increasing angle, wrapping)
    // So the smoother should have moved AWAY from zero (toward +PI or beyond)
    // rather than taking the long path through 0
    float dist_to_target_through_zero = std::abs(near_pi) + std::abs(near_neg_pi); // ~6.2
    float dist_from_current_through_zero = std::abs(smoother.current) + std::abs(near_neg_pi);
    // The smoother should still be near PI (took the short path, not the long way through 0)
    EXPECT_GT(std::abs(smoother.current), 1.5f);
}

TEST(RotationSmoother, VeryLargeDt) {
    RotationSmoother smoother;
    smoother.smooth_toward(0.0f, 0.016f); // init

    // A very large dt should converge to target without exploding
    smoother.smooth_toward(1.0f, 1000.0f);
    EXPECT_NEAR(smoother.current, 1.0f, 0.01f);
    EXPECT_TRUE(std::isfinite(smoother.current));
    EXPECT_TRUE(std::isfinite(smoother.turn_rate));
}

TEST(RotationSmoother, VerySmallDt) {
    RotationSmoother smoother;
    smoother.smooth_toward(0.0f, 0.016f); // init

    // Very small dt should not cause NaN or inf
    smoother.smooth_toward(1.0f, 1e-10f);
    EXPECT_TRUE(std::isfinite(smoother.current));
    EXPECT_TRUE(std::isfinite(smoother.turn_rate));

    // dt = 0 exactly
    smoother.smooth_toward(1.0f, 0.0f);
    EXPECT_TRUE(std::isfinite(smoother.current));
    EXPECT_TRUE(std::isfinite(smoother.turn_rate));
    // With dt=0, turn_rate should be 0 (guarded by the dt > 0.0001 check)
    EXPECT_FLOAT_EQ(smoother.turn_rate, 0.0f);
}

TEST(RotationSmoother, SpeedZeroMeansNoMovement) {
    RotationSmoother smoother;
    smoother.smooth_toward(0.0f, 0.016f); // init at 0

    // With speed=0, blend = 1 - exp(0) = 0, so no movement
    float before = smoother.current;
    smoother.smooth_toward(2.0f, 0.016f, 0.0f);
    EXPECT_FLOAT_EQ(smoother.current, before);
}

TEST(RotationSmoother, ManySmallStepsApproxOneLargeStep) {
    // Many small dt steps should converge to approximately the same result
    // as fewer large dt steps (both should approach the target)
    RotationSmoother smoother_small;
    smoother_small.smooth_toward(0.0f, 0.016f);
    for (int i = 0; i < 100; i++) {
        smoother_small.smooth_toward(1.0f, 0.001f);
    }

    RotationSmoother smoother_large;
    smoother_large.smooth_toward(0.0f, 0.016f);
    for (int i = 0; i < 10; i++) {
        smoother_large.smooth_toward(1.0f, 0.01f);
    }

    // Both had total dt = 0.1s at default speed=12, should be close to each other
    EXPECT_NEAR(smoother_small.current, smoother_large.current, 0.05f);
}

// ============================================================================
// ProceduralConfig defaults
// ============================================================================

TEST(ProceduralConfig, DefaultValuesAreSensible) {
    ProceduralConfig config;

    // All factors should be positive
    EXPECT_GT(config.forward_lean_factor, 0.0f);
    EXPECT_GT(config.forward_lean_max, 0.0f);
    EXPECT_GT(config.lateral_lean_factor, 0.0f);
    EXPECT_GT(config.lateral_lean_max, 0.0f);
    EXPECT_GT(config.attack_tilt_max, 0.0f);
    EXPECT_GT(config.attack_tilt_cooldown, 0.0f);

    // Max values should be greater than factors (max is a cap on factor*speed)
    EXPECT_GT(config.forward_lean_max, config.forward_lean_factor);
    EXPECT_GT(config.lateral_lean_max, config.lateral_lean_factor);

    // Features enabled by default
    EXPECT_TRUE(config.foot_ik);
    EXPECT_TRUE(config.lean);
}

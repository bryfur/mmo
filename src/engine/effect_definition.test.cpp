#include "engine/effect_definition.hpp"
#include <cmath>
#include <gtest/gtest.h>

using namespace mmo::engine;

// ===== Curve evaluation tests =====

TEST(CurveEvaluate, ConstantReturnsStartValue) {
    Curve c;
    c.type = CurveType::CONSTANT;
    c.start_value = 42.0f;
    c.end_value = 100.0f;

    EXPECT_FLOAT_EQ(c.evaluate(0.0f), 42.0f);
    EXPECT_FLOAT_EQ(c.evaluate(0.5f), 42.0f);
    EXPECT_FLOAT_EQ(c.evaluate(1.0f), 42.0f);
}

TEST(CurveEvaluate, LinearInterpolates) {
    Curve c;
    c.type = CurveType::LINEAR;
    c.start_value = 0.0f;
    c.end_value = 10.0f;

    EXPECT_FLOAT_EQ(c.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(c.evaluate(0.5f), 5.0f);
    EXPECT_FLOAT_EQ(c.evaluate(1.0f), 10.0f);
    EXPECT_NEAR(c.evaluate(0.25f), 2.5f, 1e-5f);
}

TEST(CurveEvaluate, LinearReverseRange) {
    Curve c;
    c.type = CurveType::LINEAR;
    c.start_value = 100.0f;
    c.end_value = 0.0f;

    EXPECT_FLOAT_EQ(c.evaluate(0.0f), 100.0f);
    EXPECT_FLOAT_EQ(c.evaluate(1.0f), 0.0f);
    EXPECT_NEAR(c.evaluate(0.5f), 50.0f, 1e-5f);
}

TEST(CurveEvaluate, EaseInStartsSlow) {
    Curve c;
    c.type = CurveType::EASE_IN;
    c.start_value = 0.0f;
    c.end_value = 1.0f;

    // At t=0.5, ease-in uses t*t = 0.25, so value should be 0.25.
    EXPECT_FLOAT_EQ(c.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(c.evaluate(1.0f), 1.0f);
    EXPECT_NEAR(c.evaluate(0.5f), 0.25f, 1e-5f);

    // Ease-in should be below linear at any interior point.
    EXPECT_LT(c.evaluate(0.3f), 0.3f);
}

TEST(CurveEvaluate, EaseOutStartsFast) {
    Curve c;
    c.type = CurveType::EASE_OUT;
    c.start_value = 0.0f;
    c.end_value = 1.0f;

    EXPECT_FLOAT_EQ(c.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(c.evaluate(1.0f), 1.0f);

    // At t=0.5: 1 - (1-0.5)^2 = 1 - 0.25 = 0.75
    EXPECT_NEAR(c.evaluate(0.5f), 0.75f, 1e-5f);

    // Ease-out should be above linear at any interior point.
    EXPECT_GT(c.evaluate(0.3f), 0.3f);
}

TEST(CurveEvaluate, EaseInOutSymmetric) {
    Curve c;
    c.type = CurveType::EASE_IN_OUT;
    c.start_value = 0.0f;
    c.end_value = 1.0f;

    EXPECT_FLOAT_EQ(c.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(c.evaluate(1.0f), 1.0f);

    // Midpoint should be exactly 0.5.
    EXPECT_NEAR(c.evaluate(0.5f), 0.5f, 1e-5f);

    // Symmetry: f(t) + f(1-t) = 1
    EXPECT_NEAR(c.evaluate(0.2f) + c.evaluate(0.8f), 1.0f, 1e-5f);
    EXPECT_NEAR(c.evaluate(0.1f) + c.evaluate(0.9f), 1.0f, 1e-5f);
}

TEST(CurveEvaluate, FadeOutLateHoldsUntilFadeStart) {
    Curve c;
    c.type = CurveType::FADE_OUT_LATE;
    c.start_value = 1.0f;
    c.end_value = 0.0f;
    c.fade_start = 0.8f;

    // Before fade_start, holds at start_value.
    EXPECT_FLOAT_EQ(c.evaluate(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(c.evaluate(0.5f), 1.0f);
    EXPECT_FLOAT_EQ(c.evaluate(0.79f), 1.0f);

    // At fade_start, still at start_value.
    EXPECT_FLOAT_EQ(c.evaluate(0.8f), 1.0f);

    // At t=1.0, should reach end_value.
    EXPECT_NEAR(c.evaluate(1.0f), 0.0f, 1e-5f);

    // Midpoint of fade region (0.9): should be 0.5.
    EXPECT_NEAR(c.evaluate(0.9f), 0.5f, 1e-5f);
}

TEST(CurveEvaluate, ClampsInputRange) {
    Curve c;
    c.type = CurveType::LINEAR;
    c.start_value = 0.0f;
    c.end_value = 10.0f;

    // Values outside [0,1] should be clamped.
    EXPECT_FLOAT_EQ(c.evaluate(-1.0f), 0.0f);
    EXPECT_FLOAT_EQ(c.evaluate(2.0f), 10.0f);
}

// ===== Default value tests =====

TEST(CurveDefaults, DefaultIsConstantOne) {
    Curve c;
    EXPECT_EQ(c.type, CurveType::CONSTANT);
    EXPECT_FLOAT_EQ(c.start_value, 1.0f);
    EXPECT_FLOAT_EQ(c.end_value, 1.0f);
    EXPECT_FLOAT_EQ(c.fade_start, 0.8f);
}

TEST(VelocityDefinitionDefaults, Values) {
    VelocityDefinition v;
    EXPECT_EQ(v.type, VelocityType::DIRECTIONAL);
    EXPECT_FLOAT_EQ(v.speed, 100.0f);
    EXPECT_FLOAT_EQ(v.spread_angle, 0.0f);
    EXPECT_FLOAT_EQ(v.drag, 0.0f);
    EXPECT_EQ(v.gravity, glm::vec3(0, 0, 0));
}

TEST(RotationDefinitionDefaults, Values) {
    RotationDefinition r;
    EXPECT_EQ(r.initial_rotation, glm::vec3(0, 0, 0));
    EXPECT_EQ(r.rotation_rate, glm::vec3(0, 0, 0));
    EXPECT_FALSE(r.face_velocity);
}

TEST(AppearanceDefinitionDefaults, Values) {
    AppearanceDefinition a;
    EXPECT_EQ(a.color_tint, glm::vec4(1, 1, 1, 1));
    EXPECT_EQ(a.color_end, glm::vec4(1, 1, 1, 1));
    EXPECT_FALSE(a.use_color_gradient);
}

TEST(EmitterDefinitionDefaults, Values) {
    EmitterDefinition e;
    EXPECT_TRUE(e.name.empty());
    EXPECT_EQ(e.particle_type, "mesh");
    EXPECT_TRUE(e.model.empty());
    EXPECT_EQ(e.spawn_mode, SpawnMode::BURST);
    EXPECT_EQ(e.spawn_count, 1);
    EXPECT_FLOAT_EQ(e.spawn_rate, 10.0f);
    EXPECT_FLOAT_EQ(e.particle_lifetime, 1.0f);
    EXPECT_FLOAT_EQ(e.delay, 0.0f);
    EXPECT_FLOAT_EQ(e.duration, -1.0f);
}

TEST(EffectDefinitionDefaults, Values) {
    EffectDefinition d;
    EXPECT_TRUE(d.name.empty());
    EXPECT_TRUE(d.emitters.empty());
    EXPECT_FLOAT_EQ(d.duration, 1.0f);
    EXPECT_FALSE(d.loop);
    EXPECT_FLOAT_EQ(d.default_range, 100.0f);
}

// ===== Struct size sanity checks =====

TEST(EffectDefinitionLayout, StructSizesReasonable) {
    // These structs should not have unexpected bloat. Exact sizes vary by
    // platform/alignment, but we can assert reasonable upper bounds.
    EXPECT_LE(sizeof(Curve), 32u);
    EXPECT_LE(sizeof(VelocityDefinition), 128u);
    EXPECT_LE(sizeof(RotationDefinition), 64u);
    EXPECT_LE(sizeof(AppearanceDefinition), 128u);
    // EmitterDefinition contains strings and sub-structs, but should be compact.
    EXPECT_LE(sizeof(EmitterDefinition), 512u);
    EXPECT_LE(sizeof(EffectDefinition), 256u);
}

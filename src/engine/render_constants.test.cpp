#include <gtest/gtest.h>
#include "engine/render_constants.hpp"

using namespace mmo::engine;

// ---------------------------------------------------------------------------
// Fog range validity
// ---------------------------------------------------------------------------

TEST(RenderConstants_Fog, StartIsLessThanEnd) {
    EXPECT_LT(fog::START, fog::END);
}

TEST(RenderConstants_Fog, DistantStartIsLessThanDistantEnd) {
    EXPECT_LT(fog::DISTANT_START, fog::DISTANT_END);
}

TEST(RenderConstants_Fog, DistantFogStartsAfterNearFog) {
    // Distant fog should begin at or after normal fog starts
    EXPECT_GE(fog::DISTANT_START, fog::START);
}

TEST(RenderConstants_Fog, FogDistancesMonotonicallyIncreasing) {
    EXPECT_LT(fog::START, fog::END);
    EXPECT_LT(fog::DISTANT_START, fog::DISTANT_END);
    // The four boundaries should be non-decreasing
    EXPECT_LE(fog::START, fog::DISTANT_START);
    EXPECT_LE(fog::END, fog::DISTANT_END);
}

// ---------------------------------------------------------------------------
// Fog color components in [0, 1]
// ---------------------------------------------------------------------------

TEST(RenderConstants_Fog, ColorComponentsInUnitRange) {
    EXPECT_GE(fog::COLOR.r, 0.0f);
    EXPECT_LE(fog::COLOR.r, 1.0f);
    EXPECT_GE(fog::COLOR.g, 0.0f);
    EXPECT_LE(fog::COLOR.g, 1.0f);
    EXPECT_GE(fog::COLOR.b, 0.0f);
    EXPECT_LE(fog::COLOR.b, 1.0f);
}

TEST(RenderConstants_Fog, DistantColorComponentsInUnitRange) {
    EXPECT_GE(fog::DISTANT_COLOR.r, 0.0f);
    EXPECT_LE(fog::DISTANT_COLOR.r, 1.0f);
    EXPECT_GE(fog::DISTANT_COLOR.g, 0.0f);
    EXPECT_LE(fog::DISTANT_COLOR.g, 1.0f);
    EXPECT_GE(fog::DISTANT_COLOR.b, 0.0f);
    EXPECT_LE(fog::DISTANT_COLOR.b, 1.0f);
}

// ---------------------------------------------------------------------------
// Lighting color components are reasonable (non-negative, <= 2.0 for HDR)
// ---------------------------------------------------------------------------

TEST(RenderConstants_Lighting, LightColorComponentsReasonable) {
    EXPECT_GE(lighting::LIGHT_COLOR.r, 0.0f);
    EXPECT_LE(lighting::LIGHT_COLOR.r, 2.0f);
    EXPECT_GE(lighting::LIGHT_COLOR.g, 0.0f);
    EXPECT_LE(lighting::LIGHT_COLOR.g, 2.0f);
    EXPECT_GE(lighting::LIGHT_COLOR.b, 0.0f);
    EXPECT_LE(lighting::LIGHT_COLOR.b, 2.0f);
}

TEST(RenderConstants_Lighting, AmbientColorComponentsReasonable) {
    EXPECT_GE(lighting::AMBIENT_COLOR.r, 0.0f);
    EXPECT_LE(lighting::AMBIENT_COLOR.r, 2.0f);
    EXPECT_GE(lighting::AMBIENT_COLOR.g, 0.0f);
    EXPECT_LE(lighting::AMBIENT_COLOR.g, 2.0f);
    EXPECT_GE(lighting::AMBIENT_COLOR.b, 0.0f);
    EXPECT_LE(lighting::AMBIENT_COLOR.b, 2.0f);
}

TEST(RenderConstants_Lighting, AmbientColorNoFogComponentsReasonable) {
    EXPECT_GE(lighting::AMBIENT_COLOR_NO_FOG.r, 0.0f);
    EXPECT_LE(lighting::AMBIENT_COLOR_NO_FOG.r, 2.0f);
    EXPECT_GE(lighting::AMBIENT_COLOR_NO_FOG.g, 0.0f);
    EXPECT_LE(lighting::AMBIENT_COLOR_NO_FOG.g, 2.0f);
    EXPECT_GE(lighting::AMBIENT_COLOR_NO_FOG.b, 0.0f);
    EXPECT_LE(lighting::AMBIENT_COLOR_NO_FOG.b, 2.0f);
}

// ---------------------------------------------------------------------------
// Fog start values are positive (distances from camera)
// ---------------------------------------------------------------------------

TEST(RenderConstants_Fog, StartDistancesArePositive) {
    EXPECT_GT(fog::START, 0.0f);
    EXPECT_GT(fog::END, 0.0f);
    EXPECT_GT(fog::DISTANT_START, 0.0f);
    EXPECT_GT(fog::DISTANT_END, 0.0f);
}

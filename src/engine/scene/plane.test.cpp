#include <gtest/gtest.h>
#include "engine/scene/frustum.hpp"
#include <glm/glm.hpp>
#include <cmath>

using namespace mmo::engine::scene;

// ---------------------------------------------------------------------------
// normalize() produces a unit-length normal
// ---------------------------------------------------------------------------

TEST(Plane, NormalizeProducesUnitNormal) {
    Plane p{glm::vec3(3.0f, 0.0f, 4.0f), 10.0f};
    p.normalize();
    float len = glm::length(p.normal);
    EXPECT_NEAR(len, 1.0f, 1e-5f);
}

TEST(Plane, NormalizeScalesDistanceProportionally) {
    // normal (3,0,4) has length 5, so distance should be divided by 5
    Plane p{glm::vec3(3.0f, 0.0f, 4.0f), 10.0f};
    p.normalize();
    EXPECT_NEAR(p.distance, 2.0f, 1e-5f);
    EXPECT_NEAR(p.normal.x, 0.6f, 1e-5f);
    EXPECT_NEAR(p.normal.z, 0.8f, 1e-5f);
}

TEST(Plane, NormalizeAlreadyUnitLength) {
    Plane p{glm::vec3(0.0f, 1.0f, 0.0f), -5.0f};
    p.normalize();
    EXPECT_NEAR(glm::length(p.normal), 1.0f, 1e-5f);
    EXPECT_NEAR(p.distance, -5.0f, 1e-5f);
}

TEST(Plane, NormalizeZeroLengthNormalIsHandled) {
    // Zero-length normal should not crash (division by zero guard)
    Plane p{glm::vec3(0.0f, 0.0f, 0.0f), 5.0f};
    p.normalize();
    // Should remain unchanged since length is 0
    EXPECT_FLOAT_EQ(p.distance, 5.0f);
}

// ---------------------------------------------------------------------------
// distance_to_point(): positive for front side, negative for back side
// ---------------------------------------------------------------------------

TEST(Plane, DistanceToPointPositiveForFrontSide) {
    // Plane with normal (0,1,0) and distance 0 is the XZ plane (y=0).
    // Points above (positive y) are on the front side.
    Plane p{glm::vec3(0.0f, 1.0f, 0.0f), 0.0f};
    EXPECT_GT(p.distance_to_point(glm::vec3(0.0f, 5.0f, 0.0f)), 0.0f);
}

TEST(Plane, DistanceToPointNegativeForBackSide) {
    Plane p{glm::vec3(0.0f, 1.0f, 0.0f), 0.0f};
    EXPECT_LT(p.distance_to_point(glm::vec3(0.0f, -3.0f, 0.0f)), 0.0f);
}

TEST(Plane, DistanceToPointZeroForPointOnPlane) {
    Plane p{glm::vec3(0.0f, 1.0f, 0.0f), 0.0f};
    EXPECT_NEAR(p.distance_to_point(glm::vec3(7.0f, 0.0f, -2.0f)), 0.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Plane with normal (0,1,0) and distance -5: represents y=5 plane
// Points above y=5 should be positive, below should be negative.
// ---------------------------------------------------------------------------

TEST(Plane, YEquals5PlaneAboveIsPositive) {
    // The plane equation is: dot(normal, point) + distance = 0
    // For y=5 plane with normal (0,1,0): y + d = 0 when y=5, so d = -5
    Plane p{glm::vec3(0.0f, 1.0f, 0.0f), -5.0f};
    EXPECT_GT(p.distance_to_point(glm::vec3(0.0f, 10.0f, 0.0f)), 0.0f);
    EXPECT_GT(p.distance_to_point(glm::vec3(100.0f, 6.0f, -50.0f)), 0.0f);
}

TEST(Plane, YEquals5PlaneBelowIsNegative) {
    Plane p{glm::vec3(0.0f, 1.0f, 0.0f), -5.0f};
    EXPECT_LT(p.distance_to_point(glm::vec3(0.0f, 0.0f, 0.0f)), 0.0f);
    EXPECT_LT(p.distance_to_point(glm::vec3(0.0f, 4.9f, 0.0f)), 0.0f);
}

TEST(Plane, YEquals5PlaneOnPlaneIsZero) {
    Plane p{glm::vec3(0.0f, 1.0f, 0.0f), -5.0f};
    EXPECT_NEAR(p.distance_to_point(glm::vec3(0.0f, 5.0f, 0.0f)), 0.0f, 1e-5f);
    EXPECT_NEAR(p.distance_to_point(glm::vec3(99.0f, 5.0f, -42.0f)), 0.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// distance_to_point() returns correct signed distance for normalized plane
// ---------------------------------------------------------------------------

TEST(Plane, SignedDistanceAfterNormalize) {
    // Non-unit normal pointing in +X direction, plane at x=3
    // normal (2,0,0), distance -6 -> after normalize: normal (1,0,0), distance -3
    Plane p{glm::vec3(2.0f, 0.0f, 0.0f), -6.0f};
    p.normalize();

    // Point at x=5 should be 2 units in front
    EXPECT_NEAR(p.distance_to_point(glm::vec3(5.0f, 0.0f, 0.0f)), 2.0f, 1e-5f);
    // Point at x=1 should be 2 units behind
    EXPECT_NEAR(p.distance_to_point(glm::vec3(1.0f, 0.0f, 0.0f)), -2.0f, 1e-5f);
    // Point at x=3 should be on the plane
    EXPECT_NEAR(p.distance_to_point(glm::vec3(3.0f, 0.0f, 0.0f)), 0.0f, 1e-5f);
}

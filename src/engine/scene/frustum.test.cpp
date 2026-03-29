#include <gtest/gtest.h>
#include "engine/scene/frustum.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace mmo::engine::scene;

class FrustumTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Standard perspective camera looking down -Z
        glm::mat4 view = glm::lookAt(
            glm::vec3(0, 0, 5),    // camera at Z=5
            glm::vec3(0, 0, 0),    // looking at origin
            glm::vec3(0, 1, 0));   // up
        glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
        vp = proj * view;
        frustum.extract_from_matrix(vp);
    }

    Frustum frustum;
    glm::mat4 vp;
};

TEST_F(FrustumTest, SphereInFrontOfCameraIsVisible) {
    // Sphere at origin, camera at Z=5 looking at origin
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(0, 0, 0), 1.0f));
}

TEST_F(FrustumTest, SphereBehindCameraIsNotVisible) {
    // Sphere behind the camera
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(0, 0, 10), 1.0f));
}

TEST_F(FrustumTest, SphereFarLeftIsNotVisible) {
    // With 90 degree FOV, something at X=100 shouldn't be visible from Z=5
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(100, 0, 0), 1.0f));
}

TEST_F(FrustumTest, SphereFarRightIsNotVisible) {
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(-100, 0, 0), 1.0f));
}

TEST_F(FrustumTest, SphereBeyondFarPlaneIsNotVisible) {
    // Far plane is at 100, sphere at Z=-200 is beyond it
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(0, 0, -200), 1.0f));
}

TEST_F(FrustumTest, LargeSphereAlwaysIntersects) {
    // A very large sphere should intersect regardless of position
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(50, 0, 0), 1000.0f));
}

TEST_F(FrustumTest, SphereAtFrustumEdgeIntersects) {
    // Sphere right at the edge of the frustum with 90 deg FOV
    // At Z=0 (5 units from camera), frustum width is about 5 units each side
    // A sphere at X=5 with radius 1 should still intersect
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(5, 0, 0), 1.0f));
}

TEST_F(FrustumTest, SphereNearFarPlaneIsVisible) {
    // Just inside far plane
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(0, 0, -90), 1.0f));
}

TEST_F(FrustumTest, AABBInFrontIsVisible) {
    EXPECT_TRUE(frustum.intersects_aabb(glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)));
}

TEST_F(FrustumTest, AABBBehindCameraIsNotVisible) {
    EXPECT_FALSE(frustum.intersects_aabb(glm::vec3(-1, -1, 10), glm::vec3(1, 1, 12)));
}

// ============================================================================
// Edge-case sphere tests
// ============================================================================

TEST_F(FrustumTest, ZeroRadiusSphereAtVisiblePointIsVisible) {
    // A point (zero-radius sphere) at the origin should be inside the frustum
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(0, 0, 0), 0.0f));
}

TEST_F(FrustumTest, ZeroRadiusSphereAtHiddenPointIsNotVisible) {
    // A point behind the camera should not be visible
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(0, 0, 20), 0.0f));
}

TEST_F(FrustumTest, SphereExactlyOnNearPlaneBoundary) {
    // Camera at Z=5 looking down -Z. Near plane is 0.1 units in front of camera,
    // so the near plane is at world Z = 5 - 0.1 = 4.9.
    // A sphere centered exactly on the near plane with radius 0 should be visible.
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(0, 0, 4.9f), 0.0f));
}

TEST_F(FrustumTest, SphereExactlyAtCameraPosition) {
    // Camera is at Z=5, looking down -Z. The camera position is behind the near plane,
    // so it's outside the frustum. A zero-radius sphere there should NOT be visible.
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(0, 0, 5), 0.0f));
}

TEST_F(FrustumTest, SphereAtCameraPositionWithNonzeroRadiusIsVisible) {
    // Camera at Z=5. Near plane at Z=4.9. A sphere at Z=5 with radius > 0.1 crosses
    // the near plane, so it should be visible.
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(0, 0, 5), 1.0f));
}

// ============================================================================
// Narrow FOV frustum
// ============================================================================

class NarrowFOVFrustumTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Very narrow 5-degree FOV looking down -Z
        glm::mat4 view = glm::lookAt(
            glm::vec3(0, 0, 10),
            glm::vec3(0, 0, 0),
            glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(5.0f), 1.0f, 0.1f, 100.0f);
        frustum.extract_from_matrix(proj * view);
    }

    Frustum frustum;
};

TEST_F(NarrowFOVFrustumTest, ObjectOnCenterAxisIsVisible) {
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(0, 0, 0), 0.5f));
}

TEST_F(NarrowFOVFrustumTest, ObjectSlightlyOffCenterIsCulled) {
    // With 5-degree FOV at distance 10, half-width at origin is ~10 * tan(2.5 deg) ~ 0.44
    // A small sphere at X=2 should be well outside the frustum
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(2, 0, 0), 0.1f));
}

TEST_F(NarrowFOVFrustumTest, ObjectAboveIsCulled) {
    // Similarly, Y=2 should be outside the narrow cone
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(0, 2, 0), 0.1f));
}

// ============================================================================
// Orthographic projection frustum (shadow maps)
// ============================================================================

class OrthoFrustumTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Orthographic projection: 20x20 box, near=1, far=50, looking down -Z from Z=30
        glm::mat4 view = glm::lookAt(
            glm::vec3(0, 0, 30),
            glm::vec3(0, 0, 0),
            glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 50.0f);
        frustum.extract_from_matrix(proj * view);
    }

    Frustum frustum;
};

TEST_F(OrthoFrustumTest, ObjectInsideBoxIsVisible) {
    EXPECT_TRUE(frustum.intersects_sphere(glm::vec3(0, 0, 0), 1.0f));
}

TEST_F(OrthoFrustumTest, ObjectOutsideLeftIsNotVisible) {
    // Box extends from -10 to 10 in X. Sphere at X=-15 with radius 1 is outside.
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(-15, 0, 0), 1.0f));
}

TEST_F(OrthoFrustumTest, ObjectOutsideRightIsNotVisible) {
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(15, 0, 0), 1.0f));
}

TEST_F(OrthoFrustumTest, ObjectBeyondFarPlaneIsNotVisible) {
    // Camera at Z=30, far=50, so far plane is at Z=30-50=-20. Object at Z=-25 is beyond.
    EXPECT_FALSE(frustum.intersects_sphere(glm::vec3(0, 0, -25), 1.0f));
}

TEST_F(OrthoFrustumTest, AABBInsideOrthoIsVisible) {
    EXPECT_TRUE(frustum.intersects_aabb(glm::vec3(-5, -5, -5), glm::vec3(5, 5, 5)));
}

TEST_F(OrthoFrustumTest, AABBOutsideOrthoIsNotVisible) {
    // Entirely to the right of the box
    EXPECT_FALSE(frustum.intersects_aabb(glm::vec3(12, -1, -1), glm::vec3(14, 1, 1)));
}

// ============================================================================
// AABB edge cases
// ============================================================================

TEST_F(FrustumTest, AABBSpanningEntireFrustumIsVisible) {
    // A huge AABB that encompasses the entire frustum should be visible
    EXPECT_TRUE(frustum.intersects_aabb(glm::vec3(-1000, -1000, -1000), glm::vec3(1000, 1000, 1000)));
}

TEST_F(FrustumTest, AABBEntirelyToOneSideIsNotVisible) {
    // Entirely far to the right (positive X), well outside the 90-degree FOV from Z=5
    EXPECT_FALSE(frustum.intersects_aabb(glm::vec3(200, -1, -1), glm::vec3(202, 1, 1)));
}

TEST_F(FrustumTest, DegenerateAABBPointAtVisibleLocationIsVisible) {
    // AABB where min == max (a point) at the origin, which is in front of the camera
    glm::vec3 point(0, 0, 0);
    EXPECT_TRUE(frustum.intersects_aabb(point, point));
}

TEST_F(FrustumTest, DegenerateAABBPointBehindCameraIsNotVisible) {
    glm::vec3 point(0, 0, 20);
    EXPECT_FALSE(frustum.intersects_aabb(point, point));
}

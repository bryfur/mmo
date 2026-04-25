#include "client/hud/world_projection.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

namespace mmo::client::hud {
namespace {

constexpr float kScreenW = 1280.0f;
constexpr float kScreenH = 720.0f;

// Build a standard pinhole VP matrix looking down -Z from origin.
glm::mat4 default_vp() {
    const glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 0.0f),  // eye
        glm::vec3(0.0f, 0.0f, -1.0f), // forward
        glm::vec3(0.0f, 1.0f, 0.0f)); // up
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                             kScreenW / kScreenH, 0.1f, 1000.0f);
    return proj * view;
}

TEST(WorldToScreen, PointInFrontProjectsOnScreen) {
    auto p = world_to_screen(default_vp(), glm::vec3(0.0f, 0.0f, -10.0f),
                             kScreenW, kScreenH);
    ASSERT_TRUE(p.has_value());
    // Center of view: should land near (640, 360).
    EXPECT_NEAR(p->x, kScreenW * 0.5f, 0.5f);
    EXPECT_NEAR(p->y, kScreenH * 0.5f, 0.5f);
}

TEST(WorldToScreen, PointBehindCameraReturnsNullopt) {
    auto p = world_to_screen(default_vp(), glm::vec3(0.0f, 0.0f, 10.0f),
                             kScreenW, kScreenH);
    EXPECT_FALSE(p.has_value());
}

TEST(WorldToScreen, RightOfCameraGoesRightOnScreen) {
    auto p = world_to_screen(default_vp(), glm::vec3(2.0f, 0.0f, -10.0f),
                             kScreenW, kScreenH);
    ASSERT_TRUE(p.has_value());
    EXPECT_GT(p->x, kScreenW * 0.5f);
}

TEST(WorldToScreen, AboveCameraGoesUpOnScreen) {
    // Y up in world space; smaller screen Y is higher on screen.
    auto p = world_to_screen(default_vp(), glm::vec3(0.0f, 2.0f, -10.0f),
                             kScreenW, kScreenH);
    ASSERT_TRUE(p.has_value());
    EXPECT_LT(p->y, kScreenH * 0.5f);
}

TEST(WorldToScreen, FartherPointsConverge) {
    // Two points at the same world X, one farther away. The farther one
    // should be closer to screen center (x=640) than the nearer one.
    auto near_p = world_to_screen(default_vp(), glm::vec3(2.0f, 0.0f, -5.0f),
                                  kScreenW, kScreenH);
    auto far_p = world_to_screen(default_vp(), glm::vec3(2.0f, 0.0f, -50.0f),
                                 kScreenW, kScreenH);
    ASSERT_TRUE(near_p.has_value());
    ASSERT_TRUE(far_p.has_value());
    EXPECT_LT(far_p->x - kScreenW * 0.5f, near_p->x - kScreenW * 0.5f);
}

TEST(DistanceScale, NearReturnsOne) {
    EXPECT_FLOAT_EQ(distance_scale(0.0f), 1.0f);
}

TEST(DistanceScale, FarClampsToFloor) {
    EXPECT_FLOAT_EQ(distance_scale(10000.0f), 0.6f);
}

TEST(DistanceScale, MidValueLerps) {
    // 1.0 - 200/1000 = 0.8
    EXPECT_FLOAT_EQ(distance_scale(200.0f), 0.8f);
}

TEST(DistanceScale, RespectsCustomFloor) {
    EXPECT_FLOAT_EQ(distance_scale(10000.0f, 1000.0f, 0.25f), 0.25f);
}

TEST(DistanceScale, ZeroFalloffSafe) {
    EXPECT_FLOAT_EQ(distance_scale(100.0f, 0.0f, 0.5f), 0.5f);
}

} // namespace
} // namespace mmo::client::hud

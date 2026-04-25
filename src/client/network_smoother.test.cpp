#include "client/network_smoother.hpp"
#include "client/ecs/components.hpp"

#include <gtest/gtest.h>

namespace mmo::client {
namespace {

constexpr float kTickTime = 1.0f / 60.0f;

ecs::Interpolation make_interp(float prev, float target, float alpha = 0.0f) {
    ecs::Interpolation i;
    i.prev_x = prev;
    i.prev_y = prev;
    i.prev_z = prev;
    i.target_x = target;
    i.target_y = target;
    i.target_z = target;
    i.alpha = alpha;
    return i;
}

TEST(NetworkSmoother, AlphaAdvancesByDtOverInterpolationTime) {
    ecs::Transform t{};
    auto i = make_interp(0.0f, 10.0f);

    smooth_step(t, i, kTickTime * 0.5f, kTickTime);

    EXPECT_FLOAT_EQ(i.alpha, 0.5f);
}

TEST(NetworkSmoother, AlphaClampsToOneOnLargeDt) {
    ecs::Transform t{};
    auto i = make_interp(0.0f, 10.0f);

    smooth_step(t, i, 1.0f, kTickTime);

    EXPECT_FLOAT_EQ(i.alpha, 1.0f);
}

TEST(NetworkSmoother, EndpointsAreExact) {
    ecs::Transform t{};

    // Alpha 0 -> exactly prev
    auto i0 = make_interp(2.0f, 8.0f, 0.0f);
    smooth_step(t, i0, 0.0f, kTickTime);
    EXPECT_FLOAT_EQ(t.x, 2.0f);
    EXPECT_FLOAT_EQ(t.y, 2.0f);
    EXPECT_FLOAT_EQ(t.z, 2.0f);

    // After alpha clamps to 1 -> exactly target (snap-to-target branch)
    auto i1 = make_interp(2.0f, 8.0f, 0.99f);
    smooth_step(t, i1, kTickTime, kTickTime);
    EXPECT_FLOAT_EQ(t.x, 8.0f);
    EXPECT_FLOAT_EQ(t.y, 8.0f);
    EXPECT_FLOAT_EQ(t.z, 8.0f);
}

TEST(NetworkSmoother, SmoothstepMidpoint) {
    // t=0.5 -> smooth_t = 0.5; midpoint of [0,10] is 5.
    ecs::Transform t{};
    auto i = make_interp(0.0f, 10.0f, 0.0f);
    smooth_step(t, i, kTickTime * 0.5f, kTickTime);

    EXPECT_FLOAT_EQ(t.x, 5.0f);
}

TEST(NetworkSmoother, SmoothstepEasesNotLinear) {
    // At alpha=0.25, smoothstep yields 3*(0.25)^2 - 2*(0.25)^3 = 0.15625
    // (linear would be 0.25). Confirm we're using the eased curve, not linear.
    ecs::Transform t{};
    auto i = make_interp(0.0f, 100.0f, 0.0f);
    smooth_step(t, i, kTickTime * 0.25f, kTickTime);

    EXPECT_NEAR(t.x, 15.625f, 1e-4f);
    EXPECT_LT(t.x, 25.0f) << "smoothstep must lag linear at low alpha";
}

TEST(NetworkSmoother, SnapAtCompletionUpdatesPrevToTarget) {
    // After completion, prev_* must equal target_* so the next snapshot
    // (which sets a new target) interpolates from the correct origin.
    ecs::Transform t{};
    auto i = make_interp(0.0f, 10.0f, 0.99f);
    smooth_step(t, i, kTickTime, kTickTime);

    EXPECT_FLOAT_EQ(i.prev_x, 10.0f);
    EXPECT_FLOAT_EQ(i.prev_y, 10.0f);
    EXPECT_FLOAT_EQ(i.prev_z, 10.0f);
}

TEST(NetworkSmoother, ZeroInterpolationTimeSnaps) {
    // Misconfiguration guard: an interpolation_time of 0 must not divide by
    // zero — it should snap to target instead.
    ecs::Transform t{};
    auto i = make_interp(0.0f, 7.0f, 0.0f);
    smooth_step(t, i, 0.016f, 0.0f);

    EXPECT_FLOAT_EQ(t.x, 7.0f);
    EXPECT_FLOAT_EQ(i.alpha, 1.0f);
}

TEST(NetworkSmoother, IndependentAxes) {
    // Each axis must interpolate independently.
    ecs::Transform t{};
    ecs::Interpolation i;
    i.prev_x = 0.0f;
    i.target_x = 100.0f;
    i.prev_y = 50.0f;
    i.target_y = 50.0f; // No change on Y
    i.prev_z = -20.0f;
    i.target_z = -10.0f;
    i.alpha = 0.0f;

    smooth_step(t, i, kTickTime * 0.5f, kTickTime);

    EXPECT_FLOAT_EQ(t.x, 50.0f);
    EXPECT_FLOAT_EQ(t.y, 50.0f);
    EXPECT_FLOAT_EQ(t.z, -15.0f);
}

TEST(NetworkSmoother, RegistryUpdateAdvancesAllEntities) {
    entt::registry registry;
    auto e1 = registry.create();
    auto e2 = registry.create();
    registry.emplace<ecs::Transform>(e1);
    registry.emplace<ecs::Interpolation>(e1, make_interp(0.0f, 10.0f));
    registry.emplace<ecs::Transform>(e2);
    registry.emplace<ecs::Interpolation>(e2, make_interp(100.0f, 0.0f));

    NetworkSmoother smoother;
    smoother.set_interpolation_time(kTickTime);
    smoother.update(registry, kTickTime * 0.5f);

    EXPECT_FLOAT_EQ(registry.get<ecs::Transform>(e1).x, 5.0f);
    EXPECT_FLOAT_EQ(registry.get<ecs::Transform>(e2).x, 50.0f);
}

} // namespace
} // namespace mmo::client

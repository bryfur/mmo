#include "client/hud/hud_layout.hpp"

#include <gtest/gtest.h>

namespace mmo::client::hud_layout {
namespace {

MinimapView default_view() {
    MinimapView v;
    v.center_x = 100.0f;
    v.center_y = 100.0f;
    v.map_radius = 90.0f;
    v.world_radius = 1000.0f;
    v.player_world_x = 0.0f;
    v.player_world_z = 0.0f;
    v.bound_inset = 3.0f;
    return v;
}

TEST(MinimapProjection, PlayerOriginMapsToCenter) {
    const auto v = default_view();
    auto p = world_to_minimap(v, v.player_world_x, v.player_world_z);
    EXPECT_FLOAT_EQ(p.x, v.center_x);
    EXPECT_FLOAT_EQ(p.y, v.center_y);
    EXPECT_TRUE(p.in_bounds);
}

TEST(MinimapProjection, ScaleHonorsWorldRadius) {
    auto v = default_view();
    // 500 world units = half the world_radius => half map_radius pixels offset.
    auto p = world_to_minimap(v, 500.0f, 0.0f);
    EXPECT_FLOAT_EQ(p.x, v.center_x + v.map_radius * 0.5f);
    EXPECT_FLOAT_EQ(p.y, v.center_y);
    EXPECT_TRUE(p.in_bounds);
}

TEST(MinimapProjection, OutsideRadiusIsOutOfBounds) {
    auto v = default_view();
    auto p = world_to_minimap(v, 2000.0f, 0.0f); // 2x world_radius
    EXPECT_FALSE(p.in_bounds);
}

TEST(MinimapProjection, BoundInsetExcludesEdge) {
    auto v = default_view();
    // Pick a point exactly on the disc edge; bound_inset must reject it.
    auto p = world_to_minimap(v, v.world_radius, 0.0f);
    EXPECT_FALSE(p.in_bounds);
}

TEST(MinimapProjection, NegativeAxesProjectThroughCenter) {
    auto v = default_view();
    auto p = world_to_minimap(v, -500.0f, -500.0f);
    EXPECT_FLOAT_EQ(p.x, v.center_x - v.map_radius * 0.5f);
    EXPECT_FLOAT_EQ(p.y, v.center_y - v.map_radius * 0.5f);
}

TEST(MinimapProjection, FollowsPlayerPosition) {
    auto v = default_view();
    v.player_world_x = 100.0f;
    v.player_world_z = -50.0f;

    // Same world point as before now lands at (target - player) offset.
    auto p = world_to_minimap(v, 100.0f, -50.0f);
    EXPECT_FLOAT_EQ(p.x, v.center_x);
    EXPECT_FLOAT_EQ(p.y, v.center_y);
}

TEST(MinimapProjection, ZeroWorldRadiusIsSafe) {
    auto v = default_view();
    v.world_radius = 0.0f;
    auto p = world_to_minimap(v, 100.0f, 100.0f);
    // No divide-by-zero, no NaN — should pin to center and report OOB.
    EXPECT_FLOAT_EQ(p.x, v.center_x);
    EXPECT_FLOAT_EQ(p.y, v.center_y);
    EXPECT_FALSE(p.in_bounds);
}

TEST(MinimapAreaRadius, ClampedAtBothEnds) {
    EXPECT_FLOAT_EQ(minimap_area_pixel_radius(1.0f, 1000.0f, 90.0f), 4.0f);     // tiny -> min
    EXPECT_FLOAT_EQ(minimap_area_pixel_radius(5000.0f, 1000.0f, 90.0f), 45.0f); // huge -> half pixel radius
    // Mid value: 100/1000 * 90 = 9
    EXPECT_FLOAT_EQ(minimap_area_pixel_radius(100.0f, 1000.0f, 90.0f), 9.0f);
}

TEST(BarRatio, ZeroMaxIsZero) {
    EXPECT_FLOAT_EQ(bar_ratio(50.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(bar_ratio(0.0f, 0.0f), 0.0f);
}

TEST(BarRatio, ClampsToOneAndZero) {
    EXPECT_FLOAT_EQ(bar_ratio(-5.0f, 100.0f), 0.0f);
    EXPECT_FLOAT_EQ(bar_ratio(150.0f, 100.0f), 1.0f);
    EXPECT_FLOAT_EQ(bar_ratio(50.0f, 100.0f), 0.5f);
}

TEST(HealthBarColor, ThresholdsAtThirtyAndSixtyPercent) {
    EXPECT_EQ(health_bar_color(0.0f), 0xFF0000CCu);  // red
    EXPECT_EQ(health_bar_color(0.29f), 0xFF0000CCu); // red
    EXPECT_EQ(health_bar_color(0.30f), 0xFF00AAFFu); // orange (>=30)
    EXPECT_EQ(health_bar_color(0.59f), 0xFF00AAFFu); // orange
    EXPECT_EQ(health_bar_color(0.60f), 0xFF0000FFu); // green (>=60)
    EXPECT_EQ(health_bar_color(1.00f), 0xFF0000FFu);
}

TEST(FadeColor, OpaqueAndTransparentEdgeCases) {
    EXPECT_EQ(fade_color(0xFF112233u, 1.0f), 0xFF112233u);
    EXPECT_EQ(fade_color(0xFF112233u, 0.0f), 0x00112233u);
    EXPECT_EQ(fade_color(0xFF112233u, -0.5f), 0x00112233u);
    EXPECT_EQ(fade_color(0xFF112233u, 2.0f), 0xFF112233u);
}

TEST(FadeColor, AlphaScalesHighByte) {
    // 0xFF * 0.5 = 127 -> 0x7F in the alpha byte.
    EXPECT_EQ(fade_color(0xFF112233u, 0.5f) & 0xFF000000u, 0x7F000000u);
    // RGB channels untouched.
    EXPECT_EQ(fade_color(0xFF112233u, 0.5f) & 0x00FFFFFFu, 0x00112233u);
}

TEST(LinearFade, RemainsOpaqueAboveDuration) {
    EXPECT_FLOAT_EQ(linear_fade(2.0f, 1.5f), 1.0f);
    EXPECT_FLOAT_EQ(linear_fade(1.5f, 1.5f), 1.0f);
}

TEST(LinearFade, FadesLinearlyBelowDuration) {
    EXPECT_FLOAT_EQ(linear_fade(0.75f, 1.5f), 0.5f);
    EXPECT_FLOAT_EQ(linear_fade(0.0f, 1.5f), 0.0f);
    EXPECT_FLOAT_EQ(linear_fade(-1.0f, 1.5f), 0.0f);
}

TEST(TruncateWithEllipsis, ShortStringPassesThrough) {
    EXPECT_EQ(truncate_with_ellipsis("hello", 10), "hello");
    EXPECT_EQ(truncate_with_ellipsis("hello", 5), "hello"); // exactly fits
}

TEST(TruncateWithEllipsis, LongStringTruncatedWithMarker) {
    EXPECT_EQ(truncate_with_ellipsis("Long Skill Name", 7), "Long S~");
    EXPECT_EQ(truncate_with_ellipsis("Defeat 10 Spider Hatchlings", 28, '~'),
              "Defeat 10 Spider Hatchlings"); // 27 chars fits in 28
    EXPECT_EQ(truncate_with_ellipsis("Defeat 100 Spider Hatchlings now", 28, '~'), "Defeat 100 Spider Hatchling~");
}

TEST(TruncateWithEllipsis, ZeroMaxYieldsEmpty) {
    EXPECT_EQ(truncate_with_ellipsis("anything", 0), "");
}

TEST(TruncateWithEllipsis, CustomEllipsisChar) {
    EXPECT_EQ(truncate_with_ellipsis("Very long text", 6, '.'), "Very ."); // 5 chars + '.'
}

} // namespace
} // namespace mmo::client::hud_layout

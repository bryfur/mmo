#include "client/systems/npc_interaction.hpp"
#include "client/ecs/components.hpp"

#include <gtest/gtest.h>

namespace mmo::client::systems {
namespace {

using protocol::EntityType;

entt::entity make_npc(entt::registry& registry, uint32_t id, const char* name,
                      float x, float z, EntityType type = EntityType::TownNPC) {
    auto e = registry.create();
    registry.emplace<ecs::NetworkId>(e, ecs::NetworkId{id});
    ecs::Transform tf{};
    tf.x = x;
    tf.z = z;
    registry.emplace<ecs::Transform>(e, tf);
    ecs::EntityInfo info{};
    info.type = type;
    registry.emplace<ecs::EntityInfo>(e, info);
    ecs::Name n;
    n.value = name;
    registry.emplace<ecs::Name>(e, n);
    return e;
}

TEST(FindClosestNPC, ReturnsEmptyWhenRegistryEmpty) {
    entt::registry registry;
    EXPECT_FALSE(find_closest_npc(registry, 0.0f, 0.0f, 200.0f).has_value());
}

TEST(FindClosestNPC, IgnoresNonTownNPCs) {
    entt::registry registry;
    make_npc(registry, 1, "Wolf",   1.0f, 1.0f, EntityType::NPC);          // hostile
    make_npc(registry, 2, "Tree",   2.0f, 2.0f, EntityType::Environment);
    make_npc(registry, 3, "Player", 3.0f, 3.0f, EntityType::Player);
    make_npc(registry, 4, "House",  4.0f, 4.0f, EntityType::Building);
    EXPECT_FALSE(find_closest_npc(registry, 0.0f, 0.0f, 200.0f).has_value());
}

TEST(FindClosestNPC, PicksNearestWithinRange) {
    entt::registry registry;
    make_npc(registry, 10, "Far",   100.0f, 0.0f);
    make_npc(registry, 11, "Mid",    50.0f, 0.0f);
    make_npc(registry, 12, "Near",   10.0f, 0.0f);

    auto best = find_closest_npc(registry, 0.0f, 0.0f, 200.0f);
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->network_id, 12u);
    EXPECT_EQ(best->name, "Near");
    EXPECT_FLOAT_EQ(best->distance, 10.0f);
}

TEST(FindClosestNPC, RespectsMaxDistance) {
    entt::registry registry;
    make_npc(registry, 1, "Far", 250.0f, 0.0f);

    EXPECT_FALSE(find_closest_npc(registry, 0.0f, 0.0f, 200.0f).has_value());
    EXPECT_TRUE(find_closest_npc(registry, 0.0f, 0.0f, 300.0f).has_value());
}

TEST(FindClosestNPC, MeasuresPlanarDistanceOnly) {
    // Y/height is irrelevant — distance is computed in the X/Z plane.
    entt::registry registry;
    auto e = make_npc(registry, 1, "Above", 3.0f, 4.0f);
    registry.get<ecs::Transform>(e).y = 9999.0f;

    auto best = find_closest_npc(registry, 0.0f, 0.0f, 100.0f);
    ASSERT_TRUE(best.has_value());
    EXPECT_FLOAT_EQ(best->distance, 5.0f);  // 3-4-5 triangle
}

TEST(FindClosestNPC, FromOffsetPlayer) {
    entt::registry registry;
    make_npc(registry, 1, "A", 100.0f, 100.0f);
    make_npc(registry, 2, "B",  90.0f, 100.0f);

    // Player at (95, _, 100) — B is closer.
    auto best = find_closest_npc(registry, 95.0f, 100.0f, 50.0f);
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->network_id, 2u);
    EXPECT_FLOAT_EQ(best->distance, 5.0f);
}

TEST(FindClosestNPC, TiesPickOneOfTheTied) {
    // Two NPCs at exactly the same distance: either is a correct answer.
    // entt's view iteration order isn't part of the API contract, so we
    // just assert one of the two equidistant candidates won.
    entt::registry registry;
    make_npc(registry, 1, "First",  10.0f, 0.0f);
    make_npc(registry, 2, "Second", 10.0f, 0.0f);

    auto best = find_closest_npc(registry, 0.0f, 0.0f, 100.0f);
    ASSERT_TRUE(best.has_value());
    EXPECT_TRUE(best->network_id == 1u || best->network_id == 2u);
    EXPECT_FLOAT_EQ(best->distance, 10.0f);
}

} // namespace
} // namespace mmo::client::systems

#include "client/systems/minimap_system.hpp"
#include "client/ecs/components.hpp"

#include <gtest/gtest.h>

namespace mmo::client::systems {
namespace {

using protocol::EntityType;

entt::entity make_entity(entt::registry& r, uint32_t nid, EntityType type, float x, float z) {
    auto e = r.create();
    r.emplace<ecs::NetworkId>(e, ecs::NetworkId{nid});
    ecs::Transform tf{};
    tf.x = x;
    tf.z = z;
    r.emplace<ecs::Transform>(e, tf);
    ecs::EntityInfo info{};
    info.type = type;
    r.emplace<ecs::EntityInfo>(e, info);
    return e;
}

TEST(MinimapColor, KnownTypesGetColors) {
    EXPECT_EQ(*minimap_color_for(EntityType::TownNPC), 0xFF00CC00u);
    EXPECT_EQ(*minimap_color_for(EntityType::NPC), 0xFF0000FFu);
    EXPECT_EQ(*minimap_color_for(EntityType::Player), 0xFFFFFF00u);
    EXPECT_EQ(*minimap_color_for(EntityType::Building), 0xFF888888u);
}

TEST(MinimapColor, EnvironmentIsFiltered) {
    EXPECT_FALSE(minimap_color_for(EntityType::Environment).has_value());
}

TEST(UpdateMinimap, ClearsIconsWhenPlayerMissing) {
    entt::registry r;
    HUDState hud;
    PanelState panel;
    hud.minimap.icons.push_back({});
    hud.minimap.objective_areas.push_back({});

    update_minimap(r, hud, panel, entt::null, 0, 1000.0f);

    EXPECT_TRUE(hud.minimap.icons.empty());
    EXPECT_TRUE(hud.minimap.objective_areas.empty());
}

TEST(UpdateMinimap, SnapshotsPlayerPositionToHudAndPanel) {
    entt::registry r;
    HUDState hud;
    PanelState panel;
    auto local = make_entity(r, 1, EntityType::Player, 42.0f, -7.5f);

    update_minimap(r, hud, panel, local, 1, 1000.0f);

    EXPECT_FLOAT_EQ(hud.minimap.player_x, 42.0f);
    EXPECT_FLOAT_EQ(hud.minimap.player_z, -7.5f);
    EXPECT_FLOAT_EQ(panel.player_x, 42.0f);
    EXPECT_FLOAT_EQ(panel.player_z, -7.5f);
}

TEST(UpdateMinimap, ExcludesLocalPlayerIcon) {
    entt::registry r;
    HUDState hud;
    PanelState panel;
    auto local = make_entity(r, 1, EntityType::Player, 0.0f, 0.0f);
    make_entity(r, 2, EntityType::Player, 10.0f, 10.0f); // remote player

    update_minimap(r, hud, panel, local, 1, 1000.0f);

    ASSERT_EQ(hud.minimap.icons.size(), 1u);
    EXPECT_FLOAT_EQ(hud.minimap.icons[0].world_x, 10.0f);
}

TEST(UpdateMinimap, DistanceCullsFarEntities) {
    entt::registry r;
    HUDState hud;
    PanelState panel;
    auto local = make_entity(r, 1, EntityType::Player, 0.0f, 0.0f);
    make_entity(r, 2, EntityType::TownNPC, 500.0f, 0.0f);  // inside radius
    make_entity(r, 3, EntityType::TownNPC, 1500.0f, 0.0f); // outside radius

    update_minimap(r, hud, panel, local, 1, 1000.0f);

    ASSERT_EQ(hud.minimap.icons.size(), 1u);
    EXPECT_FLOAT_EQ(hud.minimap.icons[0].world_x, 500.0f);
    EXPECT_EQ(hud.minimap.icons[0].color, 0xFF00CC00u);
}

TEST(UpdateMinimap, FilteredTypesDoNotProduceIcons) {
    entt::registry r;
    HUDState hud;
    PanelState panel;
    auto local = make_entity(r, 1, EntityType::Player, 0.0f, 0.0f);
    make_entity(r, 2, EntityType::Environment, 100.0f, 0.0f);

    update_minimap(r, hud, panel, local, 1, 1000.0f);

    EXPECT_TRUE(hud.minimap.icons.empty());
}

TEST(UpdateMinimap, ClearsObjectiveAreasEachCall) {
    entt::registry r;
    HUDState hud;
    PanelState panel;
    auto local = make_entity(r, 1, EntityType::Player, 0.0f, 0.0f);
    hud.minimap.objective_areas.push_back({});

    update_minimap(r, hud, panel, local, 1, 1000.0f);

    EXPECT_TRUE(hud.minimap.objective_areas.empty());
}

} // namespace
} // namespace mmo::client::systems

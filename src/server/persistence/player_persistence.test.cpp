#include <gtest/gtest.h>
#include "player_persistence.hpp"
#include "../ecs/game_components.hpp"

#include <entt/entt.hpp>

using namespace mmo::server::persistence;
namespace ecs = mmo::server::ecs;

namespace {

// Build an entity that mirrors what World::add_player creates.
entt::entity make_player_entity(entt::registry& reg, uint8_t player_class = 1) {
    auto e = reg.create();
    reg.emplace<ecs::PlayerTag>(e);
    reg.emplace<ecs::Transform>(e, ecs::Transform{4000.0f, 50.0f, 4000.0f, 0.0f});
    reg.emplace<ecs::Velocity>(e);
    reg.emplace<ecs::Health>(e, 100.0f, 100.0f);
    reg.emplace<ecs::Combat>(e, 10.0f, 100.0f, 1.0f, 0.0f, false);

    ecs::EntityInfo info;
    info.player_class = player_class;
    reg.emplace<ecs::EntityInfo>(e, info);

    reg.emplace<ecs::Name>(e, ecs::Name{"alice"});
    reg.emplace<ecs::PlayerLevel>(e);
    reg.emplace<ecs::Inventory>(e);
    reg.emplace<ecs::Equipment>(e);
    reg.emplace<ecs::QuestState>(e);
    reg.emplace<ecs::TalentState>(e);
    return e;
}

} // namespace

TEST(PlayerPersistence, EmptyEntityRoundTrips) {
    entt::registry reg;
    auto e = make_player_entity(reg);

    auto snap = snapshot_from_entity(reg, e, "alice");
    EXPECT_EQ(snap.name, "alice");
    EXPECT_EQ(snap.player_class, 1);
    EXPECT_EQ(snap.level, 1);
    EXPECT_EQ(snap.xp, 0);
    EXPECT_EQ(snap.gold, 0);
    EXPECT_FLOAT_EQ(snap.pos_x, 4000.0f);
    EXPECT_FLOAT_EQ(snap.pos_y, 50.0f);
    EXPECT_FLOAT_EQ(snap.pos_z, 4000.0f);
    EXPECT_FLOAT_EQ(snap.health, 100.0f);
    EXPECT_FLOAT_EQ(snap.max_health, 100.0f);
    EXPECT_TRUE(snap.inventory.empty());
    EXPECT_TRUE(snap.unlocked_talents.empty());
}

TEST(PlayerPersistence, SnapshotCapturesPopulatedComponents) {
    entt::registry reg;
    auto e = make_player_entity(reg);

    {
        auto& tx = reg.get<ecs::Transform>(e);
        tx.x = 1.0f; tx.y = 2.0f; tx.z = 3.0f; tx.rotation = 1.5f;
    }
    {
        auto& hp = reg.get<ecs::Health>(e);
        hp.current = 75.0f; hp.max = 200.0f;
    }
    {
        auto& lvl = reg.get<ecs::PlayerLevel>(e);
        lvl.level = 10; lvl.xp = 1234; lvl.gold = 999;
        lvl.mana = 50.0f; lvl.max_mana = 80.0f; lvl.mana_regen = 1.5f;
    }
    {
        auto& inv = reg.get<ecs::Inventory>(e);
        inv.add_item("potion", 5);
        inv.add_item("sword", 1);
    }
    {
        auto& eq = reg.get<ecs::Equipment>(e);
        eq.weapon_id = "iron_sword";
        eq.armor_id = "leather_armor";
    }
    {
        auto& ts = reg.get<ecs::TalentState>(e);
        ts.talent_points = 3;
        ts.unlocked_talents = {"a", "b", "c"};
    }
    {
        auto& qs = reg.get<ecs::QuestState>(e);
        qs.completed_quests = {"q1", "q2"};
        ecs::ActiveQuest aq;
        aq.quest_id = "q3";
        ecs::QuestObjectiveProgress obj;
        obj.type = "kill"; obj.target = "goblin";
        obj.current = 2; obj.required = 5; obj.complete = false;
        aq.objectives.push_back(obj);
        aq.all_complete = false;
        qs.active_quests.push_back(aq);
    }

    auto snap = snapshot_from_entity(reg, e, "alice");

    EXPECT_FLOAT_EQ(snap.pos_x, 1.0f);
    EXPECT_FLOAT_EQ(snap.health, 75.0f);
    EXPECT_FLOAT_EQ(snap.max_health, 200.0f);
    EXPECT_EQ(snap.level, 10);
    EXPECT_EQ(snap.xp, 1234);
    EXPECT_EQ(snap.gold, 999);
    EXPECT_FLOAT_EQ(snap.mana, 50.0f);
    ASSERT_EQ(snap.inventory.size(), 2u);
    EXPECT_EQ(snap.inventory[0].item_id, "potion");
    EXPECT_EQ(snap.inventory[0].count, 5);
    EXPECT_EQ(snap.equipped_weapon, "iron_sword");
    EXPECT_EQ(snap.talent_points, 3);
    EXPECT_EQ(snap.unlocked_talents.size(), 3u);
    EXPECT_EQ(snap.completed_quests, std::vector<std::string>({"q1", "q2"}));
    ASSERT_EQ(snap.active_quests.size(), 1u);
    EXPECT_EQ(snap.active_quests[0].quest_id, "q3");
    EXPECT_FALSE(snap.active_quests[0].objectives_json.empty());
    // Objective JSON should round-trip key fields.
    EXPECT_NE(snap.active_quests[0].objectives_json.find("goblin"), std::string::npos);
    EXPECT_NE(snap.active_quests[0].objectives_json.find("\"current\":2"), std::string::npos);
}

TEST(PlayerPersistence, SnapshotApplyRoundtripsAllFields) {
    entt::registry reg;
    auto e = make_player_entity(reg);

    PlayerSnapshot snap;
    snap.name = "alice";
    snap.player_class = 1;
    snap.level = 7;
    snap.xp = 555;
    snap.gold = 50;
    snap.pos_x = 100.0f; snap.pos_y = 25.0f; snap.pos_z = -50.0f;
    snap.rotation = 0.75f;
    snap.health = 60.0f; snap.max_health = 150.0f;
    snap.mana = 30.0f; snap.max_mana = 90.0f; snap.mana_regen = 2.0f;
    snap.talent_points = 2;
    snap.equipped_weapon = "bow";
    snap.equipped_armor = "cloth";
    snap.inventory = {{"arrow", 99}, {"bandage", 3}};
    snap.unlocked_talents = {"agility"};
    snap.completed_quests = {"tutorial"};
    snap.active_quests = {{"hunt", R"([{"type":"kill","target":"deer","current":1,"required":3,"complete":false}])"}};

    apply_snapshot_to_entity(reg, e, snap);

    auto& tx = reg.get<ecs::Transform>(e);
    EXPECT_FLOAT_EQ(tx.x, 100.0f);
    EXPECT_FLOAT_EQ(tx.z, -50.0f);
    EXPECT_FLOAT_EQ(tx.rotation, 0.75f);

    auto& hp = reg.get<ecs::Health>(e);
    EXPECT_FLOAT_EQ(hp.current, 60.0f);
    EXPECT_FLOAT_EQ(hp.max, 150.0f);

    auto& lvl = reg.get<ecs::PlayerLevel>(e);
    EXPECT_EQ(lvl.level, 7);
    EXPECT_EQ(lvl.xp, 555);
    EXPECT_EQ(lvl.gold, 50);

    auto& inv = reg.get<ecs::Inventory>(e);
    EXPECT_EQ(inv.used_slots, 2);
    EXPECT_EQ(inv.slots[0].item_id, "arrow");
    EXPECT_EQ(inv.slots[0].count, 99);

    auto& eq = reg.get<ecs::Equipment>(e);
    EXPECT_EQ(eq.weapon_id, "bow");

    auto& ts = reg.get<ecs::TalentState>(e);
    EXPECT_EQ(ts.talent_points, 2);
    EXPECT_EQ(ts.unlocked_talents.size(), 1u);

    auto& qs = reg.get<ecs::QuestState>(e);
    EXPECT_EQ(qs.completed_quests.size(), 1u);
    ASSERT_EQ(qs.active_quests.size(), 1u);
    EXPECT_EQ(qs.active_quests[0].quest_id, "hunt");
    ASSERT_EQ(qs.active_quests[0].objectives.size(), 1u);
    EXPECT_EQ(qs.active_quests[0].objectives[0].target, "deer");
    EXPECT_EQ(qs.active_quests[0].objectives[0].current, 1);
    EXPECT_EQ(qs.active_quests[0].objectives[0].required, 3);
    EXPECT_FALSE(qs.active_quests[0].objectives[0].complete);
}

TEST(PlayerPersistence, SnapshotMarksWasDeadWhenHealthZero) {
    entt::registry reg;
    auto e = make_player_entity(reg);
    reg.get<ecs::Health>(e).current = 0.0f;

    auto snap = snapshot_from_entity(reg, e, "alice");
    EXPECT_TRUE(snap.was_dead);
}

TEST(PlayerPersistence, SnapshotMarksAliveWhenHealthPositive) {
    entt::registry reg;
    auto e = make_player_entity(reg);
    reg.get<ecs::Health>(e).current = 10.0f;

    auto snap = snapshot_from_entity(reg, e, "alice");
    EXPECT_FALSE(snap.was_dead);
}

TEST(PlayerPersistence, ApplyDeadSnapshotRevivesAtFullHealth) {
    entt::registry reg;
    auto e = make_player_entity(reg);

    PlayerSnapshot snap;
    snap.name = "alice"; snap.player_class = 1;
    snap.health = 0.0f;
    snap.max_health = 150.0f;
    snap.was_dead = true;

    apply_snapshot_to_entity(reg, e, snap);
    auto& hp = reg.get<ecs::Health>(e);
    EXPECT_FLOAT_EQ(hp.current, 150.0f);  // full revive, not clamp-to-1
}

TEST(PlayerPersistence, ApplyClampsHealthAtLeastOne) {
    entt::registry reg;
    auto e = make_player_entity(reg);

    PlayerSnapshot snap;
    snap.name = "alice"; snap.player_class = 1;
    snap.health = 0.0f;       // dead-on-save: should NOT spawn dead
    snap.max_health = 100.0f;
    apply_snapshot_to_entity(reg, e, snap);
    EXPECT_GE(reg.get<ecs::Health>(e).current, 1.0f);

    snap.health = -50.0f;
    apply_snapshot_to_entity(reg, e, snap);
    EXPECT_GE(reg.get<ecs::Health>(e).current, 1.0f);

    snap.health = 9999.0f;    // overflow: should clamp to max
    apply_snapshot_to_entity(reg, e, snap);
    EXPECT_FLOAT_EQ(reg.get<ecs::Health>(e).current, 100.0f);
}

TEST(PlayerPersistence, ApplyOverwritesExistingInventory) {
    entt::registry reg;
    auto e = make_player_entity(reg);

    auto& inv = reg.get<ecs::Inventory>(e);
    inv.add_item("old_item", 99);
    inv.add_item("another", 5);
    ASSERT_EQ(inv.used_slots, 2);

    PlayerSnapshot snap;
    snap.name = "alice"; snap.player_class = 1;
    snap.health = snap.max_health = 100.0f;
    snap.inventory = {{"new_item", 1}};
    apply_snapshot_to_entity(reg, e, snap);

    EXPECT_EQ(inv.used_slots, 1);
    EXPECT_EQ(inv.slots[0].item_id, "new_item");
    // Slots beyond used_slots should be cleared.
    EXPECT_TRUE(inv.slots[1].item_id.empty());
}

TEST(PlayerPersistence, MalformedObjectivesJsonDoesNotThrow) {
    entt::registry reg;
    auto e = make_player_entity(reg);

    PlayerSnapshot snap;
    snap.name = "alice"; snap.player_class = 1;
    snap.health = snap.max_health = 100.0f;
    snap.active_quests = {{"broken", "this is { not valid JSON"}};

    EXPECT_NO_THROW(apply_snapshot_to_entity(reg, e, snap));
    auto& qs = reg.get<ecs::QuestState>(e);
    ASSERT_EQ(qs.active_quests.size(), 1u);
    EXPECT_EQ(qs.active_quests[0].quest_id, "broken");
    EXPECT_TRUE(qs.active_quests[0].objectives.empty());
}

TEST(PlayerPersistence, AllCompleteFlagDerivedFromObjectives) {
    entt::registry reg;
    auto e = make_player_entity(reg);

    PlayerSnapshot snap;
    snap.name = "alice"; snap.player_class = 1;
    snap.health = snap.max_health = 100.0f;
    snap.active_quests = {{
        "done",
        R"([{"type":"kill","target":"goblin","current":5,"required":5,"complete":true}])"
    }};

    apply_snapshot_to_entity(reg, e, snap);
    auto& qs = reg.get<ecs::QuestState>(e);
    ASSERT_EQ(qs.active_quests.size(), 1u);
    EXPECT_TRUE(qs.active_quests[0].all_complete);
}

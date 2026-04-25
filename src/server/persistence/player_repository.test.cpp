#include "player_repository.hpp"
#include "database.hpp"
#include <gtest/gtest.h>

#include <algorithm>

using namespace mmo::server::persistence;

namespace {

PlayerSnapshot make_full_snapshot(std::string name = "alice") {
    PlayerSnapshot s;
    s.name = std::move(name);
    s.player_class = 2;
    s.level = 17;
    s.xp = 12345;
    s.gold = 999;
    s.pos_x = 4321.0f;
    s.pos_y = 50.5f;
    s.pos_z = -250.25f;
    s.rotation = 1.5707f;
    s.health = 88.0f;
    s.max_health = 120.0f;
    s.mana = 55.5f;
    s.max_mana = 100.0f;
    s.mana_regen = 2.5f;
    s.talent_points = 4;
    s.equipped_weapon = "iron_sword";
    s.equipped_armor = "leather_chest";
    s.inventory = {{"health_potion", 5}, {"mana_potion", 3}, {"gold_ore", 12}};
    s.unlocked_talents = {"fortitude", "swiftness"};
    s.completed_quests = {"intro", "first_kill"};
    s.active_quests = {
        {"slay_goblins", R"([{"type":"kill","target":"goblin","current":3,"required":10,"complete":false}])"}};
    s.last_seen_unix = 1'700'000'000LL;
    return s;
}

void expect_equal(const PlayerSnapshot& a, const PlayerSnapshot& b) {
    EXPECT_EQ(a.name, b.name);
    EXPECT_EQ(a.player_class, b.player_class);
    EXPECT_EQ(a.level, b.level);
    EXPECT_EQ(a.xp, b.xp);
    EXPECT_EQ(a.gold, b.gold);
    EXPECT_FLOAT_EQ(a.pos_x, b.pos_x);
    EXPECT_FLOAT_EQ(a.pos_y, b.pos_y);
    EXPECT_FLOAT_EQ(a.pos_z, b.pos_z);
    EXPECT_FLOAT_EQ(a.rotation, b.rotation);
    EXPECT_FLOAT_EQ(a.health, b.health);
    EXPECT_FLOAT_EQ(a.max_health, b.max_health);
    EXPECT_FLOAT_EQ(a.mana, b.mana);
    EXPECT_FLOAT_EQ(a.max_mana, b.max_mana);
    EXPECT_FLOAT_EQ(a.mana_regen, b.mana_regen);
    EXPECT_EQ(a.talent_points, b.talent_points);
    EXPECT_EQ(a.equipped_weapon, b.equipped_weapon);
    EXPECT_EQ(a.equipped_armor, b.equipped_armor);

    // unlocked_talents and completed_quests are semantic sets — SQLite returns
    // them in storage order, not insertion order. Compare as sorted vectors.
    auto sorted = [](std::vector<std::string> v) {
        std::sort(v.begin(), v.end());
        return v;
    };
    EXPECT_EQ(sorted(a.unlocked_talents), sorted(b.unlocked_talents));
    EXPECT_EQ(sorted(a.completed_quests), sorted(b.completed_quests));
    EXPECT_EQ(a.last_seen_unix, b.last_seen_unix);

    ASSERT_EQ(a.inventory.size(), b.inventory.size());
    for (size_t i = 0; i < a.inventory.size(); ++i) {
        EXPECT_EQ(a.inventory[i].item_id, b.inventory[i].item_id);
        EXPECT_EQ(a.inventory[i].count, b.inventory[i].count);
    }

    ASSERT_EQ(a.active_quests.size(), b.active_quests.size());
    for (size_t i = 0; i < a.active_quests.size(); ++i) {
        EXPECT_EQ(a.active_quests[i].quest_id, b.active_quests[i].quest_id);
        EXPECT_EQ(a.active_quests[i].objectives_json, b.active_quests[i].objectives_json);
    }
}

class PlayerRepoFixture : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_unique<Database>(":memory:");
        db->migrate();
        repo = std::make_unique<PlayerRepository>(*db);
    }
    std::unique_ptr<Database> db;
    std::unique_ptr<PlayerRepository> repo;
};

} // namespace

TEST_F(PlayerRepoFixture, LoadMissingReturnsNullopt) {
    auto loaded = repo->load("nobody");
    EXPECT_FALSE(loaded.has_value());
}

TEST_F(PlayerRepoFixture, ExistsReportsCorrectly) {
    EXPECT_FALSE(repo->exists("alice"));
    repo->save(make_full_snapshot("alice"));
    EXPECT_TRUE(repo->exists("alice"));
    EXPECT_FALSE(repo->exists("bob"));
}

TEST_F(PlayerRepoFixture, RoundtripPreservesAllFields) {
    auto original = make_full_snapshot();
    repo->save(original);

    auto loaded = repo->load(original.name);
    ASSERT_TRUE(loaded.has_value());
    expect_equal(original, *loaded);
}

TEST_F(PlayerRepoFixture, SaveReplacesPriorState) {
    auto first = make_full_snapshot();
    repo->save(first);

    PlayerSnapshot updated;
    updated.name = first.name;
    updated.player_class = first.player_class;
    updated.level = 20;
    updated.xp = 50000;
    updated.health = updated.max_health = 200.0f;
    updated.inventory = {{"diamond_sword", 1}};
    updated.unlocked_talents = {"fortitude"}; // shrunk
    updated.completed_quests = {};            // cleared
    updated.active_quests = {};
    repo->save(updated);

    auto loaded = repo->load(first.name);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->level, 20);
    EXPECT_EQ(loaded->xp, 50000);
    EXPECT_EQ(loaded->inventory.size(), 1u);
    EXPECT_EQ(loaded->inventory[0].item_id, "diamond_sword");
    EXPECT_EQ(loaded->unlocked_talents.size(), 1u);
    EXPECT_TRUE(loaded->completed_quests.empty());
    EXPECT_TRUE(loaded->active_quests.empty());
}

TEST_F(PlayerRepoFixture, EmptyInventoryRoundtrips) {
    PlayerSnapshot s;
    s.name = "naked";
    s.health = s.max_health = 100.0f;
    repo->save(s);

    auto loaded = repo->load("naked");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->inventory.empty());
    EXPECT_TRUE(loaded->equipped_weapon.empty());
    EXPECT_TRUE(loaded->equipped_armor.empty());
}

TEST_F(PlayerRepoFixture, MultiplePlayersIsolated) {
    auto a = make_full_snapshot("alice");
    a.gold = 100;
    auto b = make_full_snapshot("bob");
    b.gold = 200;
    b.inventory = {{"bow", 1}};

    repo->save(a);
    repo->save(b);

    auto loaded_a = repo->load("alice");
    auto loaded_b = repo->load("bob");
    ASSERT_TRUE(loaded_a.has_value());
    ASSERT_TRUE(loaded_b.has_value());
    EXPECT_EQ(loaded_a->gold, 100);
    EXPECT_EQ(loaded_b->gold, 200);
    EXPECT_EQ(loaded_a->inventory.size(), 3u); // make_full_snapshot default
    EXPECT_EQ(loaded_b->inventory.size(), 1u);
    EXPECT_EQ(loaded_b->inventory[0].item_id, "bow");
}

TEST_F(PlayerRepoFixture, SaveDoesNotLeakAcrossPlayers) {
    auto a = make_full_snapshot("alice");
    repo->save(a);

    PlayerSnapshot b;
    b.name = "bob";
    b.health = b.max_health = 50.0f;
    repo->save(b);

    // Saving bob must not have wiped alice.
    auto loaded_a = repo->load("alice");
    ASSERT_TRUE(loaded_a.has_value());
    EXPECT_EQ(loaded_a->inventory.size(), 3u);
    EXPECT_EQ(loaded_a->unlocked_talents.size(), 2u);
}

TEST_F(PlayerRepoFixture, WasDeadFlagRoundtrips) {
    PlayerSnapshot s;
    s.name = "fallen";
    s.health = 0.0f; // logged out at 0 hp
    s.max_health = 100.0f;
    s.was_dead = true;
    repo->save(s);

    auto loaded = repo->load("fallen");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->was_dead);
    EXPECT_FLOAT_EQ(loaded->health, 0.0f);
}

TEST_F(PlayerRepoFixture, WasDeadDefaultsFalseOnAliveSnapshot) {
    auto s = make_full_snapshot();
    // make_full_snapshot leaves was_dead at default (false).
    repo->save(s);

    auto loaded = repo->load(s.name);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(loaded->was_dead);
}

TEST_F(PlayerRepoFixture, SpecialCharactersInNameAndItem) {
    PlayerSnapshot s;
    s.name = "Bryan O'Neill"; // apostrophe (must not break SQL)
    s.health = s.max_health = 100.0f;
    s.inventory = {{"item; DROP TABLE players;--", 1}}; // SQL injection attempt
    repo->save(s);

    auto loaded = repo->load(s.name);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->inventory.size(), 1u);
    EXPECT_EQ(loaded->inventory[0].item_id, "item; DROP TABLE players;--");

    // players table must still exist:
    EXPECT_TRUE(repo->exists(s.name));
}

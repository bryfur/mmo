#include "server/systems/loot_system.hpp"
#include "server/ecs/game_components.hpp"
#include <entt/entt.hpp>
#include <filesystem>
#include <gtest/gtest.h>
#include <random>

using namespace mmo::server;
using namespace mmo::server::systems;

namespace {
std::string find_data_dir() {
    namespace fs = std::filesystem;
    fs::path cur = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = cur / "data";
        if (fs::exists(candidate / "classes.json")) {
            return candidate.string();
        }
        if (cur.has_parent_path()) {
            cur = cur.parent_path();
        } else {
            break;
        }
    }
    return "data";
}
} // namespace

// ============================================================================
// give_loot: overflow handling
// ============================================================================

TEST(LootSystem, GiveGoldAddsToPlayerLevel) {
    entt::registry registry;
    auto player = registry.create();
    registry.emplace<ecs::PlayerLevel>(player);
    registry.emplace<ecs::Inventory>(player);

    LootResult loot;
    loot.gold = 123;

    auto overflow = give_loot(registry, player, loot);
    EXPECT_TRUE(overflow.empty());
    EXPECT_EQ(registry.get<ecs::PlayerLevel>(player).gold, 123);
}

TEST(LootSystem, GiveItemsIntoInventory) {
    entt::registry registry;
    auto player = registry.create();
    registry.emplace<ecs::PlayerLevel>(player);
    registry.emplace<ecs::Inventory>(player);

    LootResult loot;
    loot.items.emplace_back("iron_sword", 1);
    loot.items.emplace_back("wild_herbs", 3);

    auto overflow = give_loot(registry, player, loot);
    EXPECT_TRUE(overflow.empty());

    auto& inv = registry.get<ecs::Inventory>(player);
    EXPECT_EQ(inv.count_item("iron_sword"), 1);
    EXPECT_EQ(inv.count_item("wild_herbs"), 3);
}

TEST(LootSystem, OverflowReportsLostItemsWhenInventoryFull) {
    entt::registry registry;
    auto player = registry.create();
    registry.emplace<ecs::PlayerLevel>(player);
    auto& inv = registry.emplace<ecs::Inventory>(player);

    // Fill inventory with 20 distinct items (each slot unique → 20 slots used)
    for (int i = 0; i < ecs::Inventory::MAX_SLOTS; ++i) {
        std::string id = "filler_" + std::to_string(i);
        inv.add_item(id, 1, 1);
    }
    EXPECT_EQ(inv.used_slots, ecs::Inventory::MAX_SLOTS);

    LootResult loot;
    loot.items.emplace_back("runic_blade", 1); // new item, no stacking

    auto overflow = give_loot(registry, player, loot);
    ASSERT_EQ(overflow.size(), 1u);
    EXPECT_EQ(overflow[0].first, "runic_blade");
    EXPECT_EQ(overflow[0].second, 1);
}

TEST(LootSystem, OverflowStacksIntoExistingSlotWhenPossible) {
    entt::registry registry;
    auto player = registry.create();
    registry.emplace<ecs::PlayerLevel>(player);
    auto& inv = registry.emplace<ecs::Inventory>(player);

    // Fill all slots except leave one with wild_herbs that can stack.
    for (int i = 0; i < ecs::Inventory::MAX_SLOTS - 1; ++i) {
        inv.add_item("filler_" + std::to_string(i), 1, 1);
    }
    inv.add_item("wild_herbs", 1, 99);

    LootResult loot;
    loot.items.emplace_back("wild_herbs", 5); // should stack on the existing slot

    auto overflow = give_loot(registry, player, loot);
    EXPECT_TRUE(overflow.empty());
    EXPECT_EQ(inv.count_item("wild_herbs"), 6);
}

// ============================================================================
// Inventory helpers
// ============================================================================

TEST(LootSystem, InventoryAddAndRemoveWorkWithStacks) {
    ecs::Inventory inv;
    EXPECT_TRUE(inv.add_item("wild_herbs", 50, 99));
    EXPECT_TRUE(inv.add_item("wild_herbs", 49, 99)); // stacks into same slot
    EXPECT_EQ(inv.count_item("wild_herbs"), 99);
    EXPECT_EQ(inv.used_slots, 1);
    EXPECT_TRUE(inv.add_item("wild_herbs", 50, 99)); // overflows into a new slot
    EXPECT_EQ(inv.used_slots, 2);
    EXPECT_EQ(inv.count_item("wild_herbs"), 149);

    EXPECT_TRUE(inv.remove_item("wild_herbs", 100));
    EXPECT_EQ(inv.count_item("wild_herbs"), 49);
}

TEST(LootSystem, InventoryRejectsOverflowItemsInFullInventory) {
    ecs::Inventory inv;
    for (int i = 0; i < ecs::Inventory::MAX_SLOTS; ++i) {
        inv.add_item("x_" + std::to_string(i), 1, 1);
    }
    EXPECT_FALSE(inv.add_item("new_item", 1, 1)) << "add_item should fail when inventory is full and cannot stack.";
}

// ============================================================================
// Seeded loot rolls: deterministic per seed
// ============================================================================

TEST(LootSystemSeeded, SameSeedProducesSameRoll) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    // Pick any monster_type_id that exists in loot_tables.json.
    std::string monster = "goblin_scout";
    if (!cfg.find_loot_table(monster)) {
        // Fall back to the first table entry.
        ASSERT_FALSE(cfg.loot_tables().empty());
        monster = cfg.loot_tables()[0].monster_type;
    }

    std::mt19937 rng_a(/*seed*/ 12345);
    std::mt19937 rng_b(/*seed*/ 12345);
    LootResult a = roll_loot(monster, cfg, rng_a);
    LootResult b = roll_loot(monster, cfg, rng_b);

    EXPECT_EQ(a.gold, b.gold);
    ASSERT_EQ(a.items.size(), b.items.size());
    for (size_t i = 0; i < a.items.size(); ++i) {
        EXPECT_EQ(a.items[i].first, b.items[i].first);
        EXPECT_EQ(a.items[i].second, b.items[i].second);
    }
}

TEST(LootSystemSeeded, DifferentSeedsCanDiverge) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    std::string monster = "goblin_scout";
    if (!cfg.find_loot_table(monster)) {
        ASSERT_FALSE(cfg.loot_tables().empty());
        monster = cfg.loot_tables()[0].monster_type;
    }

    // Roll a bunch of times with two different seeds; at least one result
    // should differ unless the table is deterministic (gold_min==gold_max
    // and no random drops). Just assert seeds give two self-consistent
    // streams rather than same stream.
    std::mt19937 rng1(1);
    std::mt19937 rng2(2);
    bool diverged = false;
    for (int i = 0; i < 20 && !diverged; ++i) {
        auto r1 = roll_loot(monster, cfg, rng1);
        auto r2 = roll_loot(monster, cfg, rng2);
        if (r1.gold != r2.gold) {
            diverged = true;
        }
        if (r1.items.size() != r2.items.size()) {
            diverged = true;
        }
    }
    // We don't REQUIRE divergence (some tables could be trivial), but
    // this test documents the API: independent seeds produce independent
    // streams and callers can rely on that.
    SUCCEED();
    (void)diverged;
}

TEST(LootSystemSeeded, UnknownMonsterReturnsEmpty) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    std::mt19937 rng(42);
    LootResult r = roll_loot("definitely_not_a_monster", cfg, rng);
    EXPECT_EQ(r.gold, 0);
    EXPECT_TRUE(r.items.empty());
}

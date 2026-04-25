#include <gtest/gtest.h>
#include "server/game_config.hpp"
#include <filesystem>

using namespace mmo::server;

namespace {

// Locate the data/ directory by walking up from the binary's CWD until a
// "data" sibling shows up. Tests are normally run from <build>/ so the repo
// root is one or two levels up.
std::string find_data_dir() {
    namespace fs = std::filesystem;
    fs::path cur = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = cur / "data";
        if (fs::exists(candidate / "classes.json")) return candidate.string();
        if (cur.has_parent_path()) cur = cur.parent_path();
        else break;
    }
    return "data";
}

} // namespace

// ============================================================================
// Full-load integrity tests
// ============================================================================

TEST(GameConfigLoad, LoadsAllDataFilesSuccessfully) {
    GameConfig cfg;
    bool ok = cfg.load(find_data_dir());
    EXPECT_TRUE(ok) << "GameConfig::load returned false — one of the required"
                       " server/world/network/classes/monsters/town JSON files"
                       " failed to parse.";
}

TEST(GameConfigLoad, ExpectedContentCounts) {
    GameConfig cfg;
    cfg.load(find_data_dir());

    EXPECT_GE(cfg.classes().size(), 4u);          // warrior, mage, paladin, archer
    EXPECT_GE(cfg.monster_types().size(), 10u);
    EXPECT_GE(cfg.zones().size(), 10u);
    EXPECT_GE(cfg.items().size(), 50u);
    EXPECT_GE(cfg.loot_tables().size(), 10u);
    EXPECT_GE(cfg.skills().size(), 20u);
    EXPECT_GE(cfg.quests().size(), 20u);
    EXPECT_GE(cfg.vendors().size(), 1u);
    EXPECT_GE(cfg.recipes().size(), 5u);
}

// ============================================================================
// Referential integrity: cross-JSON links should not dangle
// ============================================================================

TEST(GameConfigLoad, QuestPrerequisitesExist) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    for (const auto& q : cfg.quests()) {
        if (q.prerequisite_quest.empty()) continue;
        EXPECT_NE(cfg.find_quest(q.prerequisite_quest), nullptr)
            << "Quest '" << q.id << "' references missing prerequisite '"
            << q.prerequisite_quest << "'";
    }
}

TEST(GameConfigLoad, QuestItemRewardsExist) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    for (const auto& q : cfg.quests()) {
        if (q.rewards.item_reward.empty()) continue;
        EXPECT_NE(cfg.find_item(q.rewards.item_reward), nullptr)
            << "Quest '" << q.id << "' rewards missing item '"
            << q.rewards.item_reward << "'";
    }
}

TEST(GameConfigLoad, LootTableItemsExist) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    for (const auto& lt : cfg.loot_tables()) {
        for (const auto& drop : lt.drops) {
            EXPECT_NE(cfg.find_item(drop.item_id), nullptr)
                << "Loot table '" << lt.id << "' drops missing item '"
                << drop.item_id << "'";
        }
    }
}

TEST(GameConfigLoad, VendorStockItemsExist) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    for (const auto& v : cfg.vendors()) {
        for (const auto& s : v.stock) {
            EXPECT_NE(cfg.find_item(s.item_id), nullptr)
                << "Vendor '" << v.npc_type << "' stocks missing item '"
                << s.item_id << "'";
        }
    }
}

TEST(GameConfigLoad, CraftRecipesReferenceValidItems) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    for (const auto& r : cfg.recipes()) {
        EXPECT_NE(cfg.find_item(r.output_item_id), nullptr)
            << "Recipe '" << r.id << "' outputs missing item '"
            << r.output_item_id << "'";
        for (const auto& ing : r.ingredients) {
            EXPECT_NE(cfg.find_item(ing.item_id), nullptr)
                << "Recipe '" << r.id << "' needs missing ingredient '"
                << ing.item_id << "'";
        }
    }
}

TEST(GameConfigLoad, SkillClassesAreValid) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    const std::set<std::string> valid_classes = {
        "warrior", "mage", "paladin", "archer"
    };
    for (const auto& sk : cfg.skills()) {
        EXPECT_TRUE(valid_classes.count(sk.class_name) > 0)
            << "Skill '" << sk.id << "' has invalid class '"
            << sk.class_name << "'";
    }
}

TEST(GameConfigLoad, XPCurveIsMonotonicallyIncreasing) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    const auto& curve = cfg.leveling().xp_curve;
    if (curve.size() < 2) return;
    for (size_t i = 1; i < curve.size(); ++i) {
        EXPECT_GT(curve[i], curve[i - 1])
            << "xp_curve[" << i << "] (" << curve[i] << ") should be > "
            << "xp_curve[" << i - 1 << "] (" << curve[i - 1] << ")";
    }
}

TEST(GameConfigLoad, SkillUnlockLevelsWithinRange) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    int max_level = cfg.leveling().max_level;
    for (const auto& sk : cfg.skills()) {
        EXPECT_LE(sk.unlock_level, max_level)
            << "Skill '" << sk.id << "' unlock_level " << sk.unlock_level
            << " is above max_level " << max_level;
    }
}

TEST(GameConfigLoad, ZonesAreLevelRangeCoherent) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    for (const auto& z : cfg.zones()) {
        EXPECT_LE(z.level_min, z.level_max)
            << "Zone '" << z.id << "' has level_min > level_max";
    }
}

TEST(GameConfigLoad, FindItemIsO1) {
    GameConfig cfg;
    cfg.load(find_data_dir());
    for (const auto& item : cfg.items()) {
        const ItemConfig* looked_up = cfg.find_item(item.id);
        ASSERT_NE(looked_up, nullptr) << "find_item failed for '" << item.id << "'";
        EXPECT_EQ(looked_up, &item);
    }
}

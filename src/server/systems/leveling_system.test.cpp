#include "server/systems/leveling_system.hpp"
#include "server/ecs/game_components.hpp"
#include <entt/entt.hpp>
#include <gtest/gtest.h>

using namespace mmo::server;
using namespace mmo::server::systems;

namespace {

GameConfig make_minimal_config() {
    GameConfig cfg;
    // Can't easily populate private fields without a load, so the real
    // integration coverage lives in game_config.test.cpp. Here we just
    // test the pure arithmetic parts that don't need a loaded config.
    return cfg;
}

entt::entity make_player(entt::registry& r, int level, int xp = 0) {
    auto e = r.create();
    auto& pl = r.emplace<ecs::PlayerLevel>(e);
    pl.level = level;
    pl.xp = xp;
    pl.gold = 0;
    pl.mana = 50.0f;
    pl.max_mana = 100.0f;
    pl.mana_regen = 5.0f;
    r.emplace<ecs::Health>(e);
    r.emplace<ecs::EntityInfo>(e);
    return e;
}

} // namespace

// ============================================================================
// Class name helper
// ============================================================================

TEST(LevelingSystem, ClassNameForIndex) {
    EXPECT_STREQ(class_name_for_index(0), "warrior");
    EXPECT_STREQ(class_name_for_index(1), "mage");
    EXPECT_STREQ(class_name_for_index(2), "paladin");
    EXPECT_STREQ(class_name_for_index(3), "archer");
    EXPECT_STREQ(class_name_for_index(99), "warrior"); // fallback
}

// ============================================================================
// Mana regen
// ============================================================================

TEST(LevelingSystem, ManaRegenAccumulatesOverTime) {
    entt::registry r;
    auto p = make_player(r, 5);
    auto& pl = r.get<ecs::PlayerLevel>(p);
    pl.mana = 50.0f;

    // 1 second of regen at 5/s → mana = 55.
    update_mana_regen(r, 1.0f);
    EXPECT_FLOAT_EQ(pl.mana, 55.0f);
}

TEST(LevelingSystem, ManaRegenClampsAtMax) {
    entt::registry r;
    auto p = make_player(r, 5);
    auto& pl = r.get<ecs::PlayerLevel>(p);
    pl.mana = 99.0f;

    update_mana_regen(r, 10.0f); // would overshoot
    EXPECT_FLOAT_EQ(pl.mana, pl.max_mana);
}

// ============================================================================
// Consumable cooldowns (tied to the same update pass)
// ============================================================================

TEST(LevelingSystem, ConsumableCooldownsTickAlongWithMana) {
    entt::registry r;
    auto p = make_player(r, 5);
    auto& cd = r.emplace<ecs::ConsumableCooldowns>(p);
    cd.set("small_health_potion", 6.0f);
    EXPECT_FLOAT_EQ(cd.remaining("small_health_potion"), 6.0f);

    update_mana_regen(r, 2.5f);
    EXPECT_NEAR(cd.remaining("small_health_potion"), 3.5f, 1e-3f);

    update_mana_regen(r, 5.0f);
    EXPECT_FLOAT_EQ(cd.remaining("small_health_potion"), 0.0f); // expired -> 0
}

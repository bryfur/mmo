#include "server/rules/objective.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::rules;

// ============================================================================
// kill_target_matches
// ============================================================================

TEST(Objective, ExactTargetMatches) {
    EXPECT_TRUE(kill_target_matches("wolf", "wolf"));
}

TEST(Objective, DifferentTargetRejected) {
    EXPECT_FALSE(kill_target_matches("wolf", "goblin_scout"));
}

TEST(Objective, MonsterWildcardMatches) {
    EXPECT_TRUE(kill_target_matches("monster", "wolf"));
    EXPECT_TRUE(kill_target_matches("monster", "dragon"));
}

TEST(Objective, NpcEnemyWildcardMatches) {
    EXPECT_TRUE(kill_target_matches("npc_enemy", "goblin_scout"));
}

// ============================================================================
// in_objective_area
// ============================================================================

TEST(Objective, InsideRadius) {
    ObjectiveDef o{.location_x = 100, .location_z = 100, .radius = 50};
    EXPECT_TRUE(in_objective_area(o, 100, 100));
    EXPECT_TRUE(in_objective_area(o, 140, 100));
}

TEST(Objective, OnRadiusBoundaryIsInside) {
    ObjectiveDef o{.location_x = 0, .location_z = 0, .radius = 50};
    EXPECT_TRUE(in_objective_area(o, 50, 0));
}

TEST(Objective, OutsideRadius) {
    ObjectiveDef o{.location_x = 100, .location_z = 100, .radius = 50};
    EXPECT_FALSE(in_objective_area(o, 200, 200));
}

TEST(Objective, ZeroRadiusExcludesEverythingExceptCenter) {
    ObjectiveDef o{.location_x = 100, .location_z = 100, .radius = 0};
    EXPECT_TRUE(in_objective_area(o, 100, 100));
    EXPECT_FALSE(in_objective_area(o, 100.1f, 100));
}

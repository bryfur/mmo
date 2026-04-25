#include "server/rules/kill_objective.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::rules;

static_assert(Rule<KillObjective>);

TEST(KillObjective, AdvancesOnMatch) {
    KillObjective::Inputs in{
        .def = {.type = "kill", .target = "wolf", .required = 3},
        .state = {},
        .monster_type_id = "wolf",
    };
    EXPECT_EQ(KillObjective::check(in), KillObjective::Result::Ok);
}

TEST(KillObjective, NonMatchingTargetRejected) {
    KillObjective::Inputs in{
        .def = {.type = "kill", .target = "wolf", .required = 3},
        .state = {},
        .monster_type_id = "goblin_scout",
    };
    EXPECT_EQ(KillObjective::check(in), KillObjective::Result::TargetMismatch);
}

TEST(KillObjective, WouldCompleteAtRequiredCount) {
    KillObjective::Inputs in{
        .def = {.type = "kill", .target = "wolf", .required = 3},
        .state = {.current = 2},
        .monster_type_id = "wolf",
    };
    EXPECT_EQ(KillObjective::check(in), KillObjective::Result::Ok);
    EXPECT_TRUE(KillObjective::would_complete(in));
}

TEST(KillObjective, CompletedObjectiveIsNotApplicable) {
    KillObjective::Inputs in{
        .def = {.type = "kill", .target = "wolf", .required = 3},
        .state = {.current = 3, .complete = true},
        .monster_type_id = "wolf",
    };
    EXPECT_EQ(KillObjective::check(in), KillObjective::Result::NotApplicable);
}

TEST(KillObjective, VisitObjectiveIsNotApplicable) {
    KillObjective::Inputs in{
        .def = {.type = "visit", .target = "Town", .required = 1},
        .state = {},
        .monster_type_id = "wolf",
    };
    EXPECT_EQ(KillObjective::check(in), KillObjective::Result::NotApplicable);
}

TEST(KillObjective, KillInAreaRejectsKillOutsideCircle) {
    KillObjective::Inputs in{
        .def = {.type = "kill_in_area",
                .target = "npc_enemy",
                .required = 5,
                .location_x = 1000,
                .location_z = 1000,
                .radius = 500},
        .state = {},
        .monster_type_id = "goblin_scout",
        .kill_x = 5000,
        .kill_z = 5000,
    };
    EXPECT_EQ(KillObjective::check(in), KillObjective::Result::OutsideArea);
}

TEST(KillObjective, KillInAreaAcceptsKillInsideCircle) {
    KillObjective::Inputs in{
        .def = {.type = "kill_in_area",
                .target = "npc_enemy",
                .required = 5,
                .location_x = 1000,
                .location_z = 1000,
                .radius = 500},
        .state = {},
        .monster_type_id = "goblin_scout",
        .kill_x = 1200,
        .kill_z = 1100,
    };
    EXPECT_EQ(KillObjective::check(in), KillObjective::Result::Ok);
}

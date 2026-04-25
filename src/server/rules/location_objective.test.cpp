#include "server/rules/location_objective.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::rules;

static_assert(Rule<LocationObjective>);

TEST(LocationObjective, VisitAdvancesInsideRadius) {
    LocationObjective::Inputs in{
        .def = {.type = "visit", .required = 1, .location_x = 100, .location_z = 100, .radius = 50},
        .state = {},
        .player_x = 120,
        .player_z = 100,
    };
    EXPECT_EQ(LocationObjective::check(in), LocationObjective::Result::Ok);
}

TEST(LocationObjective, ExploreSharesBehaviorWithVisit) {
    LocationObjective::Inputs in{
        .def = {.type = "explore", .required = 1, .location_x = 0, .location_z = 0, .radius = 100},
        .state = {},
        .player_x = 50,
        .player_z = 0,
    };
    EXPECT_EQ(LocationObjective::check(in), LocationObjective::Result::Ok);
}

TEST(LocationObjective, OutsideRadiusRejected) {
    LocationObjective::Inputs in{
        .def = {.type = "visit", .required = 1, .location_x = 0, .location_z = 0, .radius = 50},
        .state = {},
        .player_x = 5000,
        .player_z = 5000,
    };
    EXPECT_EQ(LocationObjective::check(in), LocationObjective::Result::OutsideArea);
}

TEST(LocationObjective, KillObjectiveIsNotApplicable) {
    LocationObjective::Inputs in{
        .def = {.type = "kill", .target = "wolf", .required = 3},
        .state = {},
    };
    EXPECT_EQ(LocationObjective::check(in), LocationObjective::Result::NotApplicable);
}

TEST(LocationObjective, CompletedObjectiveIsNotApplicable) {
    LocationObjective::Inputs in{
        .def = {.type = "visit", .required = 1, .location_x = 0, .location_z = 0, .radius = 50},
        .state = {.complete = true},
        .player_x = 0,
        .player_z = 0,
    };
    EXPECT_EQ(LocationObjective::check(in), LocationObjective::Result::NotApplicable);
}

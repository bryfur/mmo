#include "server/rules/craft_rule.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::rules;

static_assert(Rule<CraftRule>);

namespace {
CraftRule::Inputs baseline() {
    CraftRule::Inputs i;
    i.recipe_exists = true;
    i.recipe_required_level = 1;
    i.caster_level = 5;
    i.gold_cost = 5;
    i.player_gold = 50;
    i.ingredients.push_back({"iron_ore", 2, 2});
    i.ingredients.push_back({"rough_wood", 1, 3});
    i.inventory_has_room_for_output = true;
    i.station_requirement = "blacksmith";
    i.near_matching_station = true;
    return i;
}
} // namespace

// ============================================================================
// Happy path
// ============================================================================

TEST(CraftRule, BaselineAccepted) {
    EXPECT_EQ(CraftRule::check(baseline()), CraftRule::Result::Ok);
}

// ============================================================================
// Each failure branch
// ============================================================================

TEST(CraftRule, UnknownRecipeRejected) {
    auto i = baseline();
    i.recipe_exists = false;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::UnknownRecipe);
}

TEST(CraftRule, LowLevelRejected) {
    auto i = baseline();
    i.caster_level = 2;
    i.recipe_required_level = 10;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::LevelTooLow);
}

TEST(CraftRule, ExactLevelAccepted) {
    auto i = baseline();
    i.caster_level = 5;
    i.recipe_required_level = 5;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::Ok);
}

TEST(CraftRule, NotEnoughGoldRejected) {
    auto i = baseline();
    i.gold_cost = 200;
    i.player_gold = 100;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::NotEnoughGold);
}

TEST(CraftRule, ExactGoldAccepted) {
    auto i = baseline();
    i.gold_cost = 50;
    i.player_gold = 50;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::Ok);
}

TEST(CraftRule, MissingIngredientRejected) {
    auto i = baseline();
    i.ingredients.push_back({"enchanted_dust", 3, 0});
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::MissingIngredient);
}

TEST(CraftRule, InsufficientIngredientQuantityRejected) {
    auto i = baseline();
    i.ingredients[0].required = 5;
    i.ingredients[0].available = 3;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::MissingIngredient);
}

TEST(CraftRule, ExactIngredientCountAccepted) {
    auto i = baseline();
    i.ingredients[0].required = 2;
    i.ingredients[0].available = 2;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::Ok);
}

TEST(CraftRule, InventoryFullRejected) {
    auto i = baseline();
    i.inventory_has_room_for_output = false;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::InventoryFull);
}

// ============================================================================
// Station requirement
// ============================================================================

TEST(CraftRule, NotNearStationRejected) {
    auto i = baseline();
    i.station_requirement = "blacksmith";
    i.near_matching_station = false;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::NotNearStation);
}

TEST(CraftRule, EmptyStationSkipsCheck) {
    auto i = baseline();
    i.station_requirement = "";
    i.near_matching_station = false;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::Ok);
}

TEST(CraftRule, AnyStationSkipsCheck) {
    auto i = baseline();
    i.station_requirement = "any";
    i.near_matching_station = false;
    EXPECT_EQ(CraftRule::check(i), CraftRule::Result::Ok);
}

// ============================================================================
// near_station helper
// ============================================================================

TEST(CraftRule, NearStationInRange) {
    EXPECT_TRUE(CraftRule::near_station(100, 100, 100, 100));
    EXPECT_TRUE(CraftRule::near_station(100, 100, 500, 100)); // exactly MAX_STATION_DIST
}

TEST(CraftRule, NearStationOutOfRange) {
    EXPECT_FALSE(CraftRule::near_station(0, 0, 1000, 0));
}

static_assert(CraftRule::near_station(0, 0, 100, 0));

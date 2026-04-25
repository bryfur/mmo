#include <gtest/gtest.h>
#include "server/rules/vendor_sell_rule.hpp"

using namespace mmo::server::rules;

static_assert(Rule<VendorSellRule>);

namespace {
VendorSellRule::Inputs baseline() {
    return VendorSellRule::Inputs{
        .player_x = 10, .player_z = 10,
        .npc_x = 0, .npc_z = 0,
        .slot_occupied = true,
        .item_exists = true,
        .slot_count = 5,
        .quantity = 1,
    };
}
} // namespace

TEST(VendorSellRule, OkWhenValid) {
    EXPECT_EQ(VendorSellRule::check(baseline()), VendorSellRule::Result::Ok);
}

TEST(VendorSellRule, RejectedWhenOutOfRange) {
    auto i = baseline(); i.npc_x = 9999;
    EXPECT_EQ(VendorSellRule::check(i), VendorSellRule::Result::OutOfRange);
}

TEST(VendorSellRule, RejectedWhenSlotEmpty) {
    auto i = baseline(); i.slot_occupied = false;
    EXPECT_EQ(VendorSellRule::check(i), VendorSellRule::Result::EmptySlot);
}

TEST(VendorSellRule, RejectedWhenQuantityExceedsSlotCount) {
    auto i = baseline(); i.slot_count = 1; i.quantity = 5;
    EXPECT_EQ(VendorSellRule::check(i), VendorSellRule::Result::InsufficientQuantity);
}

TEST(VendorSellRule, RejectedWhenQuantityZero) {
    auto i = baseline(); i.quantity = 0;
    EXPECT_EQ(VendorSellRule::check(i), VendorSellRule::Result::InsufficientQuantity);
}

#include "server/rules/vendor_buy_rule.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::rules;

static_assert(Rule<VendorBuyRule>);

namespace {
VendorBuyRule::Inputs baseline() {
    return VendorBuyRule::Inputs{
        .player_x = 100,
        .player_z = 100,
        .npc_x = 100,
        .npc_z = 105,
        .item_exists = true,
        .available_stock = -1,
        .quantity = 1,
        .unit_price = 10,
        .player_gold = 100,
        .inventory_has_room = true,
    };
}
} // namespace

TEST(VendorBuyRule, OkWhenValid) {
    EXPECT_EQ(VendorBuyRule::check(baseline()), VendorBuyRule::Result::Ok);
}

TEST(VendorBuyRule, RejectedWhenOutOfRange) {
    auto i = baseline();
    i.player_x = 100000;
    EXPECT_EQ(VendorBuyRule::check(i), VendorBuyRule::Result::OutOfRange);
}

TEST(VendorBuyRule, RejectedWhenItemUnknown) {
    auto i = baseline();
    i.item_exists = false;
    EXPECT_EQ(VendorBuyRule::check(i), VendorBuyRule::Result::UnknownItem);
}

TEST(VendorBuyRule, RejectedWhenOutOfStock) {
    auto i = baseline();
    i.available_stock = 0;
    EXPECT_EQ(VendorBuyRule::check(i), VendorBuyRule::Result::OutOfStock);
}

TEST(VendorBuyRule, InfiniteStockAllowsLargeQuantity) {
    auto i = baseline();
    i.available_stock = -1;
    i.quantity = 99;
    i.player_gold = 9999;
    EXPECT_EQ(VendorBuyRule::check(i), VendorBuyRule::Result::Ok);
}

TEST(VendorBuyRule, RejectedWhenNotEnoughGold) {
    auto i = baseline();
    i.unit_price = 50;
    i.quantity = 3;
    i.player_gold = 100;
    EXPECT_EQ(VendorBuyRule::check(i), VendorBuyRule::Result::NotEnoughGold);
}

TEST(VendorBuyRule, RejectedWhenInventoryFull) {
    auto i = baseline();
    i.inventory_has_room = false;
    EXPECT_EQ(VendorBuyRule::check(i), VendorBuyRule::Result::InventoryFull);
}

TEST(VendorBuyRule, ExactGoldPasses) {
    auto i = baseline();
    i.unit_price = 10;
    i.quantity = 10;
    i.player_gold = 100;
    EXPECT_EQ(VendorBuyRule::check(i), VendorBuyRule::Result::Ok);
}

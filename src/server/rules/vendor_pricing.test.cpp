#include "server/rules/vendor_pricing.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::rules;

// ============================================================================
// unit_buy_price
// ============================================================================

TEST(VendorPricing, BuyUsesExplicitPriceWhenSet) {
    EXPECT_EQ(VendorPricing::unit_buy_price(42, 10, 5.0f), 42);
}

TEST(VendorPricing, BuyDerivesFromSellValueAndMultiplier) {
    EXPECT_EQ(VendorPricing::unit_buy_price(0, 20, 4.5f), 90);
}

TEST(VendorPricing, BuyFloorAt1Gold) {
    EXPECT_EQ(VendorPricing::unit_buy_price(0, 0, 4.0f), 1);
    EXPECT_EQ(VendorPricing::unit_buy_price(0, 1, 0.01f), 1);
}

TEST(VendorPricing, BuyOverrideTakesPrecedenceOverDerived) {
    EXPECT_EQ(VendorPricing::unit_buy_price(7, 1000, 10.0f), 7);
}

// ============================================================================
// unit_sell_price
// ============================================================================

TEST(VendorPricing, SellIsDerivedFromMultiplier) {
    EXPECT_EQ(VendorPricing::unit_sell_price(100, 0.25f), 25);
}

TEST(VendorPricing, SellFloorAt1Gold) {
    EXPECT_EQ(VendorPricing::unit_sell_price(0, 0.25f), 1);
    EXPECT_EQ(VendorPricing::unit_sell_price(2, 0.1f), 1);
}

// ============================================================================
// within_range (proximity check shared by buy/sell rules)
// ============================================================================

TEST(VendorPricing, WithinRangeInside) {
    EXPECT_TRUE(VendorPricing::within_range(100, 100, 200, 100));
}

TEST(VendorPricing, WithinRangeBoundaryInclusive) {
    EXPECT_TRUE(VendorPricing::within_range(0, 0, VendorPricing::MAX_INTERACT_DIST, 0));
}

TEST(VendorPricing, WithinRangeOutside) {
    EXPECT_FALSE(VendorPricing::within_range(0, 0, 1000, 0));
}

// Everything is constexpr.
static_assert(VendorPricing::unit_buy_price(0, 20, 4.5f) == 90);
static_assert(VendorPricing::unit_sell_price(100, 0.25f) == 25);
static_assert(VendorPricing::within_range(0, 0, 50, 50));

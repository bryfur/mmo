#include "server/rules/party_leadership.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::rules;

TEST(PartyLeadership, PromotesFirstRemainingMember) {
    std::uint32_t members[] = {42, 55};
    EXPECT_EQ(PartyLeadership::promote_new_leader(members), 42u);
}

TEST(PartyLeadership, DisbandsWhenOneOrZeroRemain) {
    std::uint32_t solo[] = {42};
    EXPECT_EQ(PartyLeadership::promote_new_leader(solo), 0u);
    EXPECT_EQ(PartyLeadership::promote_new_leader({}), 0u);
}

TEST(PartyLeadership, ShouldDisbandPredicate) {
    EXPECT_TRUE(PartyLeadership::should_disband(0));
    EXPECT_TRUE(PartyLeadership::should_disband(1));
    EXPECT_FALSE(PartyLeadership::should_disband(2));
}

static_assert(PartyLeadership::should_disband(1));
static_assert(!PartyLeadership::should_disband(5));

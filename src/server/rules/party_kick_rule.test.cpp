#include <gtest/gtest.h>
#include "server/rules/party_kick_rule.hpp"

using namespace mmo::server::rules;

static_assert(Rule<PartyKickRule>);

TEST(PartyKickRule, KickByLeaderAllowed) {
    PartyKickRule::Inputs i{
        .kicker_id = 1, .target_id = 2,
        .kicker_is_leader = true,
        .target_in_same_party = true,
    };
    EXPECT_EQ(PartyKickRule::check(i), PartyKickRule::Result::Ok);
}

TEST(PartyKickRule, KickByNonLeaderRejected) {
    PartyKickRule::Inputs i{
        .kicker_id = 1, .target_id = 2,
        .kicker_is_leader = false,
        .target_in_same_party = true,
    };
    EXPECT_EQ(PartyKickRule::check(i), PartyKickRule::Result::NotLeader);
}

TEST(PartyKickRule, KickOfPlayerInOtherPartyRejected) {
    PartyKickRule::Inputs i{
        .kicker_id = 1, .target_id = 2,
        .kicker_is_leader = true,
        .target_in_same_party = false,
    };
    EXPECT_EQ(PartyKickRule::check(i), PartyKickRule::Result::TargetNotInParty);
}

TEST(PartyKickRule, SelfKickRejectedEvenForLeader) {
    PartyKickRule::Inputs i{
        .kicker_id = 1, .target_id = 1,
        .kicker_is_leader = true,
        .target_in_same_party = true,
    };
    EXPECT_EQ(PartyKickRule::check(i), PartyKickRule::Result::SelfKick);
}

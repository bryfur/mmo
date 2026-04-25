#include "server/rules/party_invite_rule.hpp"
#include <gtest/gtest.h>

using namespace mmo::server::rules;

static_assert(Rule<PartyInviteRule>);

namespace {
PartyInviteRule::Inputs baseline() {
    return PartyInviteRule::Inputs{
        .inviter_id = 1,
        .target_id = 2,
        .target_online = true,
        .target_in_party = false,
        .inviter_has_party = false,
    };
}
} // namespace

TEST(PartyInviteRule, SoloInviteAccepted) {
    EXPECT_EQ(PartyInviteRule::check(baseline()), PartyInviteRule::Result::Ok);
}

TEST(PartyInviteRule, SelfInviteRejected) {
    auto i = baseline();
    i.target_id = i.inviter_id;
    EXPECT_EQ(PartyInviteRule::check(i), PartyInviteRule::Result::SelfInvite);
}

TEST(PartyInviteRule, OfflineTargetRejected) {
    auto i = baseline();
    i.target_online = false;
    EXPECT_EQ(PartyInviteRule::check(i), PartyInviteRule::Result::TargetOffline);
}

TEST(PartyInviteRule, TargetAlreadyInPartyRejected) {
    auto i = baseline();
    i.target_in_party = true;
    EXPECT_EQ(PartyInviteRule::check(i), PartyInviteRule::Result::TargetAlreadyInAParty);
}

TEST(PartyInviteRule, NonLeaderInviterRejected) {
    auto i = baseline();
    i.inviter_has_party = true;
    i.inviter_is_leader = false;
    i.inviter_party_size = 2;
    EXPECT_EQ(PartyInviteRule::check(i), PartyInviteRule::Result::InviterNotLeader);
}

TEST(PartyInviteRule, FullPartyRejected) {
    auto i = baseline();
    i.inviter_has_party = true;
    i.inviter_is_leader = true;
    i.inviter_party_size = PARTY_MAX_MEMBERS;
    EXPECT_EQ(PartyInviteRule::check(i), PartyInviteRule::Result::PartyFull);
}

TEST(PartyInviteRule, NearFullPartyAccepted) {
    auto i = baseline();
    i.inviter_has_party = true;
    i.inviter_is_leader = true;
    i.inviter_party_size = PARTY_MAX_MEMBERS - 1;
    EXPECT_EQ(PartyInviteRule::check(i), PartyInviteRule::Result::Ok);
}

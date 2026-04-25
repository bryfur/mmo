#pragma once

// PartyInviteRule: decides whether A can invite B to their party. See
// rule_concept.hpp for the Rule contract.

#include "server/rules/rule_concept.hpp"

#include <cstdint>

namespace mmo::server::rules {

/// Maximum members per party. Mirrored from PartyStateMsg::MAX_MEMBERS in
/// the protocol; duplicated here so pure tests don't need a protocol
/// header include.
inline constexpr int PARTY_MAX_MEMBERS = 5;

class PartyInviteRule {
public:
    enum class Result : std::uint8_t {
        Ok,
        SelfInvite,
        TargetOffline,
        TargetAlreadyInAParty,
        InviterNotLeader,     // has a party but isn't the leader
        PartyFull,
    };

    struct Inputs {
        std::uint32_t inviter_id = 0;
        std::uint32_t target_id = 0;
        bool target_online = true;
        bool target_in_party = false;
        bool inviter_has_party = false;
        bool inviter_is_leader = true;   // only meaningful when inviter_has_party
        int  inviter_party_size = 0;     // only meaningful when inviter_has_party
    };

    [[nodiscard]] static constexpr Result check(const Inputs& in) noexcept {
        if (in.inviter_id == in.target_id)     return Result::SelfInvite;
        if (!in.target_online)                 return Result::TargetOffline;
        if (in.target_in_party)                return Result::TargetAlreadyInAParty;
        if (in.inviter_has_party) {
            if (!in.inviter_is_leader)                      return Result::InviterNotLeader;
            if (in.inviter_party_size >= PARTY_MAX_MEMBERS) return Result::PartyFull;
        }
        return Result::Ok;
    }
};
static_assert(Rule<PartyInviteRule>);

} // namespace mmo::server::rules

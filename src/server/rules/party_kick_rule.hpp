#pragma once

// PartyKickRule: decides whether the party leader can kick a specific
// target. See rule_concept.hpp for the Rule contract.

#include "server/rules/rule_concept.hpp"

#include <cstdint>

namespace mmo::server::rules {

class PartyKickRule {
public:
    enum class Result : std::uint8_t {
        Ok,
        NotLeader,
        TargetNotInParty,
        SelfKick,
    };

    struct Inputs {
        std::uint32_t kicker_id = 0;
        std::uint32_t target_id = 0;
        bool kicker_is_leader = false;
        bool target_in_same_party = true;
    };

    [[nodiscard]] static constexpr Result check(const Inputs& in) noexcept {
        if (in.kicker_id == in.target_id) {
            return Result::SelfKick;
        }
        if (!in.kicker_is_leader) {
            return Result::NotLeader;
        }
        if (!in.target_in_same_party) {
            return Result::TargetNotInParty;
        }
        return Result::Ok;
    }
};
static_assert(Rule<PartyKickRule>);

} // namespace mmo::server::rules

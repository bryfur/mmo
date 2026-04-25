#pragma once

// PartyLeadership: scalar queries about party state. Not a Rule (no
// Inputs/Result/check triad), just a grouping of helpers for deciding who
// becomes leader after someone leaves and whether a party should disband.

#include <cstddef>
#include <cstdint>
#include <span>

namespace mmo::server::rules {

class PartyLeadership {
public:
    /// Who becomes leader after the current leader leaves?
    /// `remaining_members` is the member list with the leaver removed.
    /// Returns 0 (meaning "disband") if the party would be solo or empty.
    [[nodiscard]] static constexpr std::uint32_t
    promote_new_leader(std::span<const std::uint32_t> remaining_members) noexcept {
        if (remaining_members.size() < 2) {
            return 0;
        }
        return remaining_members.front();
    }

    /// Should a party be disbanded? Parties with one or zero members
    /// aren't parties.
    [[nodiscard]] static constexpr bool should_disband(std::size_t member_count) noexcept { return member_count < 2; }
};

} // namespace mmo::server::rules

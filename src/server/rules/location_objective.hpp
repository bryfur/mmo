#pragma once

// LocationObjective: evaluates a player-position update against a
// visit/explore quest objective. Proximity-based; completes on first
// successful check. See rule_concept.hpp for the Rule contract.

#include "server/rules/objective.hpp"
#include "server/rules/rule_concept.hpp"

#include <cstdint>

namespace mmo::server::rules {

class LocationObjective {
public:
    enum class Result : std::uint8_t {
        Ok,            // player is inside the objective circle this tick
        NotApplicable, // wrong type, or already complete
        OutsideArea,
    };

    struct Inputs {
        ObjectiveDef def;
        ObjectiveState state;
        float player_x = 0.0f;
        float player_z = 0.0f;
    };

    [[nodiscard]] static constexpr Result check(const Inputs& in) noexcept {
        if (in.state.complete) {
            return Result::NotApplicable;
        }
        if (in.def.type != "visit" && in.def.type != "explore") {
            return Result::NotApplicable;
        }
        if (!in_objective_area(in.def, in.player_x, in.player_z)) {
            return Result::OutsideArea;
        }
        return Result::Ok;
    }
};
static_assert(Rule<LocationObjective>);

} // namespace mmo::server::rules

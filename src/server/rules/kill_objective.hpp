#pragma once

// KillObjective: evaluates a KillEvent against a kill/kill_in_area quest
// objective. Returns a granular Result enum (see rule_concept.hpp).
//
// This Rule is slightly richer than most: it exposes a
// progress_delta_on_ok() helper so orchestrators don't need to hard-code
// "+1 per kill" in multiple places, and would_complete() for callers that
// need to know before committing progress.

#include "server/rules/objective.hpp"
#include "server/rules/rule_concept.hpp"

#include <cstdint>
#include <string_view>

namespace mmo::server::rules {

class KillObjective {
public:
    enum class Result : std::uint8_t {
        Ok,             // progress_delta > 0; this event advances the objective
        NotApplicable,  // wrong objective type or already complete
        TargetMismatch, // objective target doesn't match killed monster
        OutsideArea,    // kill_in_area kill was outside the circle
    };

    struct Inputs {
        ObjectiveDef def;
        ObjectiveState state;
        std::string_view monster_type_id;
        float kill_x = 0.0f;
        float kill_z = 0.0f;
    };

    [[nodiscard]] static constexpr Result check(const Inputs& in) noexcept {
        if (in.state.complete) {
            return Result::NotApplicable;
        }
        if (in.def.type != "kill" && in.def.type != "kill_in_area") {
            return Result::NotApplicable;
        }
        if (!kill_target_matches(in.def.target, in.monster_type_id)) {
            return Result::TargetMismatch;
        }
        if (in.def.type == "kill_in_area" && !in_objective_area(in.def, in.kill_x, in.kill_z)) {
            return Result::OutsideArea;
        }
        return Result::Ok;
    }

    /// Applied delta when check returns Ok; kept separate from check() so
    /// callers don't need to branch to know how much to advance.
    [[nodiscard]] static constexpr int progress_delta_on_ok() noexcept { return 1; }

    /// Would this event complete the objective if applied?
    [[nodiscard]] static constexpr bool would_complete(const Inputs& in) noexcept {
        return check(in) == Result::Ok && in.state.current + 1 >= in.def.required;
    }
};
static_assert(Rule<KillObjective>);

} // namespace mmo::server::rules

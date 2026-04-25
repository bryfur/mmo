#pragma once

// SkillGate: a Rule (see rule_contract.hpp) that decides whether a skill
// use is legal right now. Pure decision — no ECS, no registry, no RNG.

#include "server/rules/rule_concept.hpp"

#include <cstdint>
#include <string_view>

namespace mmo::server::rules {

class SkillGate {
public:
    enum class Result : std::uint8_t {
        Ok,
        NoSuchSkill,
        CasterDead,
        WrongClass,
        LevelTooLow,
        OnCooldown,
        InsufficientMana,
    };

    struct Inputs {
        bool skill_exists = true; // false if SkillConfig lookup failed
        bool caster_alive = true;
        // Class tokens from skills.json: "warrior", "mage", "paladin", "archer".
        std::string_view caster_class;
        std::string_view skill_class;
        int caster_level = 1;
        int skill_unlock_level = 1;
        float current_cooldown = 0.0f; // seconds remaining on the skill
        float caster_mana = 0.0f;
        float mana_cost = 0.0f;
    };

    [[nodiscard]] static constexpr Result check(const Inputs& in) noexcept {
        if (!in.skill_exists) {
            return Result::NoSuchSkill;
        }
        if (!in.caster_alive) {
            return Result::CasterDead;
        }
        if (in.skill_class != in.caster_class) {
            return Result::WrongClass;
        }
        if (in.caster_level < in.skill_unlock_level) {
            return Result::LevelTooLow;
        }
        if (in.current_cooldown > 0.0f) {
            return Result::OnCooldown;
        }
        if (in.caster_mana < in.mana_cost) {
            return Result::InsufficientMana;
        }
        return Result::Ok;
    }

    /// Effective cooldown after talent CDR + multiplier. Floor of 0.5s
    /// prevents stacking from trivializing cooldowns entirely.
    [[nodiscard]] static constexpr float effective_cooldown(float base_cooldown, float cooldown_mult,
                                                            float global_cdr) noexcept {
        float c = base_cooldown * cooldown_mult * (1.0f - global_cdr);
        return (c < 0.5f) ? 0.5f : c;
    }

    /// Effective mana cost after talent reduction. Never negative.
    [[nodiscard]] static constexpr float effective_mana_cost(float base_mana_cost, float mana_cost_mult) noexcept {
        float c = base_mana_cost * mana_cost_mult;
        return (c < 0.0f) ? 0.0f : c;
    }
};

// Compile-time contract check: violating the Rule shape produces a
// compile error pointing right here.
static_assert(Rule<SkillGate>);

} // namespace mmo::server::rules

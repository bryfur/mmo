#pragma once

// DamagePipeline: the numerical pipeline that turns a raw attacker-side
// damage roll into a defender-side health delta. Not a Rule - it's pure
// math that the combat orchestrator composes step-by-step.

namespace mmo::server::math {

class DamagePipeline {
public:
    /// An attacker-side damage roll ready to apply to a target.
    struct Roll {
        float base_damage = 0.0f;
        bool  is_crit = false;
        float crit_damage_mult = 1.5f;

        [[nodiscard]] constexpr float final_damage() const noexcept {
            return is_crit ? base_damage * crit_damage_mult : base_damage;
        }
    };

    /// Defender-side mitigation inputs.
    struct Mitigation {
        float equipment_defense = 0.0f;   // flat subtraction
        float defense_buff_mult = 1.0f;   // BuffState::get_defense_multiplier
        float talent_defense_mult = 1.0f; // TalentPassiveState::defense_mult
        bool  is_invulnerable = false;
    };

    /// Result of piping damage through a shield pool.
    struct ShieldAbsorbResult {
        float damage_to_health = 0.0f;
        float shield_consumed = 0.0f;
    };

    /// Result of applying damage to a health pool.
    struct HealthApplyResult {
        float new_health = 0.0f;
        bool  died = false;                  // killing blow this tick
        bool  cheat_death_eligible = false;  // died && has_cheat_death
    };

    /// Roll a damage value, deciding crit from a uniform [0, 1) random.
    /// Keeping the RNG as an input parameter lets tests pass a known value.
    [[nodiscard]] static constexpr Roll roll_with_crit(
        float base_damage, float crit_chance,
        float crit_damage_mult, float uniform_01) noexcept {
        return Roll{
            .base_damage = base_damage,
            .is_crit = (crit_chance > 0.0f) && (uniform_01 < crit_chance),
            .crit_damage_mult = crit_damage_mult,
        };
    }

    /// Pipe a Roll through defender mitigation. Returns 0 if invulnerable,
    /// otherwise a floor of 1 damage (cannot be fully blocked by def).
    [[nodiscard]] static constexpr float apply_mitigation(
        Roll roll, const Mitigation& m) noexcept {
        if (m.is_invulnerable) return 0.0f;
        float d = roll.final_damage();
        d *= m.defense_buff_mult;
        d *= m.talent_defense_mult;
        d -= m.equipment_defense;
        return (d < 1.0f) ? 1.0f : d;
    }

    /// Absorb damage through a shield pool. Residual spills to health.
    [[nodiscard]] static constexpr ShieldAbsorbResult absorb_with_shield(
        float damage, float shield_value) noexcept {
        ShieldAbsorbResult r;
        if (damage <= 0.0f || shield_value <= 0.0f) {
            r.damage_to_health = (damage > 0.0f) ? damage : 0.0f;
            return r;
        }
        r.shield_consumed = (shield_value < damage) ? shield_value : damage;
        r.damage_to_health = damage - r.shield_consumed;
        return r;
    }

    /// Apply damage to a health pool, computing death + cheat-death
    /// eligibility. max_health reserved for future clamping rules.
    [[nodiscard]] static constexpr HealthApplyResult apply_to_health(
        float current_health, float /*max_health*/,
        float damage, bool has_cheat_death) noexcept {
        HealthApplyResult r;
        bool was_alive = current_health > 0.0f;
        float new_hp = current_health - damage;
        if (new_hp < 0.0f) new_hp = 0.0f;
        r.new_health = new_hp;
        r.died = was_alive && (new_hp <= 0.0f);
        r.cheat_death_eligible = r.died && has_cheat_death;
        return r;
    }

    /// Lifesteal heal from a damage amount (0..1 pct).
    [[nodiscard]] static constexpr float compute_lifesteal_heal(
        float damage_dealt, float lifesteal_pct) noexcept {
        if (lifesteal_pct <= 0.0f || damage_dealt <= 0.0f) return 0.0f;
        return damage_dealt * lifesteal_pct;
    }
};

} // namespace mmo::server::math

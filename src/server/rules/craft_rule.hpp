#pragma once

// CraftRule: decides whether a craft request is legal given the player's
// level, gold, inventory ingredients, and proximity to a station. The
// server.cpp orchestrator snapshots state into Inputs, calls check(), and
// only mutates registry state on Ok.

#include "server/rules/rule_concept.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mmo::server::rules {

class CraftRule {
public:
    /// How far a player can stand from a station NPC and still craft there.
    static constexpr float MAX_STATION_DIST = 400.0f;

    enum class Result : std::uint8_t {
        Ok,
        UnknownRecipe,
        LevelTooLow,
        NotEnoughGold,
        MissingIngredient,
        InventoryFull,
        NotNearStation,
    };

    /// One ingredient requirement + how much the player currently has.
    struct Ingredient {
        std::string item_id;
        int required = 0;
        int available = 0;
    };

    struct Inputs {
        bool recipe_exists = true;
        int recipe_required_level = 1;
        int caster_level = 1;
        int gold_cost = 0;
        int player_gold = 0;
        std::vector<Ingredient> ingredients;
        bool inventory_has_room_for_output = true;

        // Empty station or "any" means no proximity check is needed.
        std::string_view station_requirement;
        bool near_matching_station = false;
    };

    [[nodiscard]] static Result check(const Inputs& in) noexcept {
        if (!in.recipe_exists) {
            return Result::UnknownRecipe;
        }
        if (in.caster_level < in.recipe_required_level) {
            return Result::LevelTooLow;
        }
        if (in.player_gold < in.gold_cost) {
            return Result::NotEnoughGold;
        }
        for (const auto& ing : in.ingredients) {
            if (ing.available < ing.required) {
                return Result::MissingIngredient;
            }
        }
        if (!in.station_requirement.empty() && in.station_requirement != "any" && !in.near_matching_station) {
            return Result::NotNearStation;
        }
        if (!in.inventory_has_room_for_output) {
            return Result::InventoryFull;
        }
        return Result::Ok;
    }

    /// True if the player's position is within MAX_STATION_DIST of a
    /// station at (sx, sz). Used to compute Inputs.near_matching_station.
    [[nodiscard]] static constexpr bool near_station(float px, float pz, float sx, float sz) noexcept {
        float dx = px - sx;
        float dz = pz - sz;
        return dx * dx + dz * dz <= MAX_STATION_DIST * MAX_STATION_DIST;
    }
};
static_assert(Rule<CraftRule>);

} // namespace mmo::server::rules

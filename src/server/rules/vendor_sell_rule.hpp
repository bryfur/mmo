#pragma once

// VendorSellRule: decides whether a vendor sell is legal. See
// rule_concept.hpp for the Rule contract.

#include "server/rules/rule_concept.hpp"
#include "server/rules/vendor_pricing.hpp"  // for within_range + MAX_INTERACT_DIST

#include <cstdint>

namespace mmo::server::rules {

class VendorSellRule {
public:
    enum class Result : std::uint8_t {
        Ok,
        OutOfRange,
        EmptySlot,
        UnknownItem,
        InsufficientQuantity,
    };

    struct Inputs {
        float player_x = 0.0f;
        float player_z = 0.0f;
        float npc_x = 0.0f;
        float npc_z = 0.0f;
        bool  slot_occupied = true;
        bool  item_exists = true;
        int   slot_count = 0;
        int   quantity = 1;
    };

    [[nodiscard]] static constexpr Result check(const Inputs& in) noexcept {
        if (!VendorPricing::within_range(in.player_x, in.player_z, in.npc_x, in.npc_z))
            return Result::OutOfRange;
        if (!in.slot_occupied)  return Result::EmptySlot;
        if (!in.item_exists)    return Result::UnknownItem;
        if (in.quantity <= 0 || in.slot_count < in.quantity)
            return Result::InsufficientQuantity;
        return Result::Ok;
    }
};
static_assert(Rule<VendorSellRule>);

} // namespace mmo::server::rules

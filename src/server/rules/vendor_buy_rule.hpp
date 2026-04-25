#pragma once

// VendorBuyRule: decides whether a vendor purchase is legal. See
// rule_concept.hpp for the Rule contract; server.cpp owns the orchestrator
// that snapshots state into Inputs, calls check, then mutates registry.

#include "server/rules/rule_concept.hpp"
#include "server/rules/vendor_pricing.hpp" // for within_range + MAX_INTERACT_DIST

#include <cstdint>

namespace mmo::server::rules {

class VendorBuyRule {
public:
    enum class Result : std::uint8_t {
        Ok,
        OutOfRange,
        UnknownItem,
        OutOfStock,
        NotEnoughGold,
        InventoryFull,
    };

    struct Inputs {
        float player_x = 0.0f;
        float player_z = 0.0f;
        float npc_x = 0.0f;
        float npc_z = 0.0f;
        bool item_exists = true;
        int available_stock = -1; // -1 = infinite
        int quantity = 1;
        int unit_price = 0;
        int player_gold = 0;
        bool inventory_has_room = true;
    };

    [[nodiscard]] static constexpr Result check(const Inputs& in) noexcept {
        if (!VendorPricing::within_range(in.player_x, in.player_z, in.npc_x, in.npc_z)) {
            return Result::OutOfRange;
        }
        if (!in.item_exists) {
            return Result::UnknownItem;
        }
        if (in.quantity <= 0) {
            return Result::UnknownItem;
        }
        if (in.available_stock >= 0 && in.quantity > in.available_stock) {
            return Result::OutOfStock;
        }
        int total = in.unit_price * in.quantity;
        if (in.player_gold < total) {
            return Result::NotEnoughGold;
        }
        if (!in.inventory_has_room) {
            return Result::InventoryFull;
        }
        return Result::Ok;
    }
};
static_assert(Rule<VendorBuyRule>);

} // namespace mmo::server::rules

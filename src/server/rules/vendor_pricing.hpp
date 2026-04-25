#pragma once

// VendorPricing: pure price computation + interact-distance helper used by
// VendorBuyRule and VendorSellRule. No contract (not a Rule, not a Formula
// with a single Inputs/Output); it's a utility grouping for vendor math.

namespace mmo::server::rules {

class VendorPricing {
public:
    /// Maximum interact distance between a player and a vendor NPC.
    static constexpr float MAX_INTERACT_DIST = 250.0f;

    /// Unit price of a vendor-listed item in gold.
    /// - Explicit `stock_override_price` (if > 0) wins.
    /// - Otherwise derive from item_sell_value * buy_price_multiplier.
    /// - Floors at 1 so every item costs at least 1 gold.
    [[nodiscard]] static constexpr int unit_buy_price(int stock_override_price,
                                                      int item_sell_value,
                                                      float buy_price_multiplier) noexcept {
        if (stock_override_price > 0) return stock_override_price;
        float raw = static_cast<float>(item_sell_value) * buy_price_multiplier;
        int p = static_cast<int>(raw);
        return (p < 1) ? 1 : p;
    }

    /// Unit price the vendor pays when the player sells.
    [[nodiscard]] static constexpr int unit_sell_price(int item_sell_value,
                                                        float sell_price_multiplier) noexcept {
        float raw = static_cast<float>(item_sell_value) * sell_price_multiplier;
        int p = static_cast<int>(raw);
        return (p < 1) ? 1 : p;
    }

    /// Squared distance helper; shared by both buy and sell range checks.
    [[nodiscard]] static constexpr bool within_range(
        float px, float pz, float nx, float nz) noexcept {
        float dx = px - nx;
        float dz = pz - nz;
        return dx * dx + dz * dz <= MAX_INTERACT_DIST * MAX_INTERACT_DIST;
    }
};

} // namespace mmo::server::rules

#include "protocol/gameplay_msgs.hpp"
#include "protocol/buffer_reader.hpp"
#include "protocol/buffer_writer.hpp"
#include <cstring>
#include <gtest/gtest.h>

using namespace mmo::protocol;

template<typename T> static void round_trip(const T& in, T& out) {
    std::vector<uint8_t> buf(T::serialized_size());
    in.serialize(buf);
    out.deserialize(buf);
}

// ============================================================================
// Chat
// ============================================================================

TEST(ChatProtocol, SendRoundTrip) {
    ChatSendMsg a;
    a.channel = 2; // Global
    std::strncpy(a.message, "hello world!", sizeof(a.message) - 1);

    ChatSendMsg b;
    round_trip(a, b);

    EXPECT_EQ(a.channel, b.channel);
    EXPECT_STREQ(a.message, b.message);
}

TEST(ChatProtocol, BroadcastRoundTrip) {
    ChatBroadcastMsg a;
    a.channel = static_cast<uint8_t>(ChatChannel::Whisper);
    a.sender_id = 999;
    std::strncpy(a.sender_name, "Alice", sizeof(a.sender_name) - 1);
    std::strncpy(a.message, "secret message", sizeof(a.message) - 1);

    ChatBroadcastMsg b;
    round_trip(a, b);

    EXPECT_EQ(a.channel, b.channel);
    EXPECT_EQ(a.sender_id, b.sender_id);
    EXPECT_STREQ(a.sender_name, b.sender_name);
    EXPECT_STREQ(a.message, b.message);
}

// ============================================================================
// Vendor
// ============================================================================

TEST(VendorProtocol, OpenRoundTripWithMultipleStock) {
    VendorOpenMsg a;
    a.npc_id = 42;
    std::strncpy(a.vendor_name, "Forge & Arms", sizeof(a.vendor_name) - 1);
    a.buy_price_multiplier = 4.5f;
    a.sell_price_multiplier = 0.3f;
    a.stock_count = 3;
    std::strncpy(a.stock[0].item_id, "iron_sword", sizeof(a.stock[0].item_id) - 1);
    std::strncpy(a.stock[0].item_name, "Iron Sword", sizeof(a.stock[0].item_name) - 1);
    std::strncpy(a.stock[0].rarity, "common", sizeof(a.stock[0].rarity) - 1);
    a.stock[0].price = 40;
    a.stock[0].stock = -1;
    std::strncpy(a.stock[1].item_id, "steel_sword", sizeof(a.stock[1].item_id) - 1);
    a.stock[1].price = 175;
    std::strncpy(a.stock[2].item_id, "chain_mail", sizeof(a.stock[2].item_id) - 1);
    a.stock[2].price = 90;
    a.stock[2].stock = 3;

    VendorOpenMsg b;
    round_trip(a, b);

    EXPECT_EQ(a.npc_id, b.npc_id);
    EXPECT_STREQ(a.vendor_name, b.vendor_name);
    EXPECT_FLOAT_EQ(a.buy_price_multiplier, b.buy_price_multiplier);
    EXPECT_FLOAT_EQ(a.sell_price_multiplier, b.sell_price_multiplier);
    EXPECT_EQ(a.stock_count, b.stock_count);
    for (int i = 0; i < a.stock_count; ++i) {
        EXPECT_STREQ(a.stock[i].item_id, b.stock[i].item_id);
        EXPECT_STREQ(a.stock[i].item_name, b.stock[i].item_name);
        EXPECT_STREQ(a.stock[i].rarity, b.stock[i].rarity);
        EXPECT_EQ(a.stock[i].price, b.stock[i].price);
        EXPECT_EQ(a.stock[i].stock, b.stock[i].stock);
    }
}

TEST(VendorProtocol, BuyRoundTrip) {
    VendorBuyMsg a;
    a.npc_id = 42;
    a.stock_index = 3;
    a.quantity = 5;
    VendorBuyMsg b;
    round_trip(a, b);
    EXPECT_EQ(a.npc_id, b.npc_id);
    EXPECT_EQ(a.stock_index, b.stock_index);
    EXPECT_EQ(a.quantity, b.quantity);
}

TEST(VendorProtocol, SellRoundTrip) {
    VendorSellMsg a;
    a.npc_id = 1;
    a.inventory_slot = 7;
    a.quantity = 1;
    VendorSellMsg b;
    round_trip(a, b);
    EXPECT_EQ(a.npc_id, b.npc_id);
    EXPECT_EQ(a.inventory_slot, b.inventory_slot);
    EXPECT_EQ(a.quantity, b.quantity);
}

// ============================================================================
// Party
// ============================================================================

TEST(PartyProtocol, InviteRoundTrip) {
    PartyInviteMsg a;
    std::strncpy(a.target_name, "Alice", sizeof(a.target_name) - 1);
    PartyInviteMsg b;
    round_trip(a, b);
    EXPECT_STREQ(a.target_name, b.target_name);
}

TEST(PartyProtocol, InviteOfferRoundTrip) {
    PartyInviteOfferMsg a;
    a.inviter_id = 1234;
    std::strncpy(a.inviter_name, "Bob", sizeof(a.inviter_name) - 1);
    PartyInviteOfferMsg b;
    round_trip(a, b);
    EXPECT_EQ(a.inviter_id, b.inviter_id);
    EXPECT_STREQ(a.inviter_name, b.inviter_name);
}

TEST(PartyProtocol, StateRoundTripWith3Members) {
    PartyStateMsg a;
    a.leader_id = 100;
    a.member_count = 3;
    a.members[0].player_id = 100;
    std::strncpy(a.members[0].name, "Leader", sizeof(a.members[0].name) - 1);
    a.members[0].player_class = 0;
    a.members[0].level = 15;
    a.members[0].health = 200.0f;
    a.members[0].max_health = 220.0f;
    a.members[0].mana = 50.0f;
    a.members[0].max_mana = 80.0f;
    a.members[1].player_id = 101;
    a.members[1].level = 12;
    a.members[2].player_id = 102;
    a.members[2].level = 8;

    PartyStateMsg b;
    round_trip(a, b);

    EXPECT_EQ(a.leader_id, b.leader_id);
    EXPECT_EQ(a.member_count, b.member_count);
    EXPECT_EQ(a.members[0].player_id, b.members[0].player_id);
    EXPECT_STREQ(a.members[0].name, b.members[0].name);
    EXPECT_EQ(a.members[0].level, b.members[0].level);
    EXPECT_FLOAT_EQ(a.members[0].health, b.members[0].health);
    EXPECT_EQ(a.members[1].player_id, b.members[1].player_id);
    EXPECT_EQ(a.members[2].player_id, b.members[2].player_id);
}

// ============================================================================
// Crafting
// ============================================================================

TEST(CraftingProtocol, RecipeRoundTrip) {
    CraftRecipeInfo a;
    std::strncpy(a.id, "craft_iron_sword", sizeof(a.id) - 1);
    std::strncpy(a.name, "Iron Sword", sizeof(a.name) - 1);
    std::strncpy(a.output_item_id, "iron_sword", sizeof(a.output_item_id) - 1);
    a.output_count = 1;
    a.gold_cost = 5;
    a.required_level = 1;
    a.ingredient_count = 2;
    std::strncpy(a.ingredients[0].item_id, "iron_ore", sizeof(a.ingredients[0].item_id) - 1);
    a.ingredients[0].count = 2;
    std::strncpy(a.ingredients[1].item_id, "rough_wood", sizeof(a.ingredients[1].item_id) - 1);
    a.ingredients[1].count = 1;

    CraftRecipeInfo b;
    round_trip(a, b);

    EXPECT_STREQ(a.id, b.id);
    EXPECT_STREQ(a.output_item_id, b.output_item_id);
    EXPECT_EQ(a.output_count, b.output_count);
    EXPECT_EQ(a.gold_cost, b.gold_cost);
    EXPECT_EQ(a.required_level, b.required_level);
    EXPECT_EQ(a.ingredient_count, b.ingredient_count);
    EXPECT_STREQ(a.ingredients[0].item_id, b.ingredients[0].item_id);
    EXPECT_EQ(a.ingredients[0].count, b.ingredients[0].count);
    EXPECT_STREQ(a.ingredients[1].item_id, b.ingredients[1].item_id);
    EXPECT_EQ(a.ingredients[1].count, b.ingredients[1].count);
}

TEST(CraftingProtocol, ResultRoundTrip) {
    CraftResultMsg a;
    std::strncpy(a.recipe_id, "craft_iron_sword", sizeof(a.recipe_id) - 1);
    a.success = 0;
    std::strncpy(a.reason, "missing iron_ore", sizeof(a.reason) - 1);

    CraftResultMsg b;
    round_trip(a, b);

    EXPECT_STREQ(a.recipe_id, b.recipe_id);
    EXPECT_EQ(a.success, b.success);
    EXPECT_STREQ(a.reason, b.reason);
}

// ============================================================================
// Packet header integrity
// ============================================================================

#include "protocol/packet.hpp"

TEST(PacketHeader, RoundTripWithVendorPayload) {
    VendorBuyMsg src;
    src.npc_id = 7;
    src.stock_index = 2;
    src.quantity = 1;
    auto packet = build_packet(MessageType::VendorBuy, src);

    PacketHeader hdr{};
    hdr.deserialize(std::span<const uint8_t>(packet.data(), PacketHeader::serialized_size()));
    EXPECT_EQ(hdr.type, MessageType::VendorBuy);
    EXPECT_EQ(hdr.payload_size, VendorBuyMsg::serialized_size());

    VendorBuyMsg dst;
    dst.deserialize(std::span<const uint8_t>(packet.data() + PacketHeader::serialized_size(), hdr.payload_size));
    EXPECT_EQ(src.npc_id, dst.npc_id);
    EXPECT_EQ(src.stock_index, dst.stock_index);
    EXPECT_EQ(src.quantity, dst.quantity);
}

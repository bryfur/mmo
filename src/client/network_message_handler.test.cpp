#include "client/network_message_handler.hpp"
#include "protocol/buffer_writer.hpp"
#include "protocol/gameplay_msgs.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <unordered_set>
#include <vector>

namespace mmo::client {
namespace {

using namespace mmo::protocol;

// Owned fixture that produces a NetworkMessageHandler::Context bound to its
// own state — keeps each test independent and lets us inspect side effects.
struct HandlerFixture {
    HUDState hud;
    PanelState panel;
    NPCInteractionState npc;
    std::unordered_set<uint32_t> npcs_with_quests;
    std::unordered_set<uint32_t> npcs_with_turnins;
    uint32_t local_player_id = 42;
    bool player_dead = false;

    NetworkMessageHandler make_handler() {
        return NetworkMessageHandler(
            {hud, panel, npc, npcs_with_quests, npcs_with_turnins, local_player_id, player_dead});
    }
};

// Helper: serialize a Serializable<T> into a byte vector sized to the type.
template<typename T> std::vector<uint8_t> encode(const T& msg) {
    std::vector<uint8_t> bytes(msg.size());
    msg.serialize(std::span<uint8_t>(bytes));
    return bytes;
}

// ---------------------------------------------------------------------------
// XPGain
// ---------------------------------------------------------------------------

TEST(NetMsgHandler, XPGainUpdatesHudCounters) {
    HandlerFixture fx;
    fx.hud.level = 3;

    XPGainMsg msg;
    msg.xp_gained = 150;
    msg.total_xp = 450;
    msg.xp_to_next = 1000;
    msg.current_level = 3;
    auto bytes = encode(msg);

    auto handler = fx.make_handler();
    EXPECT_TRUE(handler.try_handle(MessageType::XPGain, bytes));

    EXPECT_EQ(fx.hud.xp, 450);
    EXPECT_EQ(fx.hud.xp_to_next_level, 1000);
    EXPECT_EQ(fx.hud.level, 3);
    EXPECT_FLOAT_EQ(fx.hud.level_up_timer, 0.0f); // no level-up overlay
}

TEST(NetMsgHandler, XPGainTriggersLevelUpOverlayOnLevelChange) {
    HandlerFixture fx;
    fx.hud.level = 4;

    XPGainMsg msg;
    msg.xp_gained = 200;
    msg.total_xp = 0;
    msg.xp_to_next = 1500;
    msg.current_level = 5;
    auto bytes = encode(msg);

    auto handler = fx.make_handler();
    handler.try_handle(MessageType::XPGain, bytes);

    EXPECT_EQ(fx.hud.level, 5);
    EXPECT_GT(fx.hud.level_up_timer, 0.0f);
    EXPECT_EQ(fx.hud.level_up_level, 5);
}

TEST(NetMsgHandler, XPGainIgnoresShortPayload) {
    HandlerFixture fx;
    fx.hud.xp = 99;
    auto handler = fx.make_handler();

    std::vector<uint8_t> short_buf(3, 0);
    EXPECT_TRUE(handler.try_handle(MessageType::XPGain, short_buf));
    EXPECT_EQ(fx.hud.xp, 99); // untouched
}

// ---------------------------------------------------------------------------
// LevelUp
// ---------------------------------------------------------------------------

TEST(NetMsgHandler, LevelUpUpdatesLevelAndMaxHealth) {
    HandlerFixture fx;
    fx.hud.max_health = 100.0f;

    LevelUpMsg msg;
    msg.new_level = 7;
    msg.new_max_health = 175.0f;
    msg.new_damage = 22.0f;
    auto bytes = encode(msg);

    fx.make_handler().try_handle(MessageType::LevelUp, bytes);

    EXPECT_EQ(fx.hud.level, 7);
    EXPECT_FLOAT_EQ(fx.hud.max_health, 175.0f);
    EXPECT_GT(fx.hud.level_up_timer, 0.0f);
}

// ---------------------------------------------------------------------------
// GoldChange
// ---------------------------------------------------------------------------

// GoldChange has no Serializable struct in the header; build the payload by
// hand with offset-mode BufferWriter (single-arg ctor would *append*).
std::vector<uint8_t> encode_gold_change(int32_t change, int32_t total) {
    std::vector<uint8_t> buf(2 * sizeof(int32_t));
    BufferWriter w(buf, 0);
    w.write(change);
    w.write(total);
    return buf;
}

TEST(NetMsgHandler, GoldChangeUpdatesTotalAndPushesPositiveLoot) {
    HandlerFixture fx;
    auto buf = encode_gold_change(42, 1042);

    fx.make_handler().try_handle(MessageType::GoldChange, buf);
    EXPECT_EQ(fx.hud.gold, 1042);
    EXPECT_EQ(fx.hud.loot_feed.size(), 1u);
    EXPECT_NE(fx.hud.loot_feed.front().text.find("42"), std::string::npos);
}

TEST(NetMsgHandler, GoldChangeNegativeDoesNotPushLoot) {
    HandlerFixture fx;
    auto buf = encode_gold_change(-10, 5);

    fx.make_handler().try_handle(MessageType::GoldChange, buf);
    EXPECT_EQ(fx.hud.gold, 5);
    EXPECT_TRUE(fx.hud.loot_feed.empty());
}

// ---------------------------------------------------------------------------
// QuestList — encodes (npc_id | turnin-bit) per entry
// ---------------------------------------------------------------------------

TEST(NetMsgHandler, QuestListPartitionsByTurninBit) {
    HandlerFixture fx;
    fx.npcs_with_quests.insert(99); // ensure prior state is cleared
    fx.npcs_with_turnins.insert(100);

    std::vector<uint8_t> buf;
    buf.resize(sizeof(uint16_t) + 3 * sizeof(uint32_t));
    uint16_t count = 3;
    std::memcpy(buf.data(), &count, sizeof(count));

    auto write_entry = [&](size_t idx, uint32_t npc_id, bool turnin) {
        const uint32_t bit = turnin ? 0x80000000u : 0u;
        const uint32_t encoded = (npc_id & 0x7FFFFFFFu) | bit;
        std::memcpy(buf.data() + sizeof(uint16_t) + idx * sizeof(uint32_t), &encoded, sizeof(uint32_t));
    };
    write_entry(0, 1, false);
    write_entry(1, 2, true);
    write_entry(2, 3, false);

    fx.make_handler().try_handle(MessageType::QuestList, buf);

    EXPECT_EQ(fx.npcs_with_quests, (std::unordered_set<uint32_t>{1, 3}));
    EXPECT_EQ(fx.npcs_with_turnins, (std::unordered_set<uint32_t>{2}));
}

// ---------------------------------------------------------------------------
// ZoneChange
// ---------------------------------------------------------------------------

TEST(NetMsgHandler, ZoneChangeStoresZoneAndStartsTimer) {
    HandlerFixture fx;
    std::vector<uint8_t> buf(64, 0);
    const std::string zone = "Whispering Woods";
    std::memcpy(buf.data(), zone.data(), zone.size());

    fx.make_handler().try_handle(MessageType::ZoneChange, buf);

    EXPECT_EQ(fx.hud.current_zone, zone);
    EXPECT_GT(fx.hud.zone_display_timer, 0.0f);
}

// ---------------------------------------------------------------------------
// QuestOffer — appended to the npc_interaction available list
// ---------------------------------------------------------------------------

TEST(NetMsgHandler, QuestOfferAppendsToInteractionState) {
    HandlerFixture fx;

    QuestOfferMsg msg{};
    std::strncpy(msg.quest_id, "kill_wolves", sizeof(msg.quest_id) - 1);
    std::strncpy(msg.quest_name, "Wolf Hunt", sizeof(msg.quest_name) - 1);
    std::strncpy(msg.description, "Cull the pack.", sizeof(msg.description) - 1);
    std::strncpy(msg.dialogue, "We need help.", sizeof(msg.dialogue) - 1);
    msg.xp_reward = 250;
    msg.gold_reward = 50;
    msg.objective_count = 1;
    std::strncpy(msg.objectives[0].description, "Slay 5 wolves", sizeof(msg.objectives[0].description) - 1);
    msg.objectives[0].count = 5;
    auto bytes = encode(msg);

    fx.make_handler().try_handle(MessageType::QuestOffer, bytes);

    ASSERT_EQ(fx.npc.available_quests.size(), 1u);
    const auto& q = fx.npc.available_quests.front();
    EXPECT_EQ(q.quest_id, "kill_wolves");
    EXPECT_EQ(q.quest_name, "Wolf Hunt");
    EXPECT_EQ(q.xp_reward, 250);
    EXPECT_EQ(q.gold_reward, 50);
    ASSERT_EQ(q.objectives.size(), 1u);
    EXPECT_EQ(q.objectives[0].count, 5);
}

// ---------------------------------------------------------------------------
// Unknown message
// ---------------------------------------------------------------------------

TEST(NetMsgHandler, UnknownTypeReturnsFalse) {
    HandlerFixture fx;
    std::vector<uint8_t> empty;
    // ConnectionAccepted is handled by Game, not the gameplay handler.
    EXPECT_FALSE(fx.make_handler().try_handle(MessageType::ConnectionAccepted, empty));
}

} // namespace
} // namespace mmo::client

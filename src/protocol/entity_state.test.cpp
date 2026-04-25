#include "protocol/entity_state.hpp"
#include "protocol/buffer_reader.hpp"
#include "protocol/buffer_writer.hpp"
#include "protocol/entity_delta.hpp"
#include <cstring>
#include <gtest/gtest.h>

using namespace mmo::protocol;

// ============================================================================
// NetEntityState wire round-trip
// ============================================================================

TEST(NetEntityStateTest, DefaultRoundTrip) {
    NetEntityState a;
    std::vector<uint8_t> buf(NetEntityState::serialized_size());
    a.serialize(buf);

    NetEntityState b;
    b.deserialize(buf);

    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.health, b.health);
    EXPECT_EQ(a.max_health, b.max_health);
    EXPECT_EQ(a.effects_mask, b.effects_mask);
}

TEST(NetEntityStateTest, PopulatedRoundTrip) {
    NetEntityState a;
    a.id = 12345;
    a.type = EntityType::NPC;
    a.player_class = 2;
    a.npc_type = 5;
    a.x = 100.5f;
    a.y = 25.0f;
    a.z = -200.75f;
    a.vx = 1.25f;
    a.vy = -0.5f;
    a.rotation = 1.5708f;
    a.health = 42.5f;
    a.max_health = 100.0f;
    a.color = 0xAABBCCDD;
    std::strncpy(a.name, "TestMonster", sizeof(a.name) - 1);
    a.is_attacking = true;
    a.attack_cooldown = 0.75f;
    a.attack_dir_x = 0.707f;
    a.attack_dir_y = -0.707f;
    a.scale = 1.5f;
    a.mana = 80.0f;
    a.max_mana = 120.0f;
    std::strncpy(a.model_name, "warrior_rigged", sizeof(a.model_name) - 1);
    a.target_size = 32.0f;
    std::strncpy(a.effect_type, "melee_swing", sizeof(a.effect_type) - 1);
    std::strncpy(a.animation, "humanoid", sizeof(a.animation) - 1);
    a.cone_angle = 0.5f;
    a.shows_reticle = true;
    a.effects_mask = NetEntityState::EffectStun | NetEntityState::EffectBurn;

    std::vector<uint8_t> buf(NetEntityState::serialized_size());
    a.serialize(buf);
    NetEntityState b;
    b.deserialize(buf);

    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.type, b.type);
    EXPECT_EQ(a.player_class, b.player_class);
    EXPECT_EQ(a.npc_type, b.npc_type);
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.z, b.z);
    EXPECT_FLOAT_EQ(a.rotation, b.rotation);
    EXPECT_FLOAT_EQ(a.health, b.health);
    EXPECT_FLOAT_EQ(a.max_health, b.max_health);
    EXPECT_EQ(a.color, b.color);
    EXPECT_STREQ(a.name, b.name);
    EXPECT_EQ(a.is_attacking, b.is_attacking);
    EXPECT_FLOAT_EQ(a.attack_cooldown, b.attack_cooldown);
    EXPECT_FLOAT_EQ(a.attack_dir_x, b.attack_dir_x);
    EXPECT_FLOAT_EQ(a.attack_dir_y, b.attack_dir_y);
    EXPECT_FLOAT_EQ(a.scale, b.scale);
    EXPECT_FLOAT_EQ(a.mana, b.mana);
    EXPECT_FLOAT_EQ(a.max_mana, b.max_mana);
    EXPECT_STREQ(a.model_name, b.model_name);
    EXPECT_FLOAT_EQ(a.target_size, b.target_size);
    EXPECT_STREQ(a.effect_type, b.effect_type);
    EXPECT_STREQ(a.animation, b.animation);
    EXPECT_FLOAT_EQ(a.cone_angle, b.cone_angle);
    EXPECT_EQ(a.shows_reticle, b.shows_reticle);
    EXPECT_EQ(a.effects_mask, b.effects_mask);
}

TEST(NetEntityStateTest, SerializedSizeMatchesActualWrite) {
    NetEntityState s;
    std::vector<uint8_t> buf(NetEntityState::serialized_size() + 16); // padding
    // Span-mode writer so offset starts at 0 (vector-mode is append-only).
    BufferWriter w(std::span<uint8_t>(buf.data(), buf.size()));
    s.serialize(w);
    EXPECT_EQ(w.offset(), NetEntityState::serialized_size());
}

// ============================================================================
// EntityDeltaUpdate - variable-size flags
// ============================================================================

TEST(EntityDeltaTest, EmptyDelta) {
    EntityDeltaUpdate a;
    a.id = 7;
    a.flags = 0;

    std::vector<uint8_t> buf(EntityDeltaUpdate::serialized_size(a.flags));
    a.serialize(buf);

    EntityDeltaUpdate b;
    b.deserialize(buf);

    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.flags, b.flags);
}

TEST(EntityDeltaTest, PositionOnly) {
    EntityDeltaUpdate a;
    a.id = 100;
    a.flags = EntityDeltaUpdate::FLAG_POSITION;
    a.x = 1.0f;
    a.y = 2.0f;
    a.z = 3.0f;

    std::vector<uint8_t> buf(EntityDeltaUpdate::serialized_size(a.flags));
    a.serialize(buf);

    EntityDeltaUpdate b;
    b.deserialize(buf);

    EXPECT_EQ(b.flags, EntityDeltaUpdate::FLAG_POSITION);
    EXPECT_FLOAT_EQ(b.x, 1.0f);
    EXPECT_FLOAT_EQ(b.y, 2.0f);
    EXPECT_FLOAT_EQ(b.z, 3.0f);
}

TEST(EntityDeltaTest, AllFlagsRoundTrip) {
    EntityDeltaUpdate a;
    a.id = 42;
    a.flags = EntityDeltaUpdate::FLAG_POSITION | EntityDeltaUpdate::FLAG_VELOCITY | EntityDeltaUpdate::FLAG_HEALTH |
              EntityDeltaUpdate::FLAG_MAX_HEALTH | EntityDeltaUpdate::FLAG_ATTACKING |
              EntityDeltaUpdate::FLAG_ATTACK_DIR | EntityDeltaUpdate::FLAG_ROTATION | EntityDeltaUpdate::FLAG_MANA |
              EntityDeltaUpdate::FLAG_EFFECTS;
    a.x = 1.0f;
    a.y = 2.0f;
    a.z = 3.0f;
    a.vx = 4.0f;
    a.vy = 5.0f;
    a.health = 50.0f;
    a.max_health = 150.0f;
    a.is_attacking = 1;
    a.attack_dir_x = 0.5f;
    a.attack_dir_y = -0.5f;
    a.rotation = 1.57f;
    a.mana = 200.0f;
    a.effects_mask = 0xABCD;

    std::vector<uint8_t> buf(EntityDeltaUpdate::serialized_size(a.flags));
    a.serialize(buf);

    EntityDeltaUpdate b;
    b.deserialize(buf);

    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.flags, b.flags);
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.z, b.z);
    EXPECT_FLOAT_EQ(a.vx, b.vx);
    EXPECT_FLOAT_EQ(a.vy, b.vy);
    EXPECT_FLOAT_EQ(a.health, b.health);
    EXPECT_FLOAT_EQ(a.max_health, b.max_health);
    EXPECT_EQ(a.is_attacking, b.is_attacking);
    EXPECT_FLOAT_EQ(a.attack_dir_x, b.attack_dir_x);
    EXPECT_FLOAT_EQ(a.attack_dir_y, b.attack_dir_y);
    EXPECT_FLOAT_EQ(a.rotation, b.rotation);
    EXPECT_FLOAT_EQ(a.mana, b.mana);
    EXPECT_EQ(a.effects_mask, b.effects_mask);
}

TEST(EntityDeltaTest, DifferentFlagCombinationsProduceDifferentSizes) {
    uint16_t flags_pos = EntityDeltaUpdate::FLAG_POSITION;
    uint16_t flags_all =
        EntityDeltaUpdate::FLAG_POSITION | EntityDeltaUpdate::FLAG_HEALTH | EntityDeltaUpdate::FLAG_EFFECTS;
    EXPECT_LT(EntityDeltaUpdate::serialized_size(flags_pos), EntityDeltaUpdate::serialized_size(flags_all));
}

TEST(EntityDeltaTest, EffectsMaskBitsPreserved) {
    EntityDeltaUpdate a;
    a.id = 1;
    a.flags = EntityDeltaUpdate::FLAG_EFFECTS;
    a.effects_mask = NetEntityState::EffectStun | NetEntityState::EffectSlow | NetEntityState::EffectBurn |
                     NetEntityState::EffectShield | NetEntityState::EffectDamageBoost |
                     NetEntityState::EffectSpeedBoost | NetEntityState::EffectInvuln | NetEntityState::EffectDefBoost;

    std::vector<uint8_t> buf(EntityDeltaUpdate::serialized_size(a.flags));
    a.serialize(buf);
    EntityDeltaUpdate b;
    b.deserialize(buf);
    EXPECT_EQ(a.effects_mask, b.effects_mask);
}

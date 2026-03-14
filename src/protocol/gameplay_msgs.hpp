#pragma once

#include "serializable.hpp"

#include <cstdint>

namespace mmo::protocol {

// Server → Client: XP gained from an action
struct XPGainMsg : Serializable<XPGainMsg> {
    int32_t xp_gained = 0;
    int32_t total_xp = 0;
    int32_t xp_to_next = 0;
    int32_t current_level = 0;

    static constexpr size_t serialized_size() {
        return 4 * sizeof(int32_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(xp_gained);
        w.write(total_xp);
        w.write(xp_to_next);
        w.write(current_level);
    }

    void deserialize_impl(BufferReader& r) {
        xp_gained = r.read<int32_t>();
        total_xp = r.read<int32_t>();
        xp_to_next = r.read<int32_t>();
        current_level = r.read<int32_t>();
    }
};

// Server → Client: player leveled up
struct LevelUpMsg : Serializable<LevelUpMsg> {
    int32_t new_level = 0;
    float new_max_health = 0;
    float new_damage = 0;

    static constexpr size_t serialized_size() {
        return sizeof(int32_t) + 2 * sizeof(float);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(new_level);
        w.write(new_max_health);
        w.write(new_damage);
    }

    void deserialize_impl(BufferReader& r) {
        new_level = r.read<int32_t>();
        new_max_health = r.read<float>();
        new_damage = r.read<float>();
    }
};

// Server → Client: gold amount changed
struct GoldChangeMsg : Serializable<GoldChangeMsg> {
    int32_t gold_change = 0;  // +/- amount
    int32_t total_gold = 0;

    static constexpr size_t serialized_size() {
        return 2 * sizeof(int32_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(gold_change);
        w.write(total_gold);
    }

    void deserialize_impl(BufferReader& r) {
        gold_change = r.read<int32_t>();
        total_gold = r.read<int32_t>();
    }
};

// Server → Client: loot dropped from a kill
struct LootDropMsg : Serializable<LootDropMsg> {
    int32_t gold = 0;
    uint8_t item_count = 0;

    struct ItemEntry {
        char item_name[32] = {};
        char rarity[16] = {};
        uint8_t count = 0;
    };
    static constexpr int MAX_ITEMS = 5;
    ItemEntry items[MAX_ITEMS] = {};

    static constexpr size_t serialized_size() {
        return sizeof(int32_t) + sizeof(uint8_t) + MAX_ITEMS * (32 + 16 + sizeof(uint8_t));
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(gold);
        w.write(item_count);
        for (int i = 0; i < MAX_ITEMS; ++i) {
            w.write_bytes(items[i].item_name, 32);
            w.write_bytes(items[i].rarity, 16);
            w.write(items[i].count);
        }
    }

    void deserialize_impl(BufferReader& r) {
        gold = r.read<int32_t>();
        item_count = r.read<uint8_t>();
        for (int i = 0; i < MAX_ITEMS; ++i) {
            r.read_bytes(items[i].item_name, 32);
            r.read_bytes(items[i].rarity, 16);
            items[i].count = r.read<uint8_t>();
        }
    }
};

// Server → Client: NPC offers a quest
struct QuestOfferMsg : Serializable<QuestOfferMsg> {
    char quest_id[32] = {};
    char quest_name[64] = {};
    char description[256] = {};
    char dialogue[256] = {};
    int32_t xp_reward = 0;
    int32_t gold_reward = 0;
    uint8_t objective_count = 0;

    struct ObjectiveInfo {
        char description[64] = {};
        int32_t count = 0;
        float location_x = 0.0f;
        float location_z = 0.0f;
        float radius = 0.0f;
    };
    static constexpr int MAX_OBJECTIVES = 5;
    ObjectiveInfo objectives[MAX_OBJECTIVES] = {};

    static constexpr size_t serialized_size() {
        return 32 + 64 + 256 + 256
             + 2 * sizeof(int32_t) + sizeof(uint8_t)
             + MAX_OBJECTIVES * (64 + sizeof(int32_t) + 3 * sizeof(float));
    }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(quest_id, 32);
        w.write_bytes(quest_name, 64);
        w.write_bytes(description, 256);
        w.write_bytes(dialogue, 256);
        w.write(xp_reward);
        w.write(gold_reward);
        w.write(objective_count);
        for (int i = 0; i < MAX_OBJECTIVES; ++i) {
            w.write_bytes(objectives[i].description, 64);
            w.write(objectives[i].count);
            w.write(objectives[i].location_x);
            w.write(objectives[i].location_z);
            w.write(objectives[i].radius);
        }
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(quest_id, 32);
        r.read_bytes(quest_name, 64);
        r.read_bytes(description, 256);
        r.read_bytes(dialogue, 256);
        xp_reward = r.read<int32_t>();
        gold_reward = r.read<int32_t>();
        objective_count = r.read<uint8_t>();
        for (int i = 0; i < MAX_OBJECTIVES; ++i) {
            r.read_bytes(objectives[i].description, 64);
            objectives[i].count = r.read<int32_t>();
            objectives[i].location_x = r.read<float>();
            objectives[i].location_z = r.read<float>();
            objectives[i].radius = r.read<float>();
        }
    }
};

// Client → Server: accept a quest
struct QuestAcceptMsg : Serializable<QuestAcceptMsg> {
    char quest_id[32] = {};

    static constexpr size_t serialized_size() { return 32; }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(quest_id, 32);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(quest_id, 32);
    }
};

// Server → Client: quest objective progress update
struct QuestProgressMsg : Serializable<QuestProgressMsg> {
    char quest_id[32] = {};
    uint8_t objective_index = 0;
    int32_t current = 0;
    int32_t required = 0;
    uint8_t complete = 0;

    static constexpr size_t serialized_size() {
        return 32 + sizeof(uint8_t) + 2 * sizeof(int32_t) + sizeof(uint8_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(quest_id, 32);
        w.write(objective_index);
        w.write(current);
        w.write(required);
        w.write(complete);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(quest_id, 32);
        objective_index = r.read<uint8_t>();
        current = r.read<int32_t>();
        required = r.read<int32_t>();
        complete = r.read<uint8_t>();
    }
};

// Server → Client: quest completed notification
struct QuestCompleteMsg : Serializable<QuestCompleteMsg> {
    char quest_id[32] = {};
    char quest_name[64] = {};

    static constexpr size_t serialized_size() { return 32 + 64; }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(quest_id, 32);
        w.write_bytes(quest_name, 64);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(quest_id, 32);
        r.read_bytes(quest_name, 64);
    }
};

// Client → Server: turn in a completed quest to NPC
struct QuestTurnInMsg : Serializable<QuestTurnInMsg> {
    char quest_id[32] = {};

    static constexpr size_t serialized_size() { return 32; }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(quest_id, 32);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(quest_id, 32);
    }
};

// Client → Server: interact with an NPC
struct NPCInteractMsg : Serializable<NPCInteractMsg> {
    uint32_t npc_network_id = 0;

    static constexpr size_t serialized_size() { return sizeof(uint32_t); }

    void serialize_impl(BufferWriter& w) const {
        w.write(npc_network_id);
    }

    void deserialize_impl(BufferReader& r) {
        npc_network_id = r.read<uint32_t>();
    }
};

// Server → Client: NPC dialogue response
struct NPCDialogueMsg : Serializable<NPCDialogueMsg> {
    uint32_t npc_id = 0;
    char npc_name[32] = {};
    char dialogue[256] = {};
    uint8_t quest_count = 0;  // number of available quests (sent as QuestOfferMsgs)

    static constexpr size_t serialized_size() {
        return sizeof(uint32_t) + 32 + 256 + sizeof(uint8_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(npc_id);
        w.write_bytes(npc_name, 32);
        w.write_bytes(dialogue, 256);
        w.write(quest_count);
    }

    void deserialize_impl(BufferReader& r) {
        npc_id = r.read<uint32_t>();
        r.read_bytes(npc_name, 32);
        r.read_bytes(dialogue, 256);
        quest_count = r.read<uint8_t>();
    }
};

// Client → Server: use a skill
struct SkillUseMsg : Serializable<SkillUseMsg> {
    char skill_id[32] = {};
    float dir_x = 0;
    float dir_z = 0;

    static constexpr size_t serialized_size() {
        return 32 + 2 * sizeof(float);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(skill_id, 32);
        w.write(dir_x);
        w.write(dir_z);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(skill_id, 32);
        dir_x = r.read<float>();
        dir_z = r.read<float>();
    }
};

// Server → Client: skill cooldown/result update
struct SkillResultMsg : Serializable<SkillResultMsg> {
    char skill_id[32] = {};
    float cooldown = 0;
    uint8_t success = 0;

    static constexpr size_t serialized_size() {
        return 32 + sizeof(float) + sizeof(uint8_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(skill_id, 32);
        w.write(cooldown);
        w.write(success);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(skill_id, 32);
        cooldown = r.read<float>();
        success = r.read<uint8_t>();
    }
};

// Client → Server: unlock a talent
struct TalentUnlockMsg : Serializable<TalentUnlockMsg> {
    char talent_id[32] = {};

    static constexpr size_t serialized_size() { return 32; }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(talent_id, 32);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(talent_id, 32);
    }
};

// Server → Client: full talent state sync
struct TalentSyncMsg : Serializable<TalentSyncMsg> {
    int32_t talent_points = 0;
    uint8_t unlocked_count = 0;
    static constexpr int MAX_TALENTS = 18;
    char unlocked_ids[MAX_TALENTS][32] = {};

    static constexpr size_t serialized_size() {
        return sizeof(int32_t) + sizeof(uint8_t) + MAX_TALENTS * 32;
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(talent_points);
        w.write(unlocked_count);
        for (int i = 0; i < MAX_TALENTS; ++i) {
            w.write_bytes(unlocked_ids[i], 32);
        }
    }

    void deserialize_impl(BufferReader& r) {
        talent_points = r.read<int32_t>();
        unlocked_count = r.read<uint8_t>();
        for (int i = 0; i < MAX_TALENTS; ++i) {
            r.read_bytes(unlocked_ids[i], 32);
        }
    }
};

// Server → Client: player entered a new zone
struct ZoneChangeMsg : Serializable<ZoneChangeMsg> {
    char zone_name[64] = {};

    static constexpr size_t serialized_size() { return 64; }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(zone_name, 64);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(zone_name, 64);
    }
};

} // namespace mmo::protocol

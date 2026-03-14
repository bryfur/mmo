#pragma once

#include "serializable.hpp"
#include <cstdint>
#include <cstring>

namespace mmo::protocol {

// ============================================================================
// Combat
// ============================================================================

struct CombatEventMsg : Serializable<CombatEventMsg> {
    uint32_t attacker_id = 0;
    uint32_t target_id = 0;
    float damage = 0.0f;
    uint8_t is_heal = 0;     // 1 = heal, 0 = damage
    float target_x = 0.0f;
    float target_y = 0.0f;
    float target_z = 0.0f;

    static constexpr size_t serialized_size() {
        return sizeof(uint32_t) * 2 + sizeof(float) * 4 + sizeof(uint8_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(attacker_id);
        w.write(target_id);
        w.write(damage);
        w.write(is_heal);
        w.write(target_x);
        w.write(target_y);
        w.write(target_z);
    }

    void deserialize_impl(BufferReader& r) {
        attacker_id = r.read<uint32_t>();
        target_id = r.read<uint32_t>();
        damage = r.read<float>();
        is_heal = r.read<uint8_t>();
        target_x = r.read<float>();
        target_y = r.read<float>();
        target_z = r.read<float>();
    }
};

struct EntityDeathMsg : Serializable<EntityDeathMsg> {
    uint32_t entity_id = 0;
    uint32_t killer_id = 0;

    static constexpr size_t serialized_size() { return sizeof(uint32_t) * 2; }

    void serialize_impl(BufferWriter& w) const {
        w.write(entity_id);
        w.write(killer_id);
    }

    void deserialize_impl(BufferReader& r) {
        entity_id = r.read<uint32_t>();
        killer_id = r.read<uint32_t>();
    }
};

// ============================================================================
// Progression (server -> client)
// ============================================================================

// Server -> Client: XP gained from an action
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

// Server -> Client: player leveled up
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

// Server -> Client: gold amount changed
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

// Server -> Client: loot dropped from a kill
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

// ============================================================================
// Inventory
// ============================================================================

struct InventorySlot : Serializable<InventorySlot> {
    uint16_t item_id = 0;
    uint16_t count = 0;

    static constexpr size_t serialized_size() { return sizeof(uint16_t) * 2; }

    void serialize_impl(BufferWriter& w) const {
        w.write(item_id);
        w.write(count);
    }

    void deserialize_impl(BufferReader& r) {
        item_id = r.read<uint16_t>();
        count = r.read<uint16_t>();
    }
};

struct InventoryUpdateMsg : Serializable<InventoryUpdateMsg> {
    static constexpr int MAX_SLOTS = 20;
    InventorySlot slots[MAX_SLOTS] = {};
    uint8_t slot_count = 0;
    uint16_t equipped_weapon = 0;
    uint16_t equipped_armor = 0;

    static constexpr size_t serialized_size() {
        return sizeof(uint8_t) + MAX_SLOTS * InventorySlot::serialized_size() +
               sizeof(uint16_t) * 2;
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(slot_count);
        for (int i = 0; i < MAX_SLOTS; ++i) {
            slots[i].serialize(w);
        }
        w.write(equipped_weapon);
        w.write(equipped_armor);
    }

    void deserialize_impl(BufferReader& r) {
        slot_count = r.read<uint8_t>();
        for (int i = 0; i < MAX_SLOTS; ++i) {
            slots[i].deserialize(r);
        }
        equipped_weapon = r.read<uint16_t>();
        equipped_armor = r.read<uint16_t>();
    }
};

struct ItemEquipMsg : Serializable<ItemEquipMsg> {
    uint8_t slot_index = 0;  // Inventory slot to equip from

    static constexpr size_t serialized_size() { return sizeof(uint8_t); }

    void serialize_impl(BufferWriter& w) const { w.write(slot_index); }
    void deserialize_impl(BufferReader& r) { slot_index = r.read<uint8_t>(); }
};

struct ItemUnequipMsg : Serializable<ItemUnequipMsg> {
    uint8_t equip_slot = 0;  // 0 = weapon, 1 = armor

    static constexpr size_t serialized_size() { return sizeof(uint8_t); }

    void serialize_impl(BufferWriter& w) const { w.write(equip_slot); }
    void deserialize_impl(BufferReader& r) { equip_slot = r.read<uint8_t>(); }
};

struct ItemUseMsg : Serializable<ItemUseMsg> {
    uint8_t slot_index = 0;

    static constexpr size_t serialized_size() { return sizeof(uint8_t); }

    void serialize_impl(BufferWriter& w) const { w.write(slot_index); }
    void deserialize_impl(BufferReader& r) { slot_index = r.read<uint8_t>(); }
};

// ============================================================================
// Quests
// ============================================================================

// Server -> Client: NPC offers a quest
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

struct QuestProgressMsg : Serializable<QuestProgressMsg> {
    uint16_t quest_id = 0;
    uint8_t objective_index = 0;
    uint16_t current_count = 0;
    uint16_t required_count = 0;

    static constexpr size_t serialized_size() {
        return sizeof(uint16_t) * 3 + sizeof(uint8_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(quest_id);
        w.write(objective_index);
        w.write(current_count);
        w.write(required_count);
    }

    void deserialize_impl(BufferReader& r) {
        quest_id = r.read<uint16_t>();
        objective_index = r.read<uint8_t>();
        current_count = r.read<uint16_t>();
        required_count = r.read<uint16_t>();
    }
};

struct QuestCompleteMsg : Serializable<QuestCompleteMsg> {
    uint16_t quest_id = 0;
    char quest_name[32] = {};

    static constexpr size_t serialized_size() {
        return sizeof(uint16_t) + 32;
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(quest_id);
        w.write_bytes(quest_name, 32);
    }

    void deserialize_impl(BufferReader& r) {
        quest_id = r.read<uint16_t>();
        r.read_bytes(quest_name, 32);
    }
};

struct QuestAcceptMsg : Serializable<QuestAcceptMsg> {
    uint16_t quest_id = 0;

    static constexpr size_t serialized_size() { return sizeof(uint16_t); }

    void serialize_impl(BufferWriter& w) const { w.write(quest_id); }
    void deserialize_impl(BufferReader& r) { quest_id = r.read<uint16_t>(); }
};

// Client -> Server: turn in a completed quest to NPC
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

// ============================================================================
// Skills
// ============================================================================

struct SkillUseMsg : Serializable<SkillUseMsg> {
    uint16_t skill_id = 0;

    static constexpr size_t serialized_size() { return sizeof(uint16_t); }

    void serialize_impl(BufferWriter& w) const { w.write(skill_id); }
    void deserialize_impl(BufferReader& r) { skill_id = r.read<uint16_t>(); }
};

struct SkillCooldownMsg : Serializable<SkillCooldownMsg> {
    uint16_t skill_id = 0;
    float cooldown_remaining = 0.0f;
    float cooldown_total = 0.0f;

    static constexpr size_t serialized_size() {
        return sizeof(uint16_t) + sizeof(float) * 2;
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(skill_id);
        w.write(cooldown_remaining);
        w.write(cooldown_total);
    }

    void deserialize_impl(BufferReader& r) {
        skill_id = r.read<uint16_t>();
        cooldown_remaining = r.read<float>();
        cooldown_total = r.read<float>();
    }
};

struct SkillSlotInfo : Serializable<SkillSlotInfo> {
    uint16_t skill_id = 0;
    char name[16] = {};

    static constexpr size_t serialized_size() { return sizeof(uint16_t) + 16; }

    void serialize_impl(BufferWriter& w) const {
        w.write(skill_id);
        w.write_bytes(name, 16);
    }

    void deserialize_impl(BufferReader& r) {
        skill_id = r.read<uint16_t>();
        r.read_bytes(name, 16);
    }
};

struct SkillUnlockMsg : Serializable<SkillUnlockMsg> {
    static constexpr int MAX_SKILLS = 8;
    uint8_t skill_count = 0;
    SkillSlotInfo skills[MAX_SKILLS] = {};

    static constexpr size_t serialized_size() {
        return sizeof(uint8_t) + MAX_SKILLS * SkillSlotInfo::serialized_size();
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(skill_count);
        for (int i = 0; i < MAX_SKILLS; ++i) {
            skills[i].serialize(w);
        }
    }

    void deserialize_impl(BufferReader& r) {
        skill_count = r.read<uint8_t>();
        for (int i = 0; i < MAX_SKILLS; ++i) {
            skills[i].deserialize(r);
        }
    }
};

// ============================================================================
// Talents
// ============================================================================

struct TalentUnlockMsg : Serializable<TalentUnlockMsg> {
    uint16_t talent_id = 0;

    static constexpr size_t serialized_size() { return sizeof(uint16_t); }

    void serialize_impl(BufferWriter& w) const { w.write(talent_id); }
    void deserialize_impl(BufferReader& r) { talent_id = r.read<uint16_t>(); }
};

struct TalentSyncMsg : Serializable<TalentSyncMsg> {
    static constexpr int MAX_TALENTS = 16;
    uint8_t talent_points = 0;
    uint8_t unlocked_count = 0;
    uint16_t unlocked_talents[MAX_TALENTS] = {};

    static constexpr size_t serialized_size() {
        return sizeof(uint8_t) * 2 + MAX_TALENTS * sizeof(uint16_t);
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(talent_points);
        w.write(unlocked_count);
        for (int i = 0; i < MAX_TALENTS; ++i) {
            w.write(unlocked_talents[i]);
        }
    }

    void deserialize_impl(BufferReader& r) {
        talent_points = r.read<uint8_t>();
        unlocked_count = r.read<uint8_t>();
        for (int i = 0; i < MAX_TALENTS; ++i) {
            unlocked_talents[i] = r.read<uint16_t>();
        }
    }
};

// ============================================================================
// NPC Dialogue
// ============================================================================

struct NPCInteractMsg : Serializable<NPCInteractMsg> {
    uint32_t npc_id = 0;

    static constexpr size_t serialized_size() { return sizeof(uint32_t); }

    void serialize_impl(BufferWriter& w) const { w.write(npc_id); }
    void deserialize_impl(BufferReader& r) { npc_id = r.read<uint32_t>(); }
};

struct NPCDialogueMsg : Serializable<NPCDialogueMsg> {
    uint32_t npc_id = 0;
    char npc_name[32] = {};
    char dialogue[128] = {};
    uint8_t quest_count = 0;       // Number of quests this NPC offers
    uint16_t quest_ids[4] = {};    // Up to 4 quest IDs offered
    char quest_names[4][32] = {};  // Quest names

    static constexpr size_t serialized_size() {
        return sizeof(uint32_t) + 32 + 128 + sizeof(uint8_t) +
               4 * sizeof(uint16_t) + 4 * 32;
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(npc_id);
        w.write_bytes(npc_name, 32);
        w.write_bytes(dialogue, 128);
        w.write(quest_count);
        for (int i = 0; i < 4; ++i) {
            w.write(quest_ids[i]);
        }
        for (int i = 0; i < 4; ++i) {
            w.write_bytes(quest_names[i], 32);
        }
    }

    void deserialize_impl(BufferReader& r) {
        npc_id = r.read<uint32_t>();
        r.read_bytes(npc_name, 32);
        r.read_bytes(dialogue, 128);
        quest_count = r.read<uint8_t>();
        for (int i = 0; i < 4; ++i) {
            quest_ids[i] = r.read<uint16_t>();
        }
        for (int i = 0; i < 4; ++i) {
            r.read_bytes(quest_names[i], 32);
        }
    }
};

// ============================================================================
// Zone
// ============================================================================

// Server -> Client: player entered a new zone
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

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mmo {

enum class PlayerClass : uint8_t {
    Warrior = 0,
    Mage = 1,
    Paladin = 2,
    Archer = 3,
};

// NPC subtypes for different models
enum class NPCType : uint8_t {
    Monster = 0,
    Merchant = 1,
    Guard = 2,
    Blacksmith = 3,
    Innkeeper = 4,
    Villager = 5,
};

// Building types for town structures
enum class BuildingType : uint8_t {
    Tavern = 0,
    Blacksmith = 1,
    Tower = 2,
    Shop = 3,
    Well = 4,
    House = 5,
};

enum class EntityType : uint8_t {
    Player = 0,
    NPC = 1,          // Hostile NPCs (monsters)
    TownNPC = 2,      // Friendly town NPCs
    Building = 3,     // Static buildings
};

enum class MessageType : uint8_t {
    Connect = 1,
    Disconnect = 2,
    PlayerInput = 3,
    PlayerAttack = 4,
    
    ConnectionAccepted = 10,
    ConnectionRejected = 11,
    PlayerJoined = 12,
    PlayerLeft = 13,
    WorldState = 14,
    PlayerUpdate = 15,
    CombatEvent = 16,
    EntityDeath = 17,
};

struct PlayerInput {
    bool move_up = false;
    bool move_down = false;
    bool move_left = false;
    bool move_right = false;
    bool attacking = false;
    float attack_dir_x = 0.0f;  // Normalized attack direction from mouse
    float attack_dir_y = 1.0f;
    float move_dir_x = 0.0f;    // Continuous movement direction (normalized)
    float move_dir_y = 0.0f;    // For smooth camera-relative movement
    
    // Serialize movement + attack flag only (1 byte)
    uint8_t serialize_flags() const {
        uint8_t flags = 0;
        if (move_up) flags |= 0x01;
        if (move_down) flags |= 0x02;
        if (move_left) flags |= 0x04;
        if (move_right) flags |= 0x08;
        if (attacking) flags |= 0x10;
        return flags;
    }
    
    void deserialize_flags(uint8_t flags) {
        move_up = flags & 0x01;
        move_down = flags & 0x02;
        move_left = flags & 0x04;
        move_right = flags & 0x08;
        attacking = flags & 0x10;
    }
    
    // Full serialization with direction (17 bytes: 1 flag + 4 attack_dir_x + 4 attack_dir_y + 4 move_dir_x + 4 move_dir_y)
    void serialize(std::vector<uint8_t>& buffer) const {
        buffer.push_back(serialize_flags());
        const uint8_t* dx = reinterpret_cast<const uint8_t*>(&attack_dir_x);
        const uint8_t* dy = reinterpret_cast<const uint8_t*>(&attack_dir_y);
        const uint8_t* mx = reinterpret_cast<const uint8_t*>(&move_dir_x);
        const uint8_t* my = reinterpret_cast<const uint8_t*>(&move_dir_y);
        buffer.insert(buffer.end(), dx, dx + sizeof(float));
        buffer.insert(buffer.end(), dy, dy + sizeof(float));
        buffer.insert(buffer.end(), mx, mx + sizeof(float));
        buffer.insert(buffer.end(), my, my + sizeof(float));
    }
    
    void deserialize(const uint8_t* data, size_t size) {
        if (size >= 1) {
            deserialize_flags(data[0]);
        }
        if (size >= 9) {
            std::memcpy(&attack_dir_x, data + 1, sizeof(float));
            std::memcpy(&attack_dir_y, data + 5, sizeof(float));
        }
        if (size >= 17) {
            std::memcpy(&move_dir_x, data + 9, sizeof(float));
            std::memcpy(&move_dir_y, data + 13, sizeof(float));
        }
    }
    
    static constexpr size_t serialized_size() { return 17; }
};

struct NetEntityState {
    uint32_t id = 0;
    EntityType type = EntityType::Player;
    PlayerClass player_class = PlayerClass::Warrior;
    NPCType npc_type = NPCType::Monster;
    BuildingType building_type = BuildingType::Tavern;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float health = 100.0f;
    float max_health = 100.0f;
    uint32_t color = 0xFFFFFFFF;
    char name[32] = {0};
    bool is_attacking = false;
    float attack_cooldown = 0.0f;
    float attack_dir_x = 0.0f;  // Attack direction for visual effects
    float attack_dir_y = 1.0f;
    
    static constexpr size_t serialized_size() {
        // id + type + player_class + npc_type + building_type +
        // x,y,vx,vy,health,max_health (6 floats) + color + name + is_attacking +
        // attack_dir_x,attack_dir_y (2 floats) = 8 floats total
        return sizeof(uint32_t) + sizeof(EntityType) + sizeof(PlayerClass) +
               sizeof(NPCType) + sizeof(BuildingType) +
               sizeof(float) * 8 + sizeof(uint32_t) + 32 + sizeof(uint8_t);
    }
    
    void serialize(std::vector<uint8_t>& buffer) const;
    void deserialize(const uint8_t* data);
};

using EntityState = NetEntityState;
using PlayerState = NetEntityState;

struct PacketHeader {
    MessageType type;
    uint16_t payload_size;
    
    static constexpr size_t size() { return sizeof(MessageType) + sizeof(uint16_t); }
    
    void serialize(uint8_t* buffer) const {
        buffer[0] = static_cast<uint8_t>(type);
        std::memcpy(buffer + 1, &payload_size, sizeof(payload_size));
    }
    
    void deserialize(const uint8_t* buffer) {
        type = static_cast<MessageType>(buffer[0]);
        std::memcpy(&payload_size, buffer + 1, sizeof(payload_size));
    }
};

class Packet {
public:
    Packet() = default;
    explicit Packet(MessageType type) : header_{type, 0} {}
    
    void set_type(MessageType type) { header_.type = type; }
    MessageType type() const { return header_.type; }
    
    void write_uint8(uint8_t value);
    void write_uint16(uint16_t value);
    void write_uint32(uint32_t value);
    void write_float(float value);
    void write_string(const std::string& str, size_t max_len = 32);
    void write_entity_state(const NetEntityState& state);
    
    std::vector<uint8_t> build() const;
    const std::vector<uint8_t>& payload() const { return payload_; }
    const PacketHeader& header() const { return header_; }
    
private:
    PacketHeader header_{};
    std::vector<uint8_t> payload_;
};

namespace config {
    constexpr uint16_t DEFAULT_PORT = 7777;
    constexpr float TICK_RATE = 60.0f;
    constexpr float TICK_DURATION = 1.0f / TICK_RATE;
    
    constexpr float WORLD_WIDTH = 8000.0f;
    constexpr float WORLD_HEIGHT = 8000.0f;
    
    constexpr float PLAYER_SIZE = 32.0f;
    constexpr float PLAYER_SPEED = 200.0f;
    
    constexpr float NPC_SIZE = 36.0f;
    constexpr float NPC_SPEED = 100.0f;
    constexpr float NPC_HEALTH = 100.0f;
    constexpr float NPC_DAMAGE = 15.0f;
    constexpr float NPC_ATTACK_RANGE = 50.0f;
    constexpr float NPC_ATTACK_COOLDOWN = 1.2f;
    constexpr float NPC_AGGRO_RANGE = 300.0f;
    constexpr int NPC_COUNT = 10;
    
    constexpr float WARRIOR_HEALTH = 300.0f;
    constexpr float WARRIOR_DAMAGE = 50.0f;
    constexpr float WARRIOR_ATTACK_RANGE = 60.0f;
    constexpr float WARRIOR_ATTACK_COOLDOWN = 0.8f;
    
    constexpr float MAGE_HEALTH = 160.0f;
    constexpr float MAGE_DAMAGE = 80.0f;
    constexpr float MAGE_ATTACK_RANGE = 850.0f;
    constexpr float MAGE_ATTACK_COOLDOWN = 1.5f;
    
    constexpr float PALADIN_HEALTH = 240.0f;
    constexpr float PALADIN_DAMAGE = 40.0f;
    constexpr float PALADIN_ATTACK_RANGE = 120.0f;
    constexpr float PALADIN_ATTACK_COOLDOWN = 1.0f;
    
    constexpr float ARCHER_HEALTH = 180.0f;
    constexpr float ARCHER_DAMAGE = 70.0f;
    constexpr float ARCHER_ATTACK_RANGE = 700.0f;
    constexpr float ARCHER_ATTACK_COOLDOWN = 1.2f;
}

constexpr uint16_t DEFAULT_PORT = config::DEFAULT_PORT;
constexpr float PLAYER_SPEED = config::PLAYER_SPEED;
constexpr float WORLD_WIDTH = config::WORLD_WIDTH;
constexpr float WORLD_HEIGHT = config::WORLD_HEIGHT;
constexpr float PLAYER_SIZE = config::PLAYER_SIZE;
constexpr float NPC_SIZE = config::NPC_SIZE;
constexpr float TICK_RATE = config::TICK_RATE;
constexpr float TICK_DURATION = config::TICK_DURATION;
constexpr float WARRIOR_HEALTH = config::WARRIOR_HEALTH;
constexpr float WARRIOR_DAMAGE = config::WARRIOR_DAMAGE;
constexpr float WARRIOR_ATTACK_RANGE = config::WARRIOR_ATTACK_RANGE;
constexpr float WARRIOR_ATTACK_COOLDOWN = config::WARRIOR_ATTACK_COOLDOWN;
constexpr float MAGE_HEALTH = config::MAGE_HEALTH;
constexpr float MAGE_DAMAGE = config::MAGE_DAMAGE;
constexpr float MAGE_ATTACK_RANGE = config::MAGE_ATTACK_RANGE;
constexpr float MAGE_ATTACK_COOLDOWN = config::MAGE_ATTACK_COOLDOWN;
constexpr float PALADIN_HEALTH = config::PALADIN_HEALTH;
constexpr float PALADIN_DAMAGE = config::PALADIN_DAMAGE;
constexpr float PALADIN_ATTACK_RANGE = config::PALADIN_ATTACK_RANGE;
constexpr float PALADIN_ATTACK_COOLDOWN = config::PALADIN_ATTACK_COOLDOWN;
constexpr float ARCHER_HEALTH = config::ARCHER_HEALTH;
constexpr float ARCHER_DAMAGE = config::ARCHER_DAMAGE;
constexpr float ARCHER_ATTACK_RANGE = config::ARCHER_ATTACK_RANGE;
constexpr float ARCHER_ATTACK_COOLDOWN = config::ARCHER_ATTACK_COOLDOWN;
constexpr float NPC_HEALTH = config::NPC_HEALTH;
constexpr float NPC_DAMAGE = config::NPC_DAMAGE;
constexpr float NPC_SPEED = config::NPC_SPEED;
constexpr float NPC_ATTACK_RANGE = config::NPC_ATTACK_RANGE;
constexpr float NPC_ATTACK_COOLDOWN = config::NPC_ATTACK_COOLDOWN;
constexpr float NPC_AGGRO_RANGE = config::NPC_AGGRO_RANGE;
constexpr int NPC_COUNT = config::NPC_COUNT;

} // namespace mmo

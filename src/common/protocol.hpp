#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mmo {

enum class EntityType : uint8_t {
    Player = 0,
    NPC = 1,          // Hostile NPCs (monsters)
    TownNPC = 2,      // Friendly town NPCs
    Building = 3,     // Static buildings
    Environment = 4,  // Rocks, trees, etc.
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
    
    // Terrain/heightmap messages (for streaming terrain chunks)
    HeightmapChunk = 20,      // Server sends chunk data to client
    HeightmapRequest = 21,    // Client requests a specific chunk (future)

    // Game data messages (server -> client)
    WorldConfig = 29,         // Server sends world dimensions + tick rate
    ClassList = 30,           // Server sends available classes after connection
    ClassSelect = 31,         // Client sends chosen class index
};

// World configuration sent from server to client on connect
struct NetWorldConfig {
    float world_width = 8000.0f;
    float world_height = 8000.0f;
    float tick_rate = 60.0f;

    static constexpr size_t serialized_size() {
        return sizeof(float) * 3;
    }

    void serialize(uint8_t* buffer) const {
        size_t offset = 0;
        std::memcpy(buffer + offset, &world_width, sizeof(float)); offset += sizeof(float);
        std::memcpy(buffer + offset, &world_height, sizeof(float)); offset += sizeof(float);
        std::memcpy(buffer + offset, &tick_rate, sizeof(float));
    }

    void deserialize(const uint8_t* buffer) {
        size_t offset = 0;
        std::memcpy(&world_width, buffer + offset, sizeof(float)); offset += sizeof(float);
        std::memcpy(&world_height, buffer + offset, sizeof(float)); offset += sizeof(float);
        std::memcpy(&tick_rate, buffer + offset, sizeof(float));
    }
};

// Class information sent from server to client for class selection UI
struct ClassInfo {
    char name[32] = {0};             // Display name (e.g. "WARRIOR")
    char short_desc[32] = {0};       // Short description (e.g. "High HP, Melee")
    char desc_line1[64] = {0};       // Full description line 1
    char desc_line2[64] = {0};       // Full description line 2
    char model_name[32] = {0};       // Model for preview
    uint32_t color = 0xFFFFFFFF;     // Class color (ARGB)
    uint32_t select_color = 0xFFFFFFFF; // Background color for select screen
    uint32_t ui_color = 0xFFFFFFFF;  // UI accent color
    bool shows_reticle = false;      // Whether class shows targeting reticle

    static constexpr size_t serialized_size() {
        return 32 + 32 + 64 + 64 + 32 + sizeof(uint32_t) * 3 + sizeof(uint8_t);
    }

    void serialize(std::vector<uint8_t>& buffer) const;
    void deserialize(const uint8_t* data);
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
    uint8_t player_class = 0;
    uint8_t npc_type = 0;
    uint8_t building_type = 0;
    uint8_t environment_type = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;  // Terrain height/elevation (server-authoritative)
    float vx = 0.0f;
    float vy = 0.0f;
    float rotation = 0.0f;  // Rotation in radians (for buildings)
    float health = 100.0f;
    float max_health = 100.0f;
    uint32_t color = 0xFFFFFFFF;
    char name[32] = {0};
    bool is_attacking = false;
    float attack_cooldown = 0.0f;
    float attack_dir_x = 0.0f;  // Attack direction for visual effects
    float attack_dir_y = 1.0f;
    
    // Per-instance scale multiplier (1.0 = default size)
    float scale = 1.0f;

    // Rendering data (server-authoritative, client renders using these directly)
    char model_name[32] = {0};       // Model to render (e.g. "warrior", "building_tower")
    float target_size = 0.0f;        // Visual target size in world units
    char effect_type[16] = {0};      // Attack effect: "melee_swing", "projectile", "orbit", ""
    char effect_model[32] = {0};     // Model for attack effect (e.g. "weapon_sword")
    float effect_duration = 0.0f;    // Attack effect duration in seconds
    float cone_angle = 0.0f;         // Attack cone angle for visualization
    bool shows_reticle = false;      // Whether to show targeting reticle

    static constexpr size_t serialized_size() {
        return sizeof(uint32_t) + sizeof(EntityType) + sizeof(uint8_t) * 4 +
               sizeof(float) * 11 + sizeof(uint32_t) + 32 + sizeof(uint8_t) +
               32 + sizeof(float) + 16 + 32 + sizeof(float) + sizeof(float) + sizeof(uint8_t);
    }
    
    void serialize(std::vector<uint8_t>& buffer) const;
    void deserialize(const uint8_t* data);
};

using EntityState = NetEntityState;
using PlayerState = NetEntityState;

struct PacketHeader {
    MessageType type;
    uint32_t payload_size;

    static constexpr size_t size() { return sizeof(MessageType) + sizeof(uint32_t); }

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
    void write_class_info(const ClassInfo& info);

    std::vector<uint8_t> build() const;
    const std::vector<uint8_t>& payload() const { return payload_; }
    const PacketHeader& header() const { return header_; }
    
private:
    PacketHeader header_{};
    std::vector<uint8_t> payload_;
};

// Default port for CLI usage (not game logic)
constexpr uint16_t DEFAULT_PORT = 7777;

} // namespace mmo

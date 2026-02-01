#pragma once

#include <cstdint>

namespace mmo::protocol {

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

    // Delta compression messages (replaces WorldState for incremental updates)
    EntityEnter = 40,         // Full entity state when entering view
    EntityUpdate = 41,        // Changed fields only
    EntityExit = 42,          // Entity ID leaving view
};

} // namespace mmo::protocol

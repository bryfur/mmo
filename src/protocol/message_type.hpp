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

    // Inventory messages
    InventoryUpdate = 55,     // Server -> client: full inventory sync
    ItemEquip = 57,           // Client -> server: equip item
    ItemUnequip = 58,         // Client -> server: unequip item
    ItemUse = 59,             // Client -> server: use consumable

    // Quest messages
    QuestAccept = 60,         // Client -> server: accept quest
    QuestUpdate = 61,         // Server -> client: quest added/updated
    QuestProgress = 62,       // Server -> client: objective progress
    QuestComplete = 63,       // Server -> client: quest completed

    // Skill messages
    SkillUse = 70,            // Client -> server: use skill
    SkillCooldown = 71,       // Server -> client: skill cooldown update
    SkillUnlock = 72,         // Server -> client: unlocked skills sync

    // Talent messages
    TalentUnlock = 75,        // Client -> server: unlock talent
    TalentSync = 76,          // Server -> client: talent state sync

    // NPC messages
    NPCInteract = 80,         // Client -> server: interact with NPC
    NPCDialogue = 81,         // Server -> client: NPC dialogue data
};

} // namespace mmo::protocol

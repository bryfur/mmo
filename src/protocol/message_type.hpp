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

    // Progression messages
    LevelUp = 50,             // Server notifies client of level up
    XPGain = 51,              // Server notifies client of XP gained
    GoldChange = 52,          // Gold amount changed

    // Inventory messages
    InventoryUpdate = 55,     // Full inventory sync
    LootDrop = 56,            // Item(s) dropped from monster kill
    ItemEquip = 57,           // Client equips an item
    ItemUnequip = 58,         // Client unequips an item

    // Quest messages
    QuestOffer = 60,          // Server offers a quest to client
    QuestAccept = 61,         // Client accepts a quest
    QuestProgress = 62,       // Server updates quest objective progress
    QuestComplete = 63,       // Server notifies quest completion
    QuestTurnIn = 64,         // Client turns in completed quest
    QuestList = 65,           // Server sends available quests for an NPC

    // Skill messages
    SkillUse = 70,            // Client requests skill use
    SkillCooldown = 71,       // Server sends cooldown update
    SkillUnlock = 72,         // Server notifies skill unlock

    // Talent messages
    TalentUnlock = 75,        // Client requests talent unlock
    TalentSync = 76,          // Server sends talent state

    // NPC interaction
    NPCInteract = 80,        // Client interacts with NPC
    NPCDialogue = 81,        // Server sends NPC dialogue

    // World events
    ZoneChange = 85,          // Player entered a new zone
};

} // namespace mmo::protocol

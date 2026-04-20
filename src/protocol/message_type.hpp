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
    InventoryUpdate = 55,     // Server -> client: full inventory sync
    LootDrop = 56,            // Item(s) dropped from monster kill
    ItemEquip = 57,           // Client -> server: equip item
    ItemUnequip = 58,         // Client -> server: unequip item
    ItemUse = 59,             // Client -> server: use consumable

    // Quest messages
    QuestOffer = 60,          // Server offers a quest to client
    QuestAccept = 61,         // Client -> server: accept quest
    QuestProgress = 62,       // Server -> client: objective progress
    QuestComplete = 63,       // Server -> client: quest completed
    QuestTurnIn = 64,         // Client turns in completed quest
    QuestList = 65,           // Server sends available quests for an NPC

    // Skill messages
    SkillUse = 70,            // Client -> server: use skill
    SkillCooldown = 71,       // Server -> client: skill cooldown update
    SkillUnlock = 72,         // Server -> client: unlocked skills sync

    // Talent messages
    TalentUnlock = 75,        // Client -> server: unlock talent
    TalentSync = 76,          // Server -> client: talent state sync
    TalentTree = 77,          // Server -> client: full talent tree for player's class

    // NPC interaction
    NPCInteract = 80,         // Client -> server: interact with NPC
    NPCDialogue = 81,         // Server -> client: NPC dialogue data

    // World events
    ZoneChange = 85,          // Player entered a new zone

    // Keepalive
    Ping = 86,                // Server -> client: heartbeat probe
    Pong = 87,                // Client -> server: heartbeat reply

    // Chat
    ChatSend = 90,            // Client -> server: player sent a chat message
    ChatBroadcast = 91,       // Server -> client: chat message routed from another player

    // Vendor / shop (NPC merchants)
    VendorOpen = 95,          // Server -> client: open a vendor window with stock
    VendorBuy = 96,           // Client -> server: buy N of a slot
    VendorSell = 97,          // Client -> server: sell an inventory slot
    VendorClose = 98,         // Client -> server: close vendor window

    // Party / group
    PartyInvite = 100,        // Client -> server: invite another player by name
    PartyInviteOffer = 101,   // Server -> client: "X invited you to their party"
    PartyInviteRespond = 102, // Client -> server: accept or decline an invite
    PartyLeave = 103,         // Client -> server: leave current party
    PartyKick = 104,          // Client -> server: kick a member (leader only)
    PartyState = 105,         // Server -> client: full state of player's party

    // Crafting
    CraftRecipes = 110,       // Server -> client: list of recipes available to the player
    CraftRequest = 111,       // Client -> server: craft one unit of a recipe
    CraftResult = 112,        // Server -> client: success/fail with reason
};

} // namespace mmo::protocol

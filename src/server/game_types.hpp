#pragma once

#include <cstdint>

namespace mmo::server {

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
    Inn = 6,
    WoodenLog = 7,
    LogTower = 8,
};

// Environment object types (rocks, trees, etc.)
enum class EnvironmentType : uint8_t {
    RockBoulder = 0,
    RockSlate = 1,
    RockSpire = 2,
    RockCluster = 3,
    RockMossy = 4,
    TreeOak = 5,
    TreePine = 6,
    TreeDead = 7,
    TreeWillow = 8,
    TreeBirch = 9,
    TreeMaple = 10,
    TreeAspen = 11,
};

/// Map NPC type index to string for quest/dialogue lookup.
inline const char* npc_type_to_string(int npc_type) {
    switch (npc_type) {
        case 1:
            return "merchant";
        case 2:
            return "guard";
        case 3:
            return "blacksmith";
        case 4:
            return "innkeeper";
        case 5:
            return "villager";
        default:
            return "villager";
    }
}

} // namespace mmo::server

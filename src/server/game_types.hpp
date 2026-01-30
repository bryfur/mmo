#pragma once

#include <cstdint>

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
};

} // namespace mmo

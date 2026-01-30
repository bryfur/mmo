#pragma once
/**
 * Entity Configuration - Single Source of Truth
 * 
 * This file defines target sizes and model mappings for all entity types.
 * Both client and server should use these values to ensure consistent
 * scaling between visual rendering and physics collision.
 */

#include "protocol.hpp"
#include "model_bounds_generated.hpp"

namespace mmo::config {

// ============================================================================
// Target Sizes (Design Decisions - SINGLE SOURCE OF TRUTH)
// These control how big entities appear in the world
// ============================================================================

/// Get the target visual size for a building type
inline float get_building_target_size(BuildingType type) {
    switch (type) {
        case BuildingType::Tower:       return 160.0f;
        case BuildingType::Tavern:      return 140.0f;
        case BuildingType::Blacksmith:  return 120.0f;
        case BuildingType::Shop:        return 100.0f;
        case BuildingType::House:       return 110.0f;
        case BuildingType::Well:        return 60.0f;
        case BuildingType::Inn:         return 150.0f;
        case BuildingType::WoodenLog:   return 60.0f;
        case BuildingType::LogTower:    return 140.0f;
        default:                        return 100.0f;
    }
}

/// Get the target visual scale for an environment type (rocks/trees)
/// Returns scale multiplier, not absolute size
inline float get_environment_target_scale(EnvironmentType type) {
    switch (type) {
        // Rocks - scales range from small decorative to large obstacles
        case EnvironmentType::RockBoulder:  return 25.0f;
        case EnvironmentType::RockSlate:    return 30.0f;
        case EnvironmentType::RockSpire:    return 35.0f;
        case EnvironmentType::RockCluster:  return 28.0f;
        case EnvironmentType::RockMossy:    return 22.0f;
        // Trees - larger scale for proper forest feel
        case EnvironmentType::TreeOak:      return 320.0f;
        case EnvironmentType::TreePine:     return 360.0f;
        case EnvironmentType::TreeDead:     return 280.0f;
        default:                            return 25.0f;
    }
}

/// Get the target visual size for a character type
inline float get_character_target_size(EntityType type) {
    switch (type) {
        case EntityType::Player:  return PLAYER_SIZE;
        case EntityType::NPC:     return NPC_SIZE;
        case EntityType::TownNPC: return PLAYER_SIZE * 0.9f;
        default:                  return PLAYER_SIZE;
    }
}

// ============================================================================
// Model Name Mapping (for bounds lookup)
// ============================================================================

/// Get the model filename (without extension) for a building type
inline const char* get_building_model_name(BuildingType type) {
    switch (type) {
        case BuildingType::Tower:       return "building_tower";
        case BuildingType::Tavern:      return "building_tavern";
        case BuildingType::Blacksmith:  return "building_blacksmith";
        case BuildingType::Shop:        return "building_shop";
        case BuildingType::House:       return "building_house";
        case BuildingType::Well:        return "building_well";
        case BuildingType::Inn:         return "inn";
        case BuildingType::WoodenLog:   return "wooden_log";
        case BuildingType::LogTower:    return "log_tower";
        default:                        return "building_house";
    }
}

/// Get the model filename (without extension) for an environment type
inline const char* get_environment_model_name(EnvironmentType type) {
    switch (type) {
        case EnvironmentType::RockBoulder:  return "rock_boulder";
        case EnvironmentType::RockSlate:    return "rock_slate";
        case EnvironmentType::RockSpire:    return "rock_spire";
        case EnvironmentType::RockCluster:  return "rock_cluster";
        case EnvironmentType::RockMossy:    return "rock_mossy";
        case EnvironmentType::TreeOak:      return "tree_oak";
        case EnvironmentType::TreePine:     return "tree_pine";
        case EnvironmentType::TreeDead:     return "tree_dead";
        default:                            return "rock_boulder";
    }
}

/// Get the model filename for an NPC type
inline const char* get_npc_model_name(NPCType type) {
    switch (type) {
        case NPCType::Monster:    return "npc_enemy";
        case NPCType::Merchant:   return "npc_merchant";
        case NPCType::Guard:      return "npc_guard";
        case NPCType::Blacksmith: return "npc_blacksmith";
        case NPCType::Innkeeper:  return "npc_innkeeper";
        case NPCType::Villager:   return "npc_villager";
        default:                  return "npc_enemy";
    }
}

/// Get the model filename for a player class
inline const char* get_player_model_name(PlayerClass pc) {
    switch (pc) {
        case PlayerClass::Warrior: return "warrior";
        case PlayerClass::Mage:    return "mage";
        case PlayerClass::Paladin: return "paladin";
        case PlayerClass::Archer:  return "archer";
        default:                   return "warrior";
    }
}

// ============================================================================
// Scale Calculation (used by both client and server)
// ============================================================================

// Compensates for models whose visual footprint is smaller than their bounding box
// (e.g. tall narrow models). Without this, models appear undersized.
constexpr float BOUNDS_TO_VISUAL_SCALE = 1.5f;

// Fraction of visual size used for character capsule collision radius
constexpr float CHARACTER_COLLISION_RADIUS_FACTOR = 0.35f;

// Fraction of visual size used for character capsule half-height
constexpr float CHARACTER_COLLISION_HEIGHT_FACTOR = 0.4f;

// Fraction of model extent used for building/rock collision half-extents
constexpr float STRUCTURE_COLLISION_FACTOR = 0.4f;
constexpr float ENVIRONMENT_COLLISION_FACTOR = 0.35f;

/// Calculate the base scale factor to render a model at a target size.
/// @param bounds The model's bounding box from model_bounds_generated.hpp
/// @param target_size The desired visual size in world units
/// @return Scale factor to apply uniformly to the model
inline float calculate_base_scale(const ModelBounds& bounds, float target_size) {
    return (target_size * BOUNDS_TO_VISUAL_SCALE) / bounds.max_dimension();
}

/// Calculate scale factor using model name lookup
/// @param model_name The name of the model (e.g., "building_tower")
/// @param target_size The desired visual size in world units
/// @param fallback_max_dim Fallback max dimension if model not found
/// @return Scale factor to apply uniformly to the model
inline float calculate_scale_for_model(const std::string& model_name, float target_size, 
                                       float fallback_max_dim = 1.0f) {
    const ModelBounds* bounds = get_model_bounds(model_name);
    if (bounds) {
        return calculate_base_scale(*bounds, target_size);
    }
    return (target_size * BOUNDS_TO_VISUAL_SCALE) / fallback_max_dim;
}

// ============================================================================
// Collision Sizing (for physics)
// ============================================================================

/// Get collision radius for character capsule colliders
/// @param target_size The character's visual target size
/// @param instance_scale Per-instance scale multiplier (1.0 = default)
inline float get_collision_radius(float target_size, float instance_scale = 1.0f) {
    return target_size * instance_scale * CHARACTER_COLLISION_RADIUS_FACTOR;
}

/// Get collision half-height for character capsule colliders
/// @param target_size The character's visual target size
/// @param instance_scale Per-instance scale multiplier (1.0 = default)
inline float get_collision_half_height(float target_size, float instance_scale = 1.0f) {
    return target_size * instance_scale * CHARACTER_COLLISION_HEIGHT_FACTOR;
}

/// Calculate box collision half-extents for a building
/// @param type The building type
/// @param instance_scale Per-instance scale multiplier (1.0 = default)
/// @param half_x Output: half extent in X
/// @param half_y Output: half extent in Y (vertical)
/// @param half_z Output: half extent in Z
inline void get_building_collision_size(BuildingType type, 
                                        float& half_x, float& half_y, float& half_z,
                                        float instance_scale = 1.0f) {
    const char* model_name = get_building_model_name(type);
    const ModelBounds* bounds = get_model_bounds(model_name);
    
    if (bounds) {
        float target_size = get_building_target_size(type);
        float scale = calculate_base_scale(*bounds, target_size) * instance_scale;
        
        // Apply scale to model dimensions, divide by 2 for half-extents
        half_x = (bounds->width() * scale) * STRUCTURE_COLLISION_FACTOR;
        half_y = (bounds->height() * scale) * 0.5f;  // Full height for vertical
        half_z = (bounds->depth() * scale) * STRUCTURE_COLLISION_FACTOR;
    } else {
        // Fallback if model bounds not found
        float target_size = get_building_target_size(type) * instance_scale;
        half_x = target_size * STRUCTURE_COLLISION_FACTOR;
        half_y = target_size * 0.5f;
        half_z = target_size * STRUCTURE_COLLISION_FACTOR;
    }
}

/// Check if an environment type is a tree (vs rock)
inline bool is_tree_type(EnvironmentType type) {
    return type == EnvironmentType::TreeOak || 
           type == EnvironmentType::TreePine || 
           type == EnvironmentType::TreeDead;
}

/// Calculate collision size for environment objects (rocks and trees)
/// @param type The environment type
/// @param scale The instance scale (from world generation)
/// @param half_x Output: half extent in X
/// @param half_y Output: half extent in Y (vertical)
/// @param half_z Output: half extent in Z
inline void get_environment_collision_size(EnvironmentType type, float scale,
                                           float& half_x, float& half_y, float& half_z) {
    const char* model_name = get_environment_model_name(type);
    const ModelBounds* bounds = get_model_bounds(model_name);
    
    if (bounds) {
        // Apply the instance scale to model dimensions
        half_x = (bounds->width() * scale) * ENVIRONMENT_COLLISION_FACTOR;
        half_y = (bounds->height() * scale) * 0.5f;
        half_z = (bounds->depth() * scale) * ENVIRONMENT_COLLISION_FACTOR;
    } else {
        // Fallback
        half_x = scale * 0.3f;
        half_y = scale * 0.5f;
        half_z = scale * 0.3f;
    }
}

/// Get collision radius for tree trunk (cylinder-like collision)
inline float get_tree_collision_radius(EnvironmentType type, float scale) {
    // Trees use smaller radius (just trunk area)
    switch (type) {
        case EnvironmentType::TreeOak:   return scale * 0.08f;
        case EnvironmentType::TreePine:  return scale * 0.06f;
        case EnvironmentType::TreeDead:  return scale * 0.05f;
        default:                         return scale * 0.07f;
    }
}

} // namespace mmo::config

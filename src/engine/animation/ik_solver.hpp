#pragma once

#include "animation_types.hpp"
#include <array>
#include <glm/glm.hpp>

namespace mmo::engine::animation {

// Low-level two-bone IK solver. Modifies bone_matrices and world_transforms in place.
void solve_two_bone_ik(
    std::array<glm::mat4, MAX_BONES>& bone_matrices,
    std::array<glm::mat4, MAX_BONES>& world_transforms,
    const Skeleton& skeleton,
    int upper_idx, int lower_idx, int end_idx,
    const glm::vec3& target,
    const glm::vec3& pole_hint);

// High-level foot IK: adjusts pelvis and solves both legs to plant feet at given offsets.
// model_to_world is the entity's full model matrix (for converting bone positions to world space).
// scale is the model's uniform scale factor (for converting world offsets to model space).
// left/right_terrain_offset are the vertical distances from foot to terrain (positive = foot below terrain).
void apply_foot_ik(
    std::array<glm::mat4, MAX_BONES>& bone_matrices,
    std::array<glm::mat4, MAX_BONES>& world_transforms,
    const Skeleton& skeleton,
    const FootIKData& ik,
    const glm::mat4& model_to_world,
    float scale,
    float left_terrain_offset,
    float right_terrain_offset);

// Apply procedural lean to all spine descendants.
// forward_lean: pitch forward in radians (positive = lean forward).
// lateral_lean: roll in radians (positive = lean right).
void apply_body_lean(
    std::array<glm::mat4, MAX_BONES>& bone_matrices,
    std::array<glm::mat4, MAX_BONES>& world_transforms,
    const Skeleton& skeleton,
    int spine_index,
    float forward_lean,
    float lateral_lean);

} // namespace mmo::engine::animation

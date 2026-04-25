#pragma once

#include "animation_types.hpp"
#include <span>
#include <glm/glm.hpp>

namespace mmo::engine::animation {

// Two-bone IK solver. Modifies bone_matrices and world_transforms in place.
// Spans must point to contiguous storage of at least MAX_BONES glm::mat4.
void solve_two_bone_ik(
    std::span<glm::mat4> bone_matrices,
    std::span<glm::mat4> world_transforms,
    const Skeleton& skeleton,
    int upper_idx, int lower_idx, int end_idx,
    const glm::vec3& target,
    const glm::vec3& pole_hint);

// High-level foot IK: adjusts pelvis and solves both legs.
void apply_foot_ik(
    std::span<glm::mat4> bone_matrices,
    std::span<glm::mat4> world_transforms,
    const Skeleton& skeleton,
    const FootIKData& ik,
    const glm::mat4& model_to_world,
    float scale,
    float left_terrain_offset,
    float right_terrain_offset);

// Apply procedural lean to all spine descendants.
void apply_body_lean(
    std::span<glm::mat4> bone_matrices,
    std::span<glm::mat4> world_transforms,
    const Skeleton& skeleton,
    int spine_index,
    float forward_lean,
    float lateral_lean);

} // namespace mmo::engine::animation

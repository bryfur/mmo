#include "ik_solver.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace mmo::engine::animation {

static glm::quat rotation_between(const glm::vec3& from, const glm::vec3& to) {
    float d = glm::dot(from, to);
    if (d > 0.9999f) {
        return {1, 0, 0, 0};
    }
    if (d < -0.9999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), from);
        if (glm::length(axis) < 0.001f) {
            axis = glm::cross(glm::vec3(0, 1, 0), from);
        }
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    glm::vec3 axis = glm::cross(from, to);
    float s = std::sqrt((1.0f + d) * 2.0f);
    // Caller passes nominally unit-length vectors but accumulated drift over
    // many frames can leave the result slightly off-unit; normalize defensively.
    return glm::normalize(glm::quat(s * 0.5f, axis.x / s, axis.y / s, axis.z / s));
}

static bool is_finite_mat(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m[c][r])) {
                return false;
            }
        }
    }
    return true;
}

void solve_two_bone_ik(std::span<glm::mat4> bone_matrices, std::span<glm::mat4> world_transforms,
                       const Skeleton& skeleton, int upper_idx, int lower_idx, int end_idx, const glm::vec3& target,
                       const glm::vec3& pole_hint) {
    // Bail on non-finite inputs: one poisoned matrix from a prior frame would
    // otherwise propagate NaN into every spine descendant on the next pass.
    if (!is_finite_mat(world_transforms[upper_idx]) || !is_finite_mat(world_transforms[lower_idx]) ||
        !is_finite_mat(world_transforms[end_idx]) || !std::isfinite(target.x) || !std::isfinite(target.y) ||
        !std::isfinite(target.z)) {
        return;
    }

    glm::vec3 pos_a = glm::vec3(world_transforms[upper_idx][3]);
    glm::vec3 pos_b = glm::vec3(world_transforms[lower_idx][3]);
    glm::vec3 pos_c = glm::vec3(world_transforms[end_idx][3]);

    float len_ab = glm::length(pos_b - pos_a);
    float len_bc = glm::length(pos_c - pos_b);
    if (len_ab < 0.001f || len_bc < 0.001f) {
        return;
    }

    glm::vec3 t = target;
    float len_at = glm::length(t - pos_a);
    // Target coincident with hip is a degenerate case for normalize(t-pos_a);
    // pull it slightly along the existing chain direction so we have a well-
    // defined reach axis instead of dividing by zero.
    if (len_at < 0.001f) {
        glm::vec3 fallback = pos_c - pos_a;
        float fallback_len = glm::length(fallback);
        if (fallback_len < 0.001f) {
            return;
        }
        t = pos_a + (fallback / fallback_len) * 0.001f;
        len_at = 0.001f;
    }
    float max_reach = len_ab + len_bc - 0.01f;
    float min_reach = std::abs(len_ab - len_bc) + 0.01f;
    if (len_at > max_reach) {
        t = pos_a + glm::normalize(t - pos_a) * max_reach;
        len_at = max_reach;
    }
    if (len_at < min_reach) {
        t = pos_a + glm::normalize(t - pos_a) * min_reach;
        len_at = min_reach;
    }

    float cos_a = (len_ab * len_ab + len_at * len_at - len_bc * len_bc) / (2.0f * len_ab * len_at);
    cos_a = glm::clamp(cos_a, -1.0f, 1.0f);
    float angle_a = std::acos(cos_a);

    glm::vec3 dir_at = glm::normalize(t - pos_a);

    glm::vec3 to_pole = pole_hint - pos_a;
    to_pole = to_pole - dir_at * glm::dot(to_pole, dir_at);
    if (glm::length(to_pole) < 0.001f) {
        to_pole = pos_b - pos_a;
        to_pole = to_pole - dir_at * glm::dot(to_pole, dir_at);
    }
    if (glm::length(to_pole) < 0.001f) {
        return;
    }
    glm::vec3 bend_dir = glm::normalize(to_pole);

    glm::vec3 new_b = pos_a + dir_at * (std::cos(angle_a) * len_ab) + bend_dir * (std::sin(angle_a) * len_ab);
    glm::vec3 new_c = t;

    glm::vec3 old_dir_ab = glm::normalize(pos_b - pos_a);
    glm::vec3 new_dir_ab = glm::normalize(new_b - pos_a);
    glm::quat delta_upper = rotation_between(old_dir_ab, new_dir_ab);
    glm::mat3 rot_upper = glm::mat3_cast(delta_upper);

    glm::mat3 old_rot_a = glm::mat3(world_transforms[upper_idx]);
    world_transforms[upper_idx] = glm::mat4(rot_upper * old_rot_a);
    world_transforms[upper_idx][3] = glm::vec4(pos_a, 1.0f);

    glm::vec3 old_dir_bc = glm::normalize(pos_c - pos_b);
    glm::vec3 new_dir_bc = glm::normalize(new_c - new_b);
    glm::quat delta_lower = rotation_between(old_dir_bc, new_dir_bc);
    glm::mat3 rot_lower = glm::mat3_cast(delta_lower);

    glm::mat3 old_rot_b = glm::mat3(world_transforms[lower_idx]);
    world_transforms[lower_idx] = glm::mat4(rot_lower * old_rot_b);
    world_transforms[lower_idx][3] = glm::vec4(new_b, 1.0f);

    world_transforms[end_idx][3] = glm::vec4(new_c, 1.0f);

    bone_matrices[upper_idx] = world_transforms[upper_idx] * skeleton.joints[upper_idx].inverse_bind_matrix;
    bone_matrices[lower_idx] = world_transforms[lower_idx] * skeleton.joints[lower_idx].inverse_bind_matrix;
    bone_matrices[end_idx] = world_transforms[end_idx] * skeleton.joints[end_idx].inverse_bind_matrix;

    size_t joint_count = std::min({skeleton.joints.size(), bone_matrices.size(), world_transforms.size()});
    for (size_t i = 0; i < joint_count; i++) {
        if (skeleton.joints[i].parent_index == end_idx) {
            glm::vec3 child_offset = glm::vec3(world_transforms[i][3]) - pos_c;
            glm::vec3 new_child_pos = new_c + child_offset;
            world_transforms[i][3] = glm::vec4(new_child_pos, 1.0f);
            bone_matrices[i] = world_transforms[i] * skeleton.joints[i].inverse_bind_matrix;
        }
    }
}

void apply_foot_ik(std::span<glm::mat4> bone_matrices, std::span<glm::mat4> world_transforms, const Skeleton& skeleton,
                   const FootIKData& ik, const glm::mat4& /*model_to_world*/, float scale, float left_terrain_offset,
                   float right_terrain_offset) {
    constexpr float IK_THRESHOLD = 0.1f;
    constexpr float IK_MAX_CORRECTION = 8.0f;

    float left_offset = left_terrain_offset;
    float right_offset = right_terrain_offset;

    if (std::abs(left_offset) <= IK_THRESHOLD && std::abs(right_offset) <= IK_THRESHOLD) {
        return;
    }

    left_offset = glm::clamp(left_offset, -IK_MAX_CORRECTION, IK_MAX_CORRECTION);
    right_offset = glm::clamp(right_offset, -IK_MAX_CORRECTION, IK_MAX_CORRECTION);

    float pelvis_drop = std::min(left_offset, right_offset);
    if (pelvis_drop < 0.0f) {
        float drop_model = pelvis_drop / scale;
        size_t count = std::min({skeleton.joints.size(), bone_matrices.size(), world_transforms.size()});
        for (size_t i = 0; i < count; i++) {
            world_transforms[i][3].y += drop_model;
            const glm::mat4& inv_bind = skeleton.joints[i].inverse_bind_matrix;
            bone_matrices[i][3][0] += drop_model * inv_bind[1][0];
            bone_matrices[i][3][1] += drop_model * inv_bind[1][1];
            bone_matrices[i][3][2] += drop_model * inv_bind[1][2];
            bone_matrices[i][3][3] += drop_model * inv_bind[1][3];
        }
        left_offset -= pelvis_drop;
        right_offset -= pelvis_drop;
    }

    auto solve_leg = [&](int upper, int lower, int foot, float offset) {
        if (std::abs(offset) < IK_THRESHOLD) {
            return;
        }
        glm::vec3 foot_pos = glm::vec3(world_transforms[foot][3]);
        glm::vec3 target = foot_pos;
        target.y += offset / scale;

        glm::vec3 knee_pos = glm::vec3(world_transforms[lower][3]);
        glm::vec3 hip_pos = glm::vec3(world_transforms[upper][3]);
        glm::vec3 mid = (hip_pos + foot_pos) * 0.5f;
        glm::vec3 pole = knee_pos + glm::normalize(knee_pos - mid) * 0.5f;

        solve_two_bone_ik(bone_matrices, world_transforms, skeleton, upper, lower, foot, target, pole);
    };

    solve_leg(ik.left_upper, ik.left_lower, ik.left_foot, left_offset);
    solve_leg(ik.right_upper, ik.right_lower, ik.right_foot, right_offset);
}

void apply_body_lean(std::span<glm::mat4> bone_matrices, std::span<glm::mat4> world_transforms,
                     const Skeleton& skeleton, int spine_index, float forward_lean, float lateral_lean) {
    if (std::abs(forward_lean) < 0.001f && std::abs(lateral_lean) < 0.001f) {
        return;
    }

    glm::quat lean_q =
        glm::angleAxis(forward_lean, glm::vec3(1, 0, 0)) * glm::angleAxis(lateral_lean, glm::vec3(0, 0, 1));
    glm::mat4 lean_rot = glm::mat4_cast(lean_q);

    glm::vec3 pivot = glm::vec3(world_transforms[spine_index][3]);
    glm::mat4 pivot_xform = glm::translate(glm::mat4(1.0f), pivot) * lean_rot * glm::translate(glm::mat4(1.0f), -pivot);

    size_t joint_count = std::min({skeleton.joints.size(), bone_matrices.size(), world_transforms.size()});
    std::vector<uint8_t> is_spine_descendant(joint_count, 0);
    is_spine_descendant[spine_index] = 1;
    for (size_t i = 0; i < joint_count; i++) {
        int parent = skeleton.joints[i].parent_index;
        if (parent >= 0 && parent < static_cast<int>(joint_count) && is_spine_descendant[parent]) {
            is_spine_descendant[i] = 1;
        }
    }

    for (size_t i = 0; i < joint_count; i++) {
        if (!is_spine_descendant[i]) {
            continue;
        }

        world_transforms[i] = pivot_xform * world_transforms[i];
        bone_matrices[i] = world_transforms[i] * skeleton.joints[i].inverse_bind_matrix;
    }
}

} // namespace mmo::engine::animation

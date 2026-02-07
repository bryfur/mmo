#include "ik_solver.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace mmo::engine::animation {

// Helper: rotation quaternion that takes direction 'from' to direction 'to'
static glm::quat rotation_between(const glm::vec3& from, const glm::vec3& to) {
    float d = glm::dot(from, to);
    if (d > 0.9999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.9999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), from);
        if (glm::length(axis) < 0.001f) axis = glm::cross(glm::vec3(0, 1, 0), from);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    glm::vec3 axis = glm::cross(from, to);
    float s = std::sqrt((1.0f + d) * 2.0f);
    return glm::quat(s * 0.5f, axis.x / s, axis.y / s, axis.z / s);
}

void solve_two_bone_ik(
    std::array<glm::mat4, MAX_BONES>& bone_matrices,
    std::array<glm::mat4, MAX_BONES>& world_transforms,
    const Skeleton& skeleton,
    int upper_idx, int lower_idx, int end_idx,
    const glm::vec3& target,
    const glm::vec3& pole_hint)
{
    // Extract current joint positions from world transforms
    glm::vec3 pos_a = glm::vec3(world_transforms[upper_idx][3]);
    glm::vec3 pos_b = glm::vec3(world_transforms[lower_idx][3]);
    glm::vec3 pos_c = glm::vec3(world_transforms[end_idx][3]);

    float len_ab = glm::length(pos_b - pos_a);
    float len_bc = glm::length(pos_c - pos_b);
    if (len_ab < 0.001f || len_bc < 0.001f) return;

    // Clamp target to reachable range
    glm::vec3 t = target;
    float len_at = glm::length(t - pos_a);
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

    // Law of cosines: angle at upper joint
    float cos_a = (len_ab * len_ab + len_at * len_at - len_bc * len_bc) / (2.0f * len_ab * len_at);
    cos_a = glm::clamp(cos_a, -1.0f, 1.0f);
    float angle_a = std::acos(cos_a);

    // Direction from upper joint toward target
    glm::vec3 dir_at = glm::normalize(t - pos_a);

    // Determine bend direction from pole hint (project out the chain axis)
    glm::vec3 to_pole = pole_hint - pos_a;
    to_pole = to_pole - dir_at * glm::dot(to_pole, dir_at);
    if (glm::length(to_pole) < 0.001f) {
        // Fallback: use current knee direction
        to_pole = pos_b - pos_a;
        to_pole = to_pole - dir_at * glm::dot(to_pole, dir_at);
    }
    if (glm::length(to_pole) < 0.001f) return; // degenerate, skip
    glm::vec3 bend_dir = glm::normalize(to_pole);

    // New knee position
    glm::vec3 new_b = pos_a + dir_at * (std::cos(angle_a) * len_ab)
                             + bend_dir * (std::sin(angle_a) * len_ab);
    glm::vec3 new_c = t;

    // Rotate upper bone: old A->B direction to new A->new_B direction
    glm::vec3 old_dir_ab = glm::normalize(pos_b - pos_a);
    glm::vec3 new_dir_ab = glm::normalize(new_b - pos_a);
    glm::quat delta_upper = rotation_between(old_dir_ab, new_dir_ab);
    glm::mat3 rot_upper = glm::mat3_cast(delta_upper);

    glm::mat3 old_rot_a = glm::mat3(world_transforms[upper_idx]);
    world_transforms[upper_idx] = glm::mat4(rot_upper * old_rot_a);
    world_transforms[upper_idx][3] = glm::vec4(pos_a, 1.0f);

    // Rotate lower bone: old B->C direction to new_B->new_C direction
    glm::vec3 old_dir_bc = glm::normalize(pos_c - pos_b);
    glm::vec3 new_dir_bc = glm::normalize(new_c - new_b);
    glm::quat delta_lower = rotation_between(old_dir_bc, new_dir_bc);
    glm::mat3 rot_lower = glm::mat3_cast(delta_lower);

    glm::mat3 old_rot_b = glm::mat3(world_transforms[lower_idx]);
    world_transforms[lower_idx] = glm::mat4(rot_lower * old_rot_b);
    world_transforms[lower_idx][3] = glm::vec4(new_b, 1.0f);

    // Move end effector to target
    world_transforms[end_idx][3] = glm::vec4(new_c, 1.0f);

    // Recompute bone matrices for modified bones
    bone_matrices[upper_idx] = world_transforms[upper_idx] * skeleton.joints[upper_idx].inverse_bind_matrix;
    bone_matrices[lower_idx] = world_transforms[lower_idx] * skeleton.joints[lower_idx].inverse_bind_matrix;
    bone_matrices[end_idx] = world_transforms[end_idx] * skeleton.joints[end_idx].inverse_bind_matrix;

    // Propagate to children of the end effector (toes)
    size_t joint_count = std::min(skeleton.joints.size(), static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < joint_count; i++) {
        if (skeleton.joints[i].parent_index == end_idx) {
            glm::vec3 child_offset = glm::vec3(world_transforms[i][3]) - pos_c;
            glm::vec3 new_child_pos = new_c + child_offset;
            world_transforms[i][3] = glm::vec4(new_child_pos, 1.0f);
            bone_matrices[i] = world_transforms[i] * skeleton.joints[i].inverse_bind_matrix;
        }
    }
}

void apply_foot_ik(
    std::array<glm::mat4, MAX_BONES>& bone_matrices,
    std::array<glm::mat4, MAX_BONES>& world_transforms,
    const Skeleton& skeleton,
    const FootIKData& ik,
    const glm::mat4& model_to_world,
    float scale,
    float left_terrain_offset,
    float right_terrain_offset)
{
    constexpr float IK_THRESHOLD = 0.1f;
    constexpr float IK_MAX_CORRECTION = 8.0f;

    float left_offset = left_terrain_offset;
    float right_offset = right_terrain_offset;

    if (std::abs(left_offset) <= IK_THRESHOLD && std::abs(right_offset) <= IK_THRESHOLD) {
        return;
    }

    left_offset = glm::clamp(left_offset, -IK_MAX_CORRECTION, IK_MAX_CORRECTION);
    right_offset = glm::clamp(right_offset, -IK_MAX_CORRECTION, IK_MAX_CORRECTION);

    // Drop pelvis by the larger downward correction.
    // Hips is the skeleton root, so ALL bones must translate together.
    float pelvis_drop = std::min(left_offset, right_offset);
    if (pelvis_drop < 0.0f) {
        float drop_model = pelvis_drop / scale;
        size_t count = std::min(skeleton.joints.size(), static_cast<size_t>(MAX_BONES));
        for (size_t i = 0; i < count; i++) {
            world_transforms[i][3].y += drop_model;
            bone_matrices[i] = world_transforms[i] * skeleton.joints[i].inverse_bind_matrix;
        }
        left_offset -= pelvis_drop;
        right_offset -= pelvis_drop;
    }

    // Solve each leg
    auto solve_leg = [&](int upper, int lower, int foot, float offset) {
        if (std::abs(offset) < IK_THRESHOLD) return;
        glm::vec3 foot_pos = glm::vec3(world_transforms[foot][3]);
        glm::vec3 target = foot_pos;
        target.y += offset / scale;

        glm::vec3 knee_pos = glm::vec3(world_transforms[lower][3]);
        glm::vec3 hip_pos = glm::vec3(world_transforms[upper][3]);
        glm::vec3 mid = (hip_pos + foot_pos) * 0.5f;
        glm::vec3 pole = knee_pos + glm::normalize(knee_pos - mid) * 0.5f;

        solve_two_bone_ik(bone_matrices, world_transforms, skeleton,
                          upper, lower, foot, target, pole);
    };

    solve_leg(ik.left_upper, ik.left_lower, ik.left_foot, left_offset);
    solve_leg(ik.right_upper, ik.right_lower, ik.right_foot, right_offset);
}

void apply_body_lean(
    std::array<glm::mat4, MAX_BONES>& bone_matrices,
    std::array<glm::mat4, MAX_BONES>& world_transforms,
    const Skeleton& skeleton,
    int spine_index,
    float forward_lean,
    float lateral_lean)
{
    if (std::abs(forward_lean) < 0.001f && std::abs(lateral_lean) < 0.001f) return;

    glm::quat lean_q = glm::angleAxis(forward_lean, glm::vec3(1, 0, 0))
                      * glm::angleAxis(lateral_lean, glm::vec3(0, 0, 1));
    glm::mat4 lean_rot = glm::mat4_cast(lean_q);

    glm::vec3 pivot = glm::vec3(world_transforms[spine_index][3]);
    glm::mat4 pivot_xform = glm::translate(glm::mat4(1.0f), pivot)
                           * lean_rot
                           * glm::translate(glm::mat4(1.0f), -pivot);

    for (size_t i = 0; i < skeleton.joints.size() && i < static_cast<size_t>(MAX_BONES); i++) {
        // Walk parent chain to check if descendant of spine
        int cur = static_cast<int>(i);
        bool is_descendant = false;
        while (cur >= 0) {
            if (cur == spine_index) { is_descendant = true; break; }
            cur = skeleton.joints[cur].parent_index;
        }
        if (!is_descendant) continue;

        world_transforms[i] = pivot_xform * world_transforms[i];
        bone_matrices[i] = world_transforms[i] * skeleton.joints[i].inverse_bind_matrix;
    }
}

} // namespace mmo::engine::animation

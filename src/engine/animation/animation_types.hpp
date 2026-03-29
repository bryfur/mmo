#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace mmo::engine::animation {

constexpr int MAX_BONES = 64;
constexpr int MAX_BONE_INFLUENCES = 4;

struct AnimationKeyframe {
    float time;
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;
};

struct AnimationChannel {
    int bone_index;
    std::vector<float> position_times;
    std::vector<glm::vec3> positions;
    std::vector<float> rotation_times;
    std::vector<glm::quat> rotations;
    std::vector<float> scale_times;
    std::vector<glm::vec3> scales;

    // Per-channel keyframe cursor caches (mutable so const channels can be sampled)
    mutable size_t pos_cursor = 0;
    mutable size_t rot_cursor = 0;
    mutable size_t scale_cursor = 0;
};

struct AnimationClip {
    std::string name;
    float duration;
    std::vector<AnimationChannel> channels;
};

struct Joint {
    std::string name;
    int parent_index;  // -1 for root
    glm::mat4 inverse_bind_matrix;
    glm::vec3 local_translation;
    glm::quat local_rotation;
    glm::vec3 local_scale;
};

struct Skeleton {
    std::vector<Joint> joints;
    std::vector<int> joint_node_indices;  // Map from joint index to glTF node index
};

// Per-entity smoothed foot IK offsets to eliminate jitter from terrain sampling.
struct FootIKSmoother {
    float smoothed_left_offset = 0.0f;
    float smoothed_right_offset = 0.0f;
    float smooth_rate = 15.0f;  // higher = more responsive, lower = smoother

    // Smooth raw terrain offsets toward targets. Call once per frame.
    void update(float left_target, float right_target, float dt) {
        float blend = std::min(1.0f, dt * smooth_rate);
        smoothed_left_offset += (left_target - smoothed_left_offset) * blend;
        smoothed_right_offset += (right_target - smoothed_right_offset) * blend;
    }
};

struct FootIKData {
    int hips = -1;
    int spine = -1;
    int left_upper = -1, left_lower = -1, left_foot = -1;
    int right_upper = -1, right_lower = -1, right_foot = -1;
    bool valid = false;

    void init(const Skeleton& skel) {
        for (size_t i = 0; i < skel.joints.size(); i++) {
            const auto& name = skel.joints[i].name;
            if (name == "Hips") hips = static_cast<int>(i);
            else if (name == "Spine") spine = static_cast<int>(i);
            else if (name == "LeftUpperLeg") left_upper = static_cast<int>(i);
            else if (name == "LeftLowerLeg") left_lower = static_cast<int>(i);
            else if (name == "LeftFoot") left_foot = static_cast<int>(i);
            else if (name == "RightUpperLeg") right_upper = static_cast<int>(i);
            else if (name == "RightLowerLeg") right_lower = static_cast<int>(i);
            else if (name == "RightFoot") right_foot = static_cast<int>(i);
        }
        valid = (hips >= 0 && spine >= 0 &&
                 left_upper >= 0 && left_lower >= 0 && left_foot >= 0 &&
                 right_upper >= 0 && right_lower >= 0 && right_foot >= 0);
    }
};

// Exponential angle smoother with turn-rate tracking (for body lean).
struct RotationSmoother {
    float current = 0.0f;
    float turn_rate = 0.0f;
    bool initialized = false;

    void smooth_toward(float target, float dt, float speed = 12.0f) {
        if (!initialized) {
            current = target;
            initialized = true;
            return;
        }
        float blend = 1.0f - std::exp(-speed * dt);
        float diff = std::fmod(target - current + 3.14159265f, 6.28318530f) - 3.14159265f;
        float step = diff * blend;
        current += step;
        turn_rate = (dt > 0.0001f) ? (step / dt) : 0.0f;
    }

    void decay_turn_rate(float factor = 0.9f) {
        turn_rate *= factor;
    }
};

// Per-archetype procedural animation tuning (lean, tilt, etc.)
struct ProceduralConfig {
    bool foot_ik = true;
    bool lean = true;
    float forward_lean_factor = 0.015f;
    float forward_lean_max = 0.18f;
    float lateral_lean_factor = 0.06f;
    float lateral_lean_max = 0.15f;
    float attack_tilt_max = 0.4f;
    float attack_tilt_cooldown = 0.5f;
};

// Helper to interpolate between keyframes (linear for vec3, slerp for quat)
// last_cursor is a per-channel cache that avoids binary search when playback is sequential.
template<typename T>
T interpolate_keyframes(const std::vector<float>& times, const std::vector<T>& values,
                        float t, size_t& last_cursor) {
    if (times.empty() || values.empty()) return T{};
    if (times.size() == 1 || t <= times.front()) { last_cursor = 0; return values.front(); }
    if (t >= times.back()) { last_cursor = times.size() - 2; return values.back(); }

    size_t i = 0;
    // Fast path: check if cached cursor still brackets the current time (O(1))
    if (last_cursor < times.size() - 1 &&
        times[last_cursor] <= t && t < times[last_cursor + 1]) {
        i = last_cursor;
    } else {
        // Fallback: binary search and update cursor
        auto it = std::upper_bound(times.begin(), times.end(), t);
        i = static_cast<size_t>(std::distance(times.begin(), it)) - 1;
        last_cursor = i;
    }

    float t0 = times[i], t1 = times[i + 1];
    float f = (t - t0) / (t1 - t0);

    if constexpr (std::is_same_v<T, glm::quat>) {
        return glm::slerp(values[i], values[i + 1], f);
    } else {
        return glm::mix(values[i], values[i + 1], f);
    }
}

// Convenience overload without cursor (for callers that don't need caching)
template<typename T>
T interpolate_keyframes(const std::vector<float>& times, const std::vector<T>& values, float t) {
    size_t cursor = 0;
    return interpolate_keyframes(times, values, t, cursor);
}

} // namespace mmo::engine::animation

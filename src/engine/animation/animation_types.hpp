#pragma once

#include <string>
#include <vector>
#include <cmath>
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
template<typename T>
T interpolate_keyframes(const std::vector<float>& times, const std::vector<T>& values, float t) {
    if (times.empty() || values.empty()) return T();
    if (times.size() == 1) return values[0];

    if (t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();

    for (size_t i = 0; i < times.size() - 1; i++) {
        if (t >= times[i] && t <= times[i + 1]) {
            float factor = (t - times[i]) / (times[i + 1] - times[i]);
            if constexpr (std::is_same_v<T, glm::quat>) {
                return glm::slerp(values[i], values[i + 1], factor);
            } else {
                return glm::mix(values[i], values[i + 1], factor);
            }
        }
    }
    return values.back();
}

} // namespace mmo::engine::animation

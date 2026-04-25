#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <type_traits>
#include <vector>

namespace mmo::engine::animation {

constexpr int MAX_BONES = 128;
constexpr int MAX_BONE_INFLUENCES = 4;

struct AnimationKeyframe {
    float time;
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;
};

struct AnimationChannel {
    int bone_index{};
    std::vector<float> position_times;
    std::vector<glm::vec3> positions;
    std::vector<float> rotation_times;
    std::vector<glm::quat> rotations;
    std::vector<float> scale_times;
    std::vector<glm::vec3> scales;
};

// Per-channel playback cursor. Lives on the player side so multiple instances
// can share clip data without racing on mutable state.
struct ChannelCursors {
    size_t pos = 0;
    size_t rot = 0;
    size_t scale = 0;
};

struct AnimationClip {
    std::string name;
    float duration{};
    std::vector<AnimationChannel> channels;
};

struct Joint {
    std::string name;
    int parent_index{};
    glm::mat4 inverse_bind_matrix{};
    glm::vec3 local_translation{};
    glm::quat local_rotation{};
    glm::vec3 local_scale{};
};

struct Skeleton {
    std::vector<Joint> joints;
    std::vector<int> joint_node_indices;
};

struct FootIKSmoother {
    float smoothed_left_offset = 0.0f;
    float smoothed_right_offset = 0.0f;
    float smooth_rate = 15.0f;

    void update(float left_target, float right_target, float dt) {
        float blend = std::min(1.0f, dt * smooth_rate);
        smoothed_left_offset += (left_target - smoothed_left_offset) * blend;
        smoothed_right_offset += (right_target - smoothed_right_offset) * blend;
    }
};

// Configurable bone names for foot IK. Defaults match Mixamo conventions;
// rigs from other DCCs can override.
struct FootIKBoneNames {
    std::string hips = "Hips";
    std::string spine = "Spine";
    std::string left_hip = "LeftUpperLeg";
    std::string left_knee = "LeftLowerLeg";
    std::string left_foot = "LeftFoot";
    std::string right_hip = "RightUpperLeg";
    std::string right_knee = "RightLowerLeg";
    std::string right_foot = "RightFoot";
};

struct FootIKData {
    int hips = -1;
    int spine = -1;
    int left_upper = -1, left_lower = -1, left_foot = -1;
    int right_upper = -1, right_lower = -1, right_foot = -1;
    bool valid = false;

    void init(const Skeleton& skel, const FootIKBoneNames& names = {}) {
        for (size_t i = 0; i < skel.joints.size(); i++) {
            const auto& name = skel.joints[i].name;
            int idx = static_cast<int>(i);
            if (name == names.hips) {
                {
                    hips = idx;
                }
            } else if (name == names.spine) {
                { spine = idx; }
            } else if (name == names.left_hip) {
                { left_upper = idx; }
            } else if (name == names.left_knee) {
                { left_lower = idx; }
            } else if (name == names.left_foot) {
                { left_foot = idx; }
            } else if (name == names.right_hip) {
                { right_upper = idx; }
            } else if (name == names.right_knee) {
                { right_lower = idx; }
            } else if (name == names.right_foot) {
                { right_foot = idx; }
            }
        }
        valid = (hips >= 0 && spine >= 0 && left_upper >= 0 && left_lower >= 0 && left_foot >= 0 && right_upper >= 0 &&
                 right_lower >= 0 && right_foot >= 0);
    }
};

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
        float diff = std::fmod(target - current + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();
        float step = diff * blend;
        current += step;
        turn_rate = (dt > 0.0001f) ? (step / dt) : 0.0f;
    }

    void decay_turn_rate(float factor = 0.9f) { turn_rate *= factor; }
};

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

// Parameter value for state-machine transition conditions. A tagged union
// avoids the per-frame std::variant overhead on the transition hot path.
enum class ParamType : uint8_t { Float, Bool };

struct ParamValue {
    ParamType type = ParamType::Float;
    union {
        float f;
        bool b;
    };

    ParamValue() : type(ParamType::Float), f(0.0f) {}
    explicit ParamValue(float v) : type(ParamType::Float), f(v) {}
    explicit ParamValue(bool v) : type(ParamType::Bool), b(v) {}

    float as_float() const { return type == ParamType::Float ? f : 0.0f; }
    bool as_bool() const { return type == ParamType::Bool ? b : false; }
};

template<typename T>
T interpolate_keyframes(const std::vector<float>& times, const std::vector<T>& values, float t, size_t& last_cursor) {
    if (times.empty() || values.empty()) {
        {
            return T{};
        }
    }
    if (times.size() == 1 || t <= times.front()) {
        last_cursor = 0;
        return values.front();
    }
    if (t >= times.back()) {
        last_cursor = times.size() - 2;
        return values.back();
    }

    size_t i = 0;
    if (last_cursor < times.size() - 1 && times[last_cursor] <= t && t < times[last_cursor + 1]) {
        i = last_cursor;
    } else {
        auto it = std::upper_bound(times.begin(), times.end(), t);
        i = static_cast<size_t>(std::distance(times.begin(), it)) - 1;
        last_cursor = i;
    }

    float t0 = times[i];
    float t1 = times[i + 1];
    float f = (t - t0) / (t1 - t0);

    if constexpr (std::is_same_v<T, glm::quat>) {
        return glm::slerp(values[i], values[i + 1], f);
    } else {
        return glm::mix(values[i], values[i + 1], f);
    }
}

template<typename T> T interpolate_keyframes(const std::vector<float>& times, const std::vector<T>& values, float t) {
    size_t cursor = 0;
    return interpolate_keyframes(times, values, t, cursor);
}

} // namespace mmo::engine::animation

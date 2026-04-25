#pragma once

#include "animation_types.hpp"
#include <array>
#include <span>
#include <vector>

namespace mmo::engine::animation {

class AnimationPlayer {
public:
    AnimationPlayer() { reset(); }

    AnimationPlayer(AnimationPlayer&&) noexcept = default;
    AnimationPlayer& operator=(AnimationPlayer&&) noexcept = default;

    void update(const Skeleton& skeleton,
                const std::vector<AnimationClip>& clips,
                float dt);

    void crossfade_to(int clip_index, float duration = 0.2f);

    void reset();

    void set_speed(float v) { speed_ = v; }
    float speed() const { return speed_; }

    void set_playing(bool v) { playing_ = v; }
    bool is_playing() const { return playing_; }

    void set_loop(bool v) { loop_ = v; }
    bool loops() const { return loop_; }

    void seek(float t) { time_ = t; }
    float current_time() const { return time_; }

    int current_clip() const { return current_clip_; }
    int prev_clip() const { return prev_clip_; }
    float prev_time() const { return prev_time_; }
    float blend_factor() const { return blend_factor_; }
    float blend_duration() const { return blend_duration_; }

    std::span<const glm::mat4> bone_matrices() const {
        return std::span<const glm::mat4>(bone_matrices_.data(), bone_matrices_.size());
    }

    // Direct array reference for APIs that require the fixed-size std::array type
    // (e.g. the render-scene command stores a pointer to this exact storage).
    const std::array<glm::mat4, MAX_BONES>& bone_matrix_array() const { return bone_matrices_; }

    // IK/lean integrators need to read (and occasionally patch) world transforms
    // together with the bone matrices. Exposed as spans so callers never own the
    // underlying storage.
    std::span<glm::mat4> mutable_bone_matrices() {
        return std::span<glm::mat4>(bone_matrices_.data(), bone_matrices_.size());
    }
    std::span<glm::mat4> mutable_world_transforms() {
        return std::span<glm::mat4>(world_transforms_.data(), world_transforms_.size());
    }
    std::span<const glm::mat4> world_transforms() const {
        return std::span<const glm::mat4>(world_transforms_.data(), world_transforms_.size());
    }

private:
    void compute_bone_matrices(const Skeleton& skeleton,
                               const std::vector<AnimationClip>& clips);

    int current_clip_ = 0;
    float time_ = 0.0f;
    bool playing_ = true;
    bool loop_ = true;
    float speed_ = 1.0f;

    int prev_clip_ = -1;
    float prev_time_ = 0.0f;
    float blend_factor_ = 1.0f;
    float blend_duration_ = 0.2f;

    std::array<glm::mat4, MAX_BONES> bone_matrices_;
    std::array<glm::mat4, MAX_BONES> world_transforms_;

    std::vector<const AnimationChannel*> channel_lookup_;
    std::vector<const AnimationChannel*> prev_channel_lookup_;
    std::vector<ChannelCursors> current_cursors_;
    std::vector<ChannelCursors> prev_cursors_;
    std::vector<glm::mat4> local_transforms_;
    int cached_clip_index_ = -1;
    int cached_prev_clip_index_ = -1;
};

} // namespace mmo::engine::animation

#pragma once

#include "animation_types.hpp"
#include <array>
#include <vector>

namespace mmo::engine::animation {

class AnimationPlayer {
public:
    AnimationPlayer() { reset(); }

    AnimationPlayer(AnimationPlayer&&) noexcept = default;
    AnimationPlayer& operator=(AnimationPlayer&&) noexcept = default;

    // Advance time, manage crossfade, compute bone matrices
    void update(const Skeleton& skeleton,
                const std::vector<AnimationClip>& clips,
                float dt);

    // Initiate crossfade from current clip to new clip
    void crossfade_to(int clip_index, float duration = 0.2f);

    // Reset to identity pose
    void reset();

    // Playback control
    int current_clip = 0;
    float time = 0.0f;
    bool playing = true;
    bool loop = true;
    float speed = 1.0f;

    // Crossfade state
    int prev_clip = -1;
    float prev_time = 0.0f;
    float blend_factor = 1.0f;
    float blend_duration = 0.2f;

    // Output: bone matrices ready for GPU upload
    std::array<glm::mat4, MAX_BONES> bone_matrices;

    // Output: world-space transforms (needed for IK, lean)
    std::array<glm::mat4, MAX_BONES> world_transforms;

private:
    void compute_bone_matrices(const Skeleton& skeleton,
                               const std::vector<AnimationClip>& clips);

    // Reusable buffers to avoid per-frame allocations
    std::vector<const AnimationChannel*> channel_lookup_;  // indexed by bone
    std::vector<const AnimationChannel*> prev_channel_lookup_;
    std::vector<glm::mat4> local_transforms_;
    int cached_clip_index_ = -1;
    int cached_prev_clip_index_ = -1;
};

} // namespace mmo::engine::animation

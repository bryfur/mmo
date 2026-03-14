#include "animation_player.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace mmo::engine::animation {

void AnimationPlayer::reset() {
    time = 0.0f;
    prev_clip = -1;
    blend_factor = 1.0f;
    cached_clip_index_ = -1;
    cached_prev_clip_index_ = -1;
    for (auto& m : bone_matrices) m = glm::mat4(1.0f);
    for (auto& m : world_transforms) m = glm::mat4(1.0f);
}

void AnimationPlayer::crossfade_to(int clip_index, float duration) {
    if (clip_index == current_clip) return;
    prev_clip = current_clip;
    prev_time = time;
    blend_factor = 0.0f;
    blend_duration = duration;
    current_clip = clip_index;
    time = 0.0f;
    cached_clip_index_ = -1;
    cached_prev_clip_index_ = -1;
}

void AnimationPlayer::update(const Skeleton& skeleton,
                             const std::vector<AnimationClip>& clips,
                             float dt) {
    if (clips.empty() || !playing) return;

    // Clamp clip index
    if (current_clip < 0 || current_clip >= static_cast<int>(clips.size())) {
        current_clip = 0;
    }

    const auto& clip = clips[current_clip];

    // Update current clip time
    time += dt * speed;
    if (time > clip.duration) {
        if (loop) {
            time = std::fmod(time, clip.duration);
        } else {
            time = clip.duration;
            playing = false;
        }
    }

    // Update crossfade blend
    if (blend_factor < 1.0f) {
        blend_factor += dt / blend_duration;
        if (blend_factor >= 1.0f) {
            blend_factor = 1.0f;
            prev_clip = -1;
        }

        // Advance previous clip time
        if (prev_clip >= 0 && prev_clip < static_cast<int>(clips.size())) {
            const auto& prev = clips[prev_clip];
            prev_time += dt * speed;
            if (prev_time > prev.duration) {
                prev_time = std::fmod(prev_time, prev.duration);
            }
        }
    }

    compute_bone_matrices(skeleton, clips);
}

// Helper: sample a clip's channels for a single joint at a given time
// Uses a flat vector indexed by bone_index instead of unordered_map
static void sample_clip_joint(const AnimationClip* clip,
                              const std::vector<const AnimationChannel*>& channels,
                              int joint_idx, float time,
                              const Joint& joint,
                              glm::vec3& out_translation,
                              glm::quat& out_rotation,
                              glm::vec3& out_scale) {
    out_translation = joint.local_translation;
    out_rotation = joint.local_rotation;
    out_scale = joint.local_scale;

    if (!clip) return;
    if (joint_idx < 0 || joint_idx >= static_cast<int>(channels.size())) return;
    const auto* ch = channels[joint_idx];
    if (!ch) return;

    if (!ch->position_times.empty())
        out_translation = interpolate_keyframes(ch->position_times, ch->positions, time);
    if (!ch->rotation_times.empty())
        out_rotation = interpolate_keyframes(ch->rotation_times, ch->rotations, time);
    if (!ch->scale_times.empty())
        out_scale = interpolate_keyframes(ch->scale_times, ch->scales, time);
}

void AnimationPlayer::compute_bone_matrices(const Skeleton& skeleton,
                                            const std::vector<AnimationClip>& clips) {
    size_t num_joints = skeleton.joints.size();
    if (num_joints == 0) return;

    // Build flat channel lookup (indexed by bone_index) - cache across frames
    const AnimationClip* clip = nullptr;
    if (current_clip >= 0 && current_clip < static_cast<int>(clips.size())) {
        clip = &clips[current_clip];
        if (current_clip != cached_clip_index_) {
            channel_lookup_.assign(num_joints, nullptr);
            for (const auto& ch : clip->channels) {
                if (ch.bone_index >= 0 && ch.bone_index < static_cast<int>(num_joints))
                    channel_lookup_[ch.bone_index] = &ch;
            }
            cached_clip_index_ = current_clip;
        }
    } else {
        channel_lookup_.assign(num_joints, nullptr);
        cached_clip_index_ = -1;
    }

    // Previous clip (for crossfade) - cache across frames
    const AnimationClip* prev_clip_ptr = nullptr;
    bool blending = (blend_factor < 1.0f && prev_clip >= 0 &&
                     prev_clip < static_cast<int>(clips.size()));
    if (blending) {
        prev_clip_ptr = &clips[prev_clip];
        if (prev_clip != cached_prev_clip_index_) {
            prev_channel_lookup_.assign(num_joints, nullptr);
            for (const auto& ch : prev_clip_ptr->channels) {
                if (ch.bone_index >= 0 && ch.bone_index < static_cast<int>(num_joints))
                    prev_channel_lookup_[ch.bone_index] = &ch;
            }
            cached_prev_clip_index_ = prev_clip;
        }
    }

    // Compute local transforms for each joint - reuse buffer
    local_transforms_.resize(num_joints);
    for (size_t i = 0; i < num_joints; i++) {
        const auto& joint = skeleton.joints[i];
        int idx = static_cast<int>(i);

        glm::vec3 translation;
        glm::quat rotation;
        glm::vec3 scale;
        sample_clip_joint(clip, channel_lookup_, idx, time, joint,
                          translation, rotation, scale);

        if (blending) {
            glm::vec3 prev_t;
            glm::quat prev_r;
            glm::vec3 prev_s;
            sample_clip_joint(prev_clip_ptr, prev_channel_lookup_, idx, prev_time, joint,
                              prev_t, prev_r, prev_s);

            float t = blend_factor;
            translation = glm::mix(prev_t, translation, t);
            rotation = glm::slerp(prev_r, rotation, t);
            scale = glm::mix(prev_s, scale, t);
        }

        glm::mat4 m = glm::mat4_cast(rotation);
        m[0] *= scale.x;
        m[1] *= scale.y;
        m[2] *= scale.z;
        m[3] = glm::vec4(translation, 1.0f);
        local_transforms_[i] = m;
    }

    // Compute world transforms by walking hierarchy (clamped to MAX_BONES)
    size_t count = std::min(num_joints, static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < count; i++) {
        const auto& joint = skeleton.joints[i];
        if (joint.parent_index >= 0 && joint.parent_index < static_cast<int>(count)) {
            world_transforms[i] = world_transforms[joint.parent_index] * local_transforms_[i];
        } else {
            world_transforms[i] = local_transforms_[i];
        }
    }

    // Compute final bone matrices: world_transform * inverse_bind_matrix
    for (size_t i = 0; i < count; i++) {
        bone_matrices[i] = world_transforms[i] * skeleton.joints[i].inverse_bind_matrix;
    }

    // Fill remaining slots with identity
    for (size_t i = count; i < MAX_BONES; i++) {
        bone_matrices[i] = glm::mat4(1.0f);
        world_transforms[i] = glm::mat4(1.0f);
    }
}

} // namespace mmo::engine::animation

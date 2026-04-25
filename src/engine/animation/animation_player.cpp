#include "animation_player.hpp"

#include "engine/core/profiler.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace mmo::engine::animation {

void AnimationPlayer::reset() {
    time_ = 0.0f;
    prev_clip_ = -1;
    blend_factor_ = 1.0f;
    cached_clip_index_ = -1;
    cached_prev_clip_index_ = -1;
    current_cursors_.clear();
    prev_cursors_.clear();
    for (auto& m : bone_matrices_) m = glm::mat4(1.0f);
    for (auto& m : world_transforms_) m = glm::mat4(1.0f);
}

void AnimationPlayer::crossfade_to(int clip_index, float duration) {
    if (clip_index == current_clip_) {
        return;
    }
    prev_clip_ = current_clip_;
    prev_time_ = time_;
    blend_factor_ = 0.0f;
    blend_duration_ = duration;
    current_clip_ = clip_index;
    time_ = 0.0f;
    cached_clip_index_ = -1;
    cached_prev_clip_index_ = -1;
    prev_cursors_ = std::move(current_cursors_);
    current_cursors_.clear();
}

void AnimationPlayer::update(const Skeleton& skeleton, const std::vector<AnimationClip>& clips, float dt) {
    ENGINE_PROFILE_ZONE("AnimPlayer::update");
    if (clips.empty() || !playing_) {
        return;
    }

    if (current_clip_ < 0 || current_clip_ >= static_cast<int>(clips.size())) {
        current_clip_ = 0;
    }

    const auto& clip = clips[current_clip_];

    time_ += dt * speed_;
    if (clip.duration <= 0.0f) {
        // Zero-duration clip (single keyframe pose); fmod would be UB.
        time_ = 0.0f;
        playing_ = false;
    } else if (time_ >= clip.duration) {
        if (loop_) {
            time_ = std::fmod(time_, clip.duration);
        } else {
            time_ = clip.duration;
            playing_ = false;
        }
    }

    if (blend_factor_ < 1.0f) {
        if (blend_duration_ > 0.0f) {
            blend_factor_ += dt / blend_duration_;
        } else {
            blend_factor_ = 1.0f;
        }
        if (blend_factor_ >= 1.0f) {
            blend_factor_ = 1.0f;
            prev_clip_ = -1;
        }

        if (prev_clip_ >= 0 && prev_clip_ < static_cast<int>(clips.size())) {
            const auto& prev = clips[prev_clip_];
            prev_time_ += dt * speed_;
            if (prev.duration > 0.0f && prev_time_ > prev.duration) {
                prev_time_ = std::fmod(prev_time_, prev.duration);
            }
        }
    }

    compute_bone_matrices(skeleton, clips);
}

// Sample a clip's channel for a single joint. Cursors are owned by the player
// so the clip itself stays const-shareable across instances.
static void sample_clip_joint(const AnimationClip* clip, const std::vector<const AnimationChannel*>& channels,
                              std::vector<ChannelCursors>& cursors, int joint_idx, float time, const Joint& joint,
                              glm::vec3& out_translation, glm::quat& out_rotation, glm::vec3& out_scale) {
    out_translation = joint.local_translation;
    out_rotation = joint.local_rotation;
    out_scale = joint.local_scale;

    if (!clip) {
        return;
    }
    if (joint_idx < 0 || joint_idx >= static_cast<int>(channels.size())) {
        return;
    }
    const auto* ch = channels[joint_idx];
    if (!ch) {
        return;
    }

    auto& cur = cursors[joint_idx];

    if (!ch->position_times.empty()) {
        out_translation = interpolate_keyframes(ch->position_times, ch->positions, time, cur.pos);
    }
    if (!ch->rotation_times.empty()) {
        out_rotation = interpolate_keyframes(ch->rotation_times, ch->rotations, time, cur.rot);
    }
    if (!ch->scale_times.empty()) {
        out_scale = interpolate_keyframes(ch->scale_times, ch->scales, time, cur.scale);
    }
}

void AnimationPlayer::compute_bone_matrices(const Skeleton& skeleton, const std::vector<AnimationClip>& clips) {
    size_t num_joints = skeleton.joints.size();
    if (num_joints == 0) {
        return;
    }

    const AnimationClip* clip = nullptr;
    if (current_clip_ >= 0 && current_clip_ < static_cast<int>(clips.size())) {
        clip = &clips[current_clip_];
        if (current_clip_ != cached_clip_index_) {
            channel_lookup_.assign(num_joints, nullptr);
            for (const auto& ch : clip->channels) {
                if (ch.bone_index >= 0 && ch.bone_index < static_cast<int>(num_joints)) {
                    channel_lookup_[ch.bone_index] = &ch;
                }
            }
            cached_clip_index_ = current_clip_;
            current_cursors_.assign(num_joints, ChannelCursors{});
        } else if (current_cursors_.size() != num_joints) {
            current_cursors_.assign(num_joints, ChannelCursors{});
        }
    } else {
        channel_lookup_.assign(num_joints, nullptr);
        cached_clip_index_ = -1;
        current_cursors_.assign(num_joints, ChannelCursors{});
    }

    const AnimationClip* prev_clip_ptr = nullptr;
    bool blending = (blend_factor_ < 1.0f && prev_clip_ >= 0 && prev_clip_ < static_cast<int>(clips.size()));
    if (blending) {
        prev_clip_ptr = &clips[prev_clip_];
        if (prev_clip_ != cached_prev_clip_index_) {
            prev_channel_lookup_.assign(num_joints, nullptr);
            for (const auto& ch : prev_clip_ptr->channels) {
                if (ch.bone_index >= 0 && ch.bone_index < static_cast<int>(num_joints)) {
                    prev_channel_lookup_[ch.bone_index] = &ch;
                }
            }
            cached_prev_clip_index_ = prev_clip_;
            prev_cursors_.assign(num_joints, ChannelCursors{});
        } else if (prev_cursors_.size() != num_joints) {
            prev_cursors_.assign(num_joints, ChannelCursors{});
        }
    }

    local_transforms_.resize(num_joints);
    for (size_t i = 0; i < num_joints; i++) {
        const auto& joint = skeleton.joints[i];
        int idx = static_cast<int>(i);

        glm::vec3 translation;
        glm::quat rotation;
        glm::vec3 scale;
        sample_clip_joint(clip, channel_lookup_, current_cursors_, idx, time_, joint, translation, rotation, scale);

        if (blending) {
            glm::vec3 prev_t;
            glm::quat prev_r;
            glm::vec3 prev_s;
            sample_clip_joint(prev_clip_ptr, prev_channel_lookup_, prev_cursors_, idx, prev_time_, joint, prev_t,
                              prev_r, prev_s);

            float t = blend_factor_;
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

    size_t count = std::min(num_joints, static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < count; i++) {
        const auto& joint = skeleton.joints[i];
        if (joint.parent_index >= 0 && joint.parent_index < static_cast<int>(count)) {
            world_transforms_[i] = world_transforms_[joint.parent_index] * local_transforms_[i];
        } else {
            world_transforms_[i] = local_transforms_[i];
        }
    }

    for (size_t i = 0; i < count; i++) {
        bone_matrices_[i] = world_transforms_[i] * skeleton.joints[i].inverse_bind_matrix;
    }

    for (size_t i = count; i < MAX_BONES; i++) {
        bone_matrices_[i] = glm::mat4(1.0f);
        world_transforms_[i] = glm::mat4(1.0f);
    }
}

} // namespace mmo::engine::animation

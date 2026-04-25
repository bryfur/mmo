#include "engine/animation/animation_player.hpp"
#include <gtest/gtest.h>

#include <cmath>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace mmo::engine::animation;

static Skeleton make_test_skeleton() {
    Skeleton skel;
    skel.joints.resize(2);

    Joint& root = skel.joints[0];
    root.name = "Root";
    root.parent_index = -1;
    root.local_translation = glm::vec3(0.0f);
    root.local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    root.local_scale = glm::vec3(1.0f);
    root.inverse_bind_matrix = glm::mat4(1.0f);

    Joint& child = skel.joints[1];
    child.name = "Child";
    child.parent_index = 0;
    child.local_translation = glm::vec3(0.0f, 1.0f, 0.0f);
    child.local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    child.local_scale = glm::vec3(1.0f);
    child.inverse_bind_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    return skel;
}

static AnimationClip make_test_clip(float duration, float tx = 1.0f) {
    AnimationClip clip;
    clip.name = "test_clip";
    clip.duration = duration;

    AnimationChannel root_ch;
    root_ch.bone_index = 0;
    root_ch.position_times = {0.0f, duration};
    root_ch.positions = {glm::vec3(0.0f), glm::vec3(tx, 0.0f, 0.0f)};
    root_ch.rotation_times = {0.0f};
    root_ch.rotations = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    root_ch.scale_times = {0.0f};
    root_ch.scales = {glm::vec3(1.0f)};

    clip.channels.push_back(root_ch);
    return clip;
}

static AnimationClip make_alt_clip(float duration, float tz = 2.0f) {
    AnimationClip clip;
    clip.name = "alt_clip";
    clip.duration = duration;

    AnimationChannel root_ch;
    root_ch.bone_index = 0;
    root_ch.position_times = {0.0f, duration};
    root_ch.positions = {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, tz)};
    root_ch.rotation_times = {0.0f};
    root_ch.rotations = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    root_ch.scale_times = {0.0f};
    root_ch.scales = {glm::vec3(1.0f)};

    clip.channels.push_back(root_ch);
    return clip;
}

static bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

class AnimationPlayerTest : public ::testing::Test {
protected:
    void SetUp() override {
        skeleton = make_test_skeleton();
        clips.push_back(make_test_clip(1.0f));
        clips.push_back(make_alt_clip(2.0f));
    }

    Skeleton skeleton;
    std::vector<AnimationClip> clips;
};

TEST_F(AnimationPlayerTest, DefaultState) {
    AnimationPlayer player;
    EXPECT_EQ(player.current_clip(), 0);
    EXPECT_FLOAT_EQ(player.current_time(), 0.0f);
    EXPECT_TRUE(player.is_playing());
    EXPECT_TRUE(player.loops());
    EXPECT_FLOAT_EQ(player.speed(), 1.0f);
    EXPECT_EQ(player.prev_clip(), -1);
    EXPECT_FLOAT_EQ(player.blend_factor(), 1.0f);
}

TEST_F(AnimationPlayerTest, ResetClearsState) {
    AnimationPlayer player;
    player.seek(0.5f);
    player.crossfade_to(1, 0.3f);
    player.reset();

    EXPECT_FLOAT_EQ(player.current_time(), 0.0f);
    EXPECT_EQ(player.prev_clip(), -1);
    EXPECT_FLOAT_EQ(player.blend_factor(), 1.0f);
    EXPECT_EQ(player.bone_matrices()[0], glm::mat4(1.0f));
    EXPECT_EQ(player.bone_matrices()[1], glm::mat4(1.0f));
}

TEST_F(AnimationPlayerTest, UpdateAdvancesTime) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.3f);
    EXPECT_TRUE(approx(player.current_time(), 0.3f));
}

TEST_F(AnimationPlayerTest, UpdateRespectsSpeed) {
    AnimationPlayer player;
    player.set_speed(2.0f);
    player.update(skeleton, clips, 0.25f);
    EXPECT_TRUE(approx(player.current_time(), 0.5f));
}

TEST_F(AnimationPlayerTest, UpdateDoesNothingWhenNotPlaying) {
    AnimationPlayer player;
    player.set_playing(false);
    player.update(skeleton, clips, 0.5f);
    EXPECT_FLOAT_EQ(player.current_time(), 0.0f);
}

TEST_F(AnimationPlayerTest, UpdateDoesNothingWithEmptyClips) {
    AnimationPlayer player;
    std::vector<AnimationClip> empty;
    player.update(skeleton, empty, 0.5f);
    EXPECT_FLOAT_EQ(player.current_time(), 0.0f);
}

TEST_F(AnimationPlayerTest, LoopingWrapsTime) {
    AnimationPlayer player;
    player.set_loop(true);
    player.update(skeleton, clips, 1.3f);
    EXPECT_TRUE(approx(player.current_time(), 0.3f));
    EXPECT_TRUE(player.is_playing());
}

TEST_F(AnimationPlayerTest, LoopingWrapsMultipleTimes) {
    AnimationPlayer player;
    player.set_loop(true);
    player.update(skeleton, clips, 3.7f);
    EXPECT_TRUE(approx(player.current_time(), 0.7f));
    EXPECT_TRUE(player.is_playing());
}

TEST_F(AnimationPlayerTest, NonLoopingClampsAtEnd) {
    AnimationPlayer player;
    player.set_loop(false);
    player.update(skeleton, clips, 1.5f);
    EXPECT_FLOAT_EQ(player.current_time(), 1.0f);
    EXPECT_FALSE(player.is_playing());
}

TEST_F(AnimationPlayerTest, NonLoopingStopsExactlyAtDuration) {
    AnimationPlayer player;
    player.set_loop(false);
    player.update(skeleton, clips, 1.0f);
    EXPECT_FLOAT_EQ(player.current_time(), 1.0f);
    EXPECT_FALSE(player.is_playing());
}

TEST_F(AnimationPlayerTest, IsClipFinishedWhenNonLoopingEnds) {
    AnimationPlayer player;
    player.set_loop(false);
    player.update(skeleton, clips, 2.0f);
    EXPECT_FLOAT_EQ(player.current_time(), 1.0f);
    EXPECT_FALSE(player.is_playing());
}

TEST_F(AnimationPlayerTest, CrossfadeSetsUpBlendState) {
    AnimationPlayer player;
    player.seek(0.5f);
    player.crossfade_to(1, 0.3f);

    EXPECT_EQ(player.current_clip(), 1);
    EXPECT_FLOAT_EQ(player.current_time(), 0.0f);
    EXPECT_EQ(player.prev_clip(), 0);
    EXPECT_FLOAT_EQ(player.prev_time(), 0.5f);
    EXPECT_FLOAT_EQ(player.blend_factor(), 0.0f);
    EXPECT_FLOAT_EQ(player.blend_duration(), 0.3f);
}

TEST_F(AnimationPlayerTest, CrossfadeToSameClipIsIgnored) {
    AnimationPlayer player;
    player.seek(0.5f);
    player.crossfade_to(0, 0.3f);

    EXPECT_EQ(player.current_clip(), 0);
    EXPECT_FLOAT_EQ(player.current_time(), 0.5f);
    EXPECT_EQ(player.prev_clip(), -1);
    EXPECT_FLOAT_EQ(player.blend_factor(), 1.0f);
}

TEST_F(AnimationPlayerTest, BlendFactorAdvancesDuringCrossfade) {
    AnimationPlayer player;
    player.seek(0.5f);
    player.crossfade_to(1, 0.4f);

    player.update(skeleton, clips, 0.1f);
    EXPECT_TRUE(approx(player.blend_factor(), 0.25f));
    EXPECT_EQ(player.prev_clip(), 0);

    player.update(skeleton, clips, 0.1f);
    EXPECT_TRUE(approx(player.blend_factor(), 0.5f));
}

TEST_F(AnimationPlayerTest, BlendCompletesAndClearsPrevClip) {
    AnimationPlayer player;
    player.seek(0.5f);
    player.crossfade_to(1, 0.2f);

    player.update(skeleton, clips, 0.3f);
    EXPECT_FLOAT_EQ(player.blend_factor(), 1.0f);
    EXPECT_EQ(player.prev_clip(), -1);
}

TEST_F(AnimationPlayerTest, PreviousClipTimeAdvancesDuringCrossfade) {
    AnimationPlayer player;
    player.seek(0.2f);
    player.crossfade_to(1, 0.5f);

    float prev_time_before = player.prev_time();
    player.update(skeleton, clips, 0.1f);
    EXPECT_TRUE(approx(player.prev_time(), prev_time_before + 0.1f));
}

TEST_F(AnimationPlayerTest, BoneMatricesAtTimeZero) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.0f);

    glm::mat4 root_bone = player.bone_matrices()[0];
    EXPECT_TRUE(approx(root_bone[3][0], 0.0f));
    EXPECT_TRUE(approx(root_bone[3][1], 0.0f));
    EXPECT_TRUE(approx(root_bone[3][2], 0.0f));
}

TEST_F(AnimationPlayerTest, BoneMatricesAtMidpoint) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.5f);

    glm::mat4 root_bone = player.bone_matrices()[0];
    EXPECT_TRUE(approx(root_bone[3][0], 0.5f));
    EXPECT_TRUE(approx(root_bone[3][1], 0.0f));
    EXPECT_TRUE(approx(root_bone[3][2], 0.0f));
}

TEST_F(AnimationPlayerTest, ChildBoneFollowsParent) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.5f);

    glm::mat4 child_bone = player.bone_matrices()[1];
    EXPECT_TRUE(approx(child_bone[3][0], 0.5f));
    EXPECT_TRUE(approx(child_bone[3][1], 0.0f));
    EXPECT_TRUE(approx(child_bone[3][2], 0.0f));
}

TEST_F(AnimationPlayerTest, WorldTransformsArePopulated) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.5f);

    bool root_is_identity = (player.world_transforms()[0] == glm::mat4(1.0f));
    EXPECT_FALSE(root_is_identity);
}

TEST_F(AnimationPlayerTest, BoneMatricesIdentityBeyondJointCount) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.5f);

    auto bones = player.bone_matrices();
    for (int i = 2; i < MAX_BONES; i++) {
        EXPECT_EQ(bones[i], glm::mat4(1.0f)) << "bone_matrices[" << i << "] should be identity";
    }
}

TEST_F(AnimationPlayerTest, CrossfadeBlendsBoneMatrices) {
    AnimationPlayer player;
    player.crossfade_to(1, 1.0f);
    player.update(skeleton, clips, 0.5f);

    glm::mat4 root_bone = player.bone_matrices()[0];
    EXPECT_TRUE(approx(root_bone[3][0], 0.25f)) << "Expected blended x ~0.25, got " << root_bone[3][0];
    EXPECT_TRUE(approx(root_bone[3][2], 0.25f)) << "Expected blended z ~0.25, got " << root_bone[3][2];
}

TEST_F(AnimationPlayerTest, EmptySkeletonProducesNoMatrices) {
    AnimationPlayer player;
    Skeleton empty_skel;
    player.update(empty_skel, clips, 0.5f);
    EXPECT_EQ(player.bone_matrices()[0], glm::mat4(1.0f));
}

TEST_F(AnimationPlayerTest, JointWithNoChannelsUsesBindPose) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.0f);

    glm::mat4 child_world = player.world_transforms()[1];
    EXPECT_TRUE(approx(child_world[3][0], 0.0f));
    EXPECT_TRUE(approx(child_world[3][1], 1.0f));
    EXPECT_TRUE(approx(child_world[3][2], 0.0f));
}

TEST_F(AnimationPlayerTest, MultipleUpdatesAccumulateCorrectly) {
    AnimationPlayer player;

    player.update(skeleton, clips, 0.2f);
    player.update(skeleton, clips, 0.3f);

    EXPECT_TRUE(approx(player.current_time(), 0.5f));
    glm::mat4 root_bone = player.bone_matrices()[0];
    EXPECT_TRUE(approx(root_bone[3][0], 0.5f));
}

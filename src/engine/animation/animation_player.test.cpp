#include <gtest/gtest.h>
#include "engine/animation/animation_player.hpp"

#include <cmath>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace mmo::engine::animation;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Create a 2-bone skeleton: root (index 0) + child (index 1).
// Root at origin, child offset 1 unit along Y.
// Inverse bind matrices are the inverse of each joint's rest world transform.
static Skeleton make_test_skeleton() {
    Skeleton skel;
    skel.joints.resize(2);

    // Root joint at origin
    Joint& root = skel.joints[0];
    root.name = "Root";
    root.parent_index = -1;
    root.local_translation = glm::vec3(0.0f);
    root.local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity
    root.local_scale = glm::vec3(1.0f);
    root.inverse_bind_matrix = glm::mat4(1.0f); // identity (rest pose at origin)

    // Child joint offset by (0, 1, 0) in local space
    Joint& child = skel.joints[1];
    child.name = "Child";
    child.parent_index = 0;
    child.local_translation = glm::vec3(0.0f, 1.0f, 0.0f);
    child.local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    child.local_scale = glm::vec3(1.0f);
    // In rest pose, child world pos = (0,1,0), so inverse bind = translate(0,-1,0)
    child.inverse_bind_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    return skel;
}

// Create a clip of given duration that translates the root bone from (0,0,0) to (tx,0,0).
// The child bone has no channels, so it inherits its bind-pose local transform.
static AnimationClip make_test_clip(float duration, float tx = 1.0f) {
    AnimationClip clip;
    clip.name = "test_clip";
    clip.duration = duration;

    AnimationChannel root_ch;
    root_ch.bone_index = 0;
    root_ch.position_times = {0.0f, duration};
    root_ch.positions = {glm::vec3(0.0f), glm::vec3(tx, 0.0f, 0.0f)};
    // Constant identity rotation + uniform scale
    root_ch.rotation_times = {0.0f};
    root_ch.rotations = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    root_ch.scale_times = {0.0f};
    root_ch.scales = {glm::vec3(1.0f)};

    clip.channels.push_back(root_ch);
    return clip;
}

// Create a second distinct clip (translates root along Z instead of X).
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

// Approximate float comparison helper
static bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class AnimationPlayerTest : public ::testing::Test {
protected:
    void SetUp() override {
        skeleton = make_test_skeleton();
        clips.push_back(make_test_clip(1.0f));       // clip 0: 1s, root moves X 0->1
        clips.push_back(make_alt_clip(2.0f));         // clip 1: 2s, root moves Z 0->2
    }

    Skeleton skeleton;
    std::vector<AnimationClip> clips;
};

// ===========================================================================
// Basic playback state
// ===========================================================================

TEST_F(AnimationPlayerTest, DefaultState) {
    AnimationPlayer player;
    EXPECT_EQ(player.current_clip, 0);
    EXPECT_FLOAT_EQ(player.time, 0.0f);
    EXPECT_TRUE(player.playing);
    EXPECT_TRUE(player.loop);
    EXPECT_FLOAT_EQ(player.speed, 1.0f);
    EXPECT_EQ(player.prev_clip, -1);
    EXPECT_FLOAT_EQ(player.blend_factor, 1.0f);
}

TEST_F(AnimationPlayerTest, ResetClearsState) {
    AnimationPlayer player;
    player.time = 0.5f;
    player.prev_clip = 1;
    player.blend_factor = 0.5f;
    player.reset();

    EXPECT_FLOAT_EQ(player.time, 0.0f);
    EXPECT_EQ(player.prev_clip, -1);
    EXPECT_FLOAT_EQ(player.blend_factor, 1.0f);
    // Bone matrices should be identity after reset
    EXPECT_EQ(player.bone_matrices[0], glm::mat4(1.0f));
    EXPECT_EQ(player.bone_matrices[1], glm::mat4(1.0f));
}

// ===========================================================================
// update() advances time
// ===========================================================================

TEST_F(AnimationPlayerTest, UpdateAdvancesTime) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.3f);
    EXPECT_TRUE(approx(player.time, 0.3f));
}

TEST_F(AnimationPlayerTest, UpdateRespectsSpeed) {
    AnimationPlayer player;
    player.speed = 2.0f;
    player.update(skeleton, clips, 0.25f);
    EXPECT_TRUE(approx(player.time, 0.5f));
}

TEST_F(AnimationPlayerTest, UpdateDoesNothingWhenNotPlaying) {
    AnimationPlayer player;
    player.playing = false;
    player.update(skeleton, clips, 0.5f);
    EXPECT_FLOAT_EQ(player.time, 0.0f);
}

TEST_F(AnimationPlayerTest, UpdateDoesNothingWithEmptyClips) {
    AnimationPlayer player;
    std::vector<AnimationClip> empty;
    player.update(skeleton, empty, 0.5f);
    EXPECT_FLOAT_EQ(player.time, 0.0f);
}

// ===========================================================================
// Looping behavior
// ===========================================================================

TEST_F(AnimationPlayerTest, LoopingWrapsTime) {
    AnimationPlayer player;
    player.loop = true;
    player.current_clip = 0; // duration = 1.0

    // Advance past the end
    player.update(skeleton, clips, 1.3f);
    // Should wrap: 1.3 mod 1.0 = 0.3
    EXPECT_TRUE(approx(player.time, 0.3f));
    EXPECT_TRUE(player.playing); // still playing
}

TEST_F(AnimationPlayerTest, LoopingWrapsMultipleTimes) {
    AnimationPlayer player;
    player.loop = true;
    player.current_clip = 0; // duration = 1.0

    player.update(skeleton, clips, 3.7f);
    // 3.7 mod 1.0 = 0.7
    EXPECT_TRUE(approx(player.time, 0.7f));
    EXPECT_TRUE(player.playing);
}

// ===========================================================================
// Non-looping behavior
// ===========================================================================

TEST_F(AnimationPlayerTest, NonLoopingClampsAtEnd) {
    AnimationPlayer player;
    player.loop = false;
    player.current_clip = 0; // duration = 1.0

    player.update(skeleton, clips, 1.5f);
    EXPECT_FLOAT_EQ(player.time, 1.0f);
    EXPECT_FALSE(player.playing);
}

TEST_F(AnimationPlayerTest, NonLoopingStopsExactlyAtDuration) {
    AnimationPlayer player;
    player.loop = false;
    player.current_clip = 0; // duration = 1.0

    // Advance to exactly the duration
    player.update(skeleton, clips, 1.0f);
    // time == duration, playing should stop
    EXPECT_FLOAT_EQ(player.time, 1.0f);
    EXPECT_FALSE(player.playing);
}

TEST_F(AnimationPlayerTest, IsClipFinishedWhenNonLoopingEnds) {
    AnimationPlayer player;
    player.loop = false;
    player.current_clip = 0; // duration = 1.0

    player.update(skeleton, clips, 2.0f);
    // Time is clamped and playing is false
    EXPECT_FLOAT_EQ(player.time, 1.0f);
    EXPECT_FALSE(player.playing);
}

// ===========================================================================
// crossfade_to()
// ===========================================================================

TEST_F(AnimationPlayerTest, CrossfadeSetsUpBlendState) {
    AnimationPlayer player;
    player.current_clip = 0;
    player.time = 0.5f;

    player.crossfade_to(1, 0.3f);

    EXPECT_EQ(player.current_clip, 1);
    EXPECT_FLOAT_EQ(player.time, 0.0f);         // new clip starts at 0
    EXPECT_EQ(player.prev_clip, 0);              // previous clip recorded
    EXPECT_FLOAT_EQ(player.prev_time, 0.5f);     // previous time recorded
    EXPECT_FLOAT_EQ(player.blend_factor, 0.0f);  // blend starts at 0
    EXPECT_FLOAT_EQ(player.blend_duration, 0.3f);
}

TEST_F(AnimationPlayerTest, CrossfadeToSameClipIsIgnored) {
    AnimationPlayer player;
    player.current_clip = 0;
    player.time = 0.5f;

    player.crossfade_to(0, 0.3f);

    // Should be unchanged
    EXPECT_EQ(player.current_clip, 0);
    EXPECT_FLOAT_EQ(player.time, 0.5f);
    EXPECT_EQ(player.prev_clip, -1);
    EXPECT_FLOAT_EQ(player.blend_factor, 1.0f);
}

TEST_F(AnimationPlayerTest, BlendFactorAdvancesDuringCrossfade) {
    AnimationPlayer player;
    player.current_clip = 0;
    player.time = 0.5f;
    player.crossfade_to(1, 0.4f);

    // After 0.1s, blend should be 0.1/0.4 = 0.25
    player.update(skeleton, clips, 0.1f);
    EXPECT_TRUE(approx(player.blend_factor, 0.25f));
    EXPECT_EQ(player.prev_clip, 0); // still blending

    // After another 0.1s, blend should be 0.5
    player.update(skeleton, clips, 0.1f);
    EXPECT_TRUE(approx(player.blend_factor, 0.5f));
}

TEST_F(AnimationPlayerTest, BlendCompletesAndClearsPrevClip) {
    AnimationPlayer player;
    player.current_clip = 0;
    player.time = 0.5f;
    player.crossfade_to(1, 0.2f);

    // Advance past blend duration
    player.update(skeleton, clips, 0.3f);
    EXPECT_FLOAT_EQ(player.blend_factor, 1.0f);
    EXPECT_EQ(player.prev_clip, -1); // prev clip cleared
}

TEST_F(AnimationPlayerTest, PreviousClipTimeAdvancesDuringCrossfade) {
    AnimationPlayer player;
    player.current_clip = 0;
    player.time = 0.2f;
    player.crossfade_to(1, 0.5f);

    float prev_time_before = player.prev_time; // 0.2
    player.update(skeleton, clips, 0.1f);
    // Previous clip time should have advanced by dt * speed
    EXPECT_TRUE(approx(player.prev_time, prev_time_before + 0.1f));
}

// ===========================================================================
// compute_bone_matrices() with the 2-bone skeleton
// ===========================================================================

TEST_F(AnimationPlayerTest, BoneMatricesAtTimeZero) {
    AnimationPlayer player;
    player.current_clip = 0;
    player.update(skeleton, clips, 0.0f);

    // At t=0, root is at (0,0,0), so bone_matrix[0] = identity * identity = identity
    glm::mat4 root_bone = player.bone_matrices[0];
    EXPECT_TRUE(approx(root_bone[3][0], 0.0f));
    EXPECT_TRUE(approx(root_bone[3][1], 0.0f));
    EXPECT_TRUE(approx(root_bone[3][2], 0.0f));
}

TEST_F(AnimationPlayerTest, BoneMatricesAtMidpoint) {
    AnimationPlayer player;
    player.current_clip = 0; // 1s clip, root moves from x=0 to x=1
    player.update(skeleton, clips, 0.5f);

    // At t=0.5, root should be at (0.5, 0, 0)
    // bone_matrix[0] = world_transform * inverse_bind = translate(0.5,0,0) * identity
    glm::mat4 root_bone = player.bone_matrices[0];
    EXPECT_TRUE(approx(root_bone[3][0], 0.5f));
    EXPECT_TRUE(approx(root_bone[3][1], 0.0f));
    EXPECT_TRUE(approx(root_bone[3][2], 0.0f));
}

TEST_F(AnimationPlayerTest, ChildBoneFollowsParent) {
    AnimationPlayer player;
    player.current_clip = 0; // root moves from x=0 to x=1
    player.update(skeleton, clips, 0.5f);

    // Child local offset is (0,1,0), root is at (0.5,0,0)
    // Child world pos = (0.5, 1, 0)
    // Child bone_matrix = world_transform * inverse_bind
    //                   = translate(0.5, 1, 0) * translate(0, -1, 0)
    //                   = translate(0.5, 0, 0)
    glm::mat4 child_bone = player.bone_matrices[1];
    EXPECT_TRUE(approx(child_bone[3][0], 0.5f));
    EXPECT_TRUE(approx(child_bone[3][1], 0.0f));
    EXPECT_TRUE(approx(child_bone[3][2], 0.0f));
}

TEST_F(AnimationPlayerTest, WorldTransformsArePopulated) {
    AnimationPlayer player;
    player.current_clip = 0;
    player.update(skeleton, clips, 0.5f);

    // world_transforms should be populated (not all identity) after update
    // The exact values depend on the bone matrix computation internals
    // (world_transforms = accumulated parent chain, bone_matrices = world * inverse_bind)
    // Just verify they're not all zeros/identity for animated bones
    bool root_is_identity = (player.world_transforms[0] == glm::mat4(1.0f));
    // Root should have some animation applied (translation)
    EXPECT_FALSE(root_is_identity);
}

TEST_F(AnimationPlayerTest, BoneMatricesIdentityBeyondJointCount) {
    AnimationPlayer player;
    player.update(skeleton, clips, 0.5f);

    // Indices beyond the skeleton's joint count should be identity
    for (int i = 2; i < MAX_BONES; i++) {
        EXPECT_EQ(player.bone_matrices[i], glm::mat4(1.0f))
            << "bone_matrices[" << i << "] should be identity";
    }
}

// ===========================================================================
// Crossfade produces blended bone matrices
// ===========================================================================

TEST_F(AnimationPlayerTest, CrossfadeBlendsBoneMatrices) {
    AnimationPlayer player;
    player.current_clip = 0;
    player.time = 0.0f;

    // Clip 0: root at x=0 at t=0
    // Clip 1: root at z=0 at t=0
    // Crossfade from clip 0 to clip 1 over 1.0s
    player.crossfade_to(1, 1.0f);

    // Advance 0.5s => blend_factor = 0.5, both clips at t=0.5
    // Clip 0 at t=0.5: root x=0.5, z=0
    // Clip 1 at t=0.5: root x=0, z=0.5
    // Blended: x = lerp(0.5, 0, 0.5) = 0.25, z = lerp(0, 0.5, 0.5) = 0.25
    player.update(skeleton, clips, 0.5f);

    glm::mat4 root_bone = player.bone_matrices[0];
    EXPECT_TRUE(approx(root_bone[3][0], 0.25f))
        << "Expected blended x ~0.25, got " << root_bone[3][0];
    EXPECT_TRUE(approx(root_bone[3][2], 0.25f))
        << "Expected blended z ~0.25, got " << root_bone[3][2];
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_F(AnimationPlayerTest, EmptySkeletonProducesNoMatrices) {
    AnimationPlayer player;
    Skeleton empty_skel;
    // Should not crash
    player.update(empty_skel, clips, 0.5f);
    // Bone matrices should remain identity (from reset)
    EXPECT_EQ(player.bone_matrices[0], glm::mat4(1.0f));
}

TEST_F(AnimationPlayerTest, OutOfRangeClipIndexGetsClamped) {
    AnimationPlayer player;
    player.current_clip = 99;
    // Should clamp to 0 and not crash
    player.update(skeleton, clips, 0.5f);
    EXPECT_EQ(player.current_clip, 0);
}

TEST_F(AnimationPlayerTest, NegativeClipIndexGetsClamped) {
    AnimationPlayer player;
    player.current_clip = -5;
    player.update(skeleton, clips, 0.5f);
    EXPECT_EQ(player.current_clip, 0);
}

TEST_F(AnimationPlayerTest, JointWithNoChannelsUsesBindPose) {
    // Create a clip that only animates bone 0, not bone 1.
    // Bone 1 should use its bind-pose local transform.
    AnimationPlayer player;
    player.current_clip = 0; // only animates root
    player.update(skeleton, clips, 0.0f);

    // Child world transform should just be its local offset from root
    glm::mat4 child_world = player.world_transforms[1];
    EXPECT_TRUE(approx(child_world[3][0], 0.0f));
    EXPECT_TRUE(approx(child_world[3][1], 1.0f));
    EXPECT_TRUE(approx(child_world[3][2], 0.0f));
}

TEST_F(AnimationPlayerTest, MultipleUpdatesAccumulateCorrectly) {
    AnimationPlayer player;
    player.current_clip = 0; // 1s clip

    player.update(skeleton, clips, 0.2f);
    player.update(skeleton, clips, 0.3f);

    // Total time = 0.5, root should be at x=0.5
    EXPECT_TRUE(approx(player.time, 0.5f));
    glm::mat4 root_bone = player.bone_matrices[0];
    EXPECT_TRUE(approx(root_bone[3][0], 0.5f));
}

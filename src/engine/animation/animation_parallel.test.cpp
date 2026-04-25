#include <gtest/gtest.h>

#include "engine/animation/animation_player.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/parallel_for.hpp"

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <vector>

using namespace mmo::engine::animation;
using namespace mmo::engine::core::jobs;

namespace {

Skeleton make_skeleton() {
    Skeleton skel;
    skel.joints.resize(3);
    for (int i = 0; i < 3; ++i) {
        Joint& j = skel.joints[i];
        j.parent_index = i == 0 ? -1 : i - 1;
        j.local_translation = glm::vec3(0.0f, static_cast<float>(i), 0.0f);
        j.local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        j.local_scale = glm::vec3(1.0f);
        j.inverse_bind_matrix = glm::mat4(1.0f);
    }
    return skel;
}

AnimationClip make_clip(float duration) {
    AnimationClip c;
    c.name = "loop";
    c.duration = duration;
    AnimationChannel ch;
    ch.bone_index = 1;
    ch.position_times = {0.0f, duration * 0.5f, duration};
    ch.positions = {glm::vec3(0, 1, 0), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0)};
    ch.rotation_times = {0.0f};
    ch.rotations = {glm::quat(1, 0, 0, 0)};
    ch.scale_times = {0.0f};
    ch.scales = {glm::vec3(1)};
    c.channels.push_back(ch);
    return c;
}

bool nearly_equal(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::fabs(a[c][r] - b[c][r]) > eps) return false;
        }
    }
    return true;
}

class AnimationParallelTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!JobSystem::instance().is_initialized()) JobSystem::instance().init();
    }
};

} // namespace

TEST_F(AnimationParallelTest, ParallelTickMatchesSerialOnIndependentInstances) {
    constexpr std::size_t N = 256;
    Skeleton skel = make_skeleton();
    std::vector<AnimationClip> clips{make_clip(2.0f)};

    std::vector<AnimationPlayer> serial(N);
    std::vector<AnimationPlayer> parallel(N);
    for (std::size_t i = 0; i < N; ++i) {
        serial[i].seek(static_cast<float>(i) * 0.01f);
        parallel[i].seek(static_cast<float>(i) * 0.01f);
    }

    constexpr float dt = 1.0f / 60.0f;
    constexpr int steps = 30;

    for (int s = 0; s < steps; ++s) {
        for (auto& p : serial) p.update(skel, clips, dt);
    }

    for (int s = 0; s < steps; ++s) {
        parallel_for(0, N, [&](std::size_t b, std::size_t e) {
            for (std::size_t i = b; i < e; ++i) {
                parallel[i].update(skel, clips, dt);
            }
        });
    }

    for (std::size_t i = 0; i < N; ++i) {
        auto sm = serial[i].bone_matrices();
        auto pm = parallel[i].bone_matrices();
        ASSERT_EQ(sm.size(), pm.size()) << "instance " << i;
        for (std::size_t b = 0; b < sm.size(); ++b) {
            ASSERT_TRUE(nearly_equal(sm[b], pm[b]))
                << "instance " << i << " bone " << b;
        }
    }
}

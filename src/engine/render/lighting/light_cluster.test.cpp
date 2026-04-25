#include <gtest/gtest.h>

#include "engine/render/lighting/light.hpp"
#include "engine/render/lighting/light_cluster.hpp"
#include "engine/scene/camera_state.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <random>

namespace {

using namespace mmo::engine::render::lighting;
using mmo::engine::scene::CameraState;

// Reflective subclass exposing internals for CPU-only testing without needing a GPU.
class TestableClusterGrid : public ClusterGrid {
public:
    using ClusterGrid::ClusterGrid;

    // CPU-only init: reproduces the buffer setup but skips GPU allocation.
    void init_cpu_only() {
        // Touch the public scaffold via begin_frame; default identity camera with
        // a projective camera would be set per test.
    }
};

CameraState make_camera(uint32_t /*w*/, uint32_t /*h*/, float fov_deg = 60.0f, float aspect = 16.0f / 9.0f,
                        float near_plane = 0.1f, float far_plane = 100.0f) {
    CameraState c;
    c.view = glm::mat4(1.0f); // camera at origin looking down -Z
    c.projection = glm::perspective(glm::radians(fov_deg), aspect, near_plane, far_plane);
    c.view_projection = c.projection * c.view;
    c.position = glm::vec3(0.0f);
    return c;
}

// Public-only smoke shim: drives the CPU pipeline by accessing visible state.
// We can't allocate GPU buffers here, so we construct a ClusterGrid without
// init() and exercise the public methods that don't depend on the SSBO handles.
struct CpuOnlyClusterGrid {
    ClusterGrid g;
    bool inited = false;

    // Simulate the begin/add/build pipeline using a stub allocator: skip init().
    void run(const CameraState& cam, uint32_t w, uint32_t h, float n, float f, const std::vector<PointLight>& points,
             const std::vector<SpotLight>& spots) {
        // begin_frame() doesn't touch GPU buffers - safe even without init().
        g.begin_frame(cam, w, h, n, f);
        for (const auto& p : points) g.add_point_light(p);
        for (const auto& s : spots) g.add_spot_light(s);
        g.build();
    }
};

} // namespace

TEST(ClusterGrid, EmptySceneAllOffsetsZero) {
    CpuOnlyClusterGrid cg;
    cg.run(make_camera(1920, 1080), 1920, 1080, 0.1f, 100.0f, {}, {});
    const auto& offsets = cg.g.cluster_offsets();
    ASSERT_EQ(offsets.size(), CLUSTER_COUNT * 2u);
    for (uint32_t i = 0; i < CLUSTER_COUNT; ++i) {
        EXPECT_EQ(offsets[i * 2 + 0], 0u);
        EXPECT_EQ(offsets[i * 2 + 1], 0u);
    }
    EXPECT_TRUE(cg.g.light_indices().empty());
    EXPECT_EQ(cg.g.total_lights(), 0u);
}

TEST(ClusterGrid, SingleLightAtOriginTouchesNearClusters) {
    PointLight p{};
    p.position = glm::vec3(0.0f, 0.0f, -5.0f);
    p.radius = 3.0f;
    p.color = glm::vec3(1.0f);
    p.intensity = 1.0f;

    CpuOnlyClusterGrid cg;
    cg.run(make_camera(1920, 1080), 1920, 1080, 0.1f, 100.0f, {p}, {});

    const auto& offsets = cg.g.cluster_offsets();
    uint32_t total_count = 0;
    for (uint32_t i = 0; i < CLUSTER_COUNT; ++i) {
        total_count += offsets[i * 2 + 1];
    }
    // A 3-unit-radius point light at z=-5 in a 100m frustum must touch at least
    // one cluster.
    EXPECT_GT(total_count, 0u);
    // All emitted indices reference our single header (index 0).
    for (uint32_t idx : cg.g.light_indices()) {
        EXPECT_EQ(idx, 0u);
    }
}

TEST(ClusterGrid, LightFarBehindCameraTouchesNoClusters) {
    PointLight p{};
    // Place light far behind camera (positive Z is behind for default view).
    p.position = glm::vec3(0.0f, 0.0f, 500.0f);
    p.radius = 1.0f;
    p.color = glm::vec3(1.0f);
    p.intensity = 1.0f;

    CpuOnlyClusterGrid cg;
    cg.run(make_camera(1920, 1080), 1920, 1080, 0.1f, 100.0f, {p}, {});

    uint32_t total_count = 0;
    const auto& offsets = cg.g.cluster_offsets();
    for (uint32_t i = 0; i < CLUSTER_COUNT; ++i) {
        total_count += offsets[i * 2 + 1];
    }
    EXPECT_EQ(total_count, 0u);
    EXPECT_TRUE(cg.g.light_indices().empty());
}

TEST(ClusterGrid, RandomLightsAllIndicesInBounds) {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> px(-30.0f, 30.0f);
    std::uniform_real_distribution<float> py(-10.0f, 10.0f);
    std::uniform_real_distribution<float> pz(-80.0f, -1.0f);
    std::uniform_real_distribution<float> pr(0.5f, 6.0f);

    std::vector<PointLight> lights;
    for (int i = 0; i < 100; ++i) {
        PointLight p{};
        p.position = glm::vec3(px(rng), py(rng), pz(rng));
        p.radius = pr(rng);
        p.color = glm::vec3(1.0f);
        p.intensity = 1.0f;
        lights.push_back(p);
    }

    CpuOnlyClusterGrid cg;
    cg.run(make_camera(1920, 1080), 1920, 1080, 0.1f, 100.0f, lights, {});

    const uint32_t header_count = cg.g.total_lights();
    EXPECT_EQ(header_count, 100u);
    for (uint32_t idx : cg.g.light_indices()) {
        EXPECT_LT(idx, header_count);
    }
    const auto& offsets = cg.g.cluster_offsets();
    ASSERT_EQ(offsets.size(), CLUSTER_COUNT * 2u);
    for (uint32_t c = 0; c < CLUSTER_COUNT; ++c) {
        uint32_t off = offsets[c * 2 + 0];
        uint32_t cnt = offsets[c * 2 + 1];
        EXPECT_LE(off + cnt, cg.g.light_indices().size());
        EXPECT_LE(cnt, MAX_LIGHTS_PER_CLUSTER);
    }
}

TEST(ClusterGrid, LightLimitTruncatesPastMaxLights) {
    CpuOnlyClusterGrid cg;
    CameraState cam = make_camera(1920, 1080);

    cg.g.begin_frame(cam, 1920, 1080, 0.1f, 100.0f);
    for (uint32_t i = 0; i < MAX_LIGHTS + 50; ++i) {
        PointLight p{};
        p.position = glm::vec3(0.0f, 0.0f, -2.0f);
        p.radius = 0.5f;
        p.color = glm::vec3(1.0f);
        p.intensity = 1.0f;
        cg.g.add_point_light(p);
    }
    cg.g.build();
    EXPECT_EQ(cg.g.total_lights(), MAX_LIGHTS);
}

TEST(ClusterGrid, ParallelBuildIsDeterministic) {
    // Build twice with identical inputs; offsets and indices must match exactly.
    // Verifies the cluster-parallel implementation has no race-induced nondeterminism.
    std::mt19937 rng(987654);
    std::uniform_real_distribution<float> px(-30.0f, 30.0f);
    std::uniform_real_distribution<float> py(-10.0f, 10.0f);
    std::uniform_real_distribution<float> pz(-80.0f, -1.0f);
    std::uniform_real_distribution<float> pr(0.5f, 6.0f);
    std::vector<PointLight> lights;
    for (int i = 0; i < 200; ++i) {
        PointLight p{};
        p.position = glm::vec3(px(rng), py(rng), pz(rng));
        p.radius = pr(rng);
        p.color = glm::vec3(1.0f);
        p.intensity = 1.0f;
        lights.push_back(p);
    }

    CpuOnlyClusterGrid a;
    CpuOnlyClusterGrid b;
    a.run(make_camera(1920, 1080), 1920, 1080, 0.1f, 100.0f, lights, {});
    b.run(make_camera(1920, 1080), 1920, 1080, 0.1f, 100.0f, lights, {});

    EXPECT_EQ(a.g.cluster_offsets(), b.g.cluster_offsets());
    EXPECT_EQ(a.g.light_indices(), b.g.light_indices());
}

TEST(LightStruct, SizesMatchSpec) {
    EXPECT_EQ(sizeof(PointLight), 32u);
    EXPECT_EQ(sizeof(SpotLight), 64u);
    EXPECT_EQ(sizeof(LightHeader), 8u);
}

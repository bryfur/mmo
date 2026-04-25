#include "engine/scene/render_scene.hpp"
#include <glm/glm.hpp>
#include <gtest/gtest.h>

using namespace mmo::engine::scene;

TEST(RenderScene, InitiallyEmpty) {
    RenderScene scene;
    EXPECT_TRUE(scene.model_commands().empty());
    EXPECT_TRUE(scene.skinned_commands().empty());
    EXPECT_TRUE(scene.billboards().empty());
    EXPECT_TRUE(scene.debug_lines().empty());
}

TEST(RenderScene, ClearResetsEverything) {
    RenderScene scene;
    scene.add_model("test", glm::mat4(1.0f));
    scene.add_debug_line({0, 0, 0}, {1, 1, 1}, 0xFFFFFFFF);
    scene.set_draw_skybox(false);
    scene.set_draw_grass(false);

    scene.clear();

    EXPECT_TRUE(scene.model_commands().empty());
    EXPECT_TRUE(scene.debug_lines().empty());
    EXPECT_TRUE(scene.should_draw_skybox());
    EXPECT_TRUE(scene.should_draw_grass());
}

TEST(RenderScene, AddModelStringVersion) {
    RenderScene scene;
    glm::mat4 transform = glm::mat4(2.0f);
    glm::vec4 tint(1, 0, 0, 1);

    scene.add_model("my_model", transform, tint, true, true);

    ASSERT_EQ(scene.model_commands().size(), 1u);
    const auto& cmd = scene.model_commands()[0];
    EXPECT_EQ(cmd.model_name, "my_model");
    EXPECT_EQ(cmd.tint, tint);
    EXPECT_TRUE(cmd.force_non_instanced);
    EXPECT_TRUE(cmd.no_fog);
}

TEST(RenderScene, AddModelHandleVersion) {
    RenderScene scene;
    mmo::engine::ModelHandle handle = 42;
    scene.add_model(handle, glm::mat4(1.0f));

    ASSERT_EQ(scene.model_commands().size(), 1u);
    EXPECT_EQ(scene.model_commands()[0].model_handle, 42u);
}

TEST(RenderScene, SkinnedModelStoresBonePointer) {
    RenderScene scene;
    std::array<glm::mat4, 128> bones{};
    bones.fill(glm::mat4(1.0f));
    bones[0] = glm::mat4(2.0f);

    scene.add_skinned_model("skinned", glm::mat4(1.0f), bones);

    ASSERT_EQ(scene.skinned_commands().size(), 1u);
    const auto& cmd = scene.skinned_commands()[0];
    EXPECT_EQ(cmd.bone_matrices, &bones);
    EXPECT_EQ((*cmd.bone_matrices)[0], glm::mat4(2.0f));
}

TEST(RenderScene, Billboard3D) {
    RenderScene scene;
    scene.add_billboard_3d(1.0f, 2.0f, 3.0f, 0.5f, 0.75f, 0xFF0000FF, 0x00FF00FF, 0x0000FFFF);

    ASSERT_EQ(scene.billboards().size(), 1u);
    const auto& bb = scene.billboards()[0];
    EXPECT_FLOAT_EQ(bb.world_x, 1.0f);
    EXPECT_FLOAT_EQ(bb.world_y, 2.0f);
    EXPECT_FLOAT_EQ(bb.world_z, 3.0f);
    EXPECT_FLOAT_EQ(bb.fill_ratio, 0.75f);
}

TEST(RenderScene, DebugLine) {
    RenderScene scene;
    scene.add_debug_line({0, 0, 0}, {1, 2, 3}, 0xFFFF00FF);

    ASSERT_EQ(scene.debug_lines().size(), 1u);
    EXPECT_EQ(scene.debug_lines()[0].start, glm::vec3(0, 0, 0));
    EXPECT_EQ(scene.debug_lines()[0].end, glm::vec3(1, 2, 3));
    EXPECT_EQ(scene.debug_lines()[0].color, 0xFFFF00FFu);
}

TEST(RenderScene, DebugSphereGeneratesThreeCircles) {
    RenderScene scene;
    int segments = 16;
    scene.add_debug_sphere({0, 0, 0}, 1.0f, 0xFFFFFFFF, segments);

    // 3 circles * segments line segments each
    EXPECT_EQ(scene.debug_lines().size(), static_cast<size_t>(3 * segments));
}

TEST(RenderScene, DebugBoxGeneratesTwelveEdges) {
    RenderScene scene;
    scene.add_debug_box({0, 0, 0}, {1, 1, 1}, 0xFFFFFFFF);
    EXPECT_EQ(scene.debug_lines().size(), 12u);
}

TEST(RenderScene, Has3DContentDetectsModels) {
    RenderScene scene;
    scene.clear();
    scene.set_draw_skybox(false);
    scene.set_draw_ground(false);
    scene.set_draw_grass(false);
    EXPECT_FALSE(scene.has_3d_content());

    scene.add_model("test", glm::mat4(1.0f));
    EXPECT_TRUE(scene.has_3d_content());
}

TEST(RenderScene, WorldElementFlags) {
    RenderScene scene;
    scene.set_draw_skybox(false);
    EXPECT_FALSE(scene.should_draw_skybox());
    scene.set_draw_skybox(true);
    EXPECT_TRUE(scene.should_draw_skybox());

    scene.set_draw_rocks(false);
    EXPECT_FALSE(scene.should_draw_rocks());
    scene.set_draw_trees(false);
    EXPECT_FALSE(scene.should_draw_trees());
}

// ============================================================================
// Edge-case tests
// ============================================================================

TEST(RenderScene, AddManyModelsWorks) {
    RenderScene scene;
    const int count = 1500;
    for (int i = 0; i < count; ++i) {
        scene.add_model(static_cast<mmo::engine::ModelHandle>(i + 1), glm::mat4(1.0f));
    }
    EXPECT_EQ(scene.model_commands().size(), static_cast<size_t>(count));
    // Verify first and last entries have correct handles
    EXPECT_EQ(scene.model_commands().front().model_handle, 1u);
    EXPECT_EQ(scene.model_commands().back().model_handle, static_cast<uint32_t>(count));
}

TEST(RenderScene, MixedModelAndSkinnedModelCommands) {
    RenderScene scene;
    std::array<glm::mat4, 128> bones{};
    bones.fill(glm::mat4(1.0f));

    scene.add_model("static_model", glm::mat4(1.0f));
    scene.add_skinned_model("skinned_model", glm::mat4(1.0f), bones);
    scene.add_model(42u, glm::mat4(1.0f));
    scene.add_skinned_model(99u, glm::mat4(1.0f), bones);

    EXPECT_EQ(scene.model_commands().size(), 2u);
    EXPECT_EQ(scene.skinned_commands().size(), 2u);
    EXPECT_EQ(scene.model_commands()[0].model_name, "static_model");
    EXPECT_EQ(scene.model_commands()[1].model_handle, 42u);
    EXPECT_EQ(scene.skinned_commands()[0].model_name, "skinned_model");
    EXPECT_EQ(scene.skinned_commands()[1].model_handle, 99u);
}

TEST(RenderScene, ParticleEffectSpawnCommands) {
    RenderScene scene;

    // Particle spawns use a definition pointer; nullptr is fine for testing storage
    scene.add_particle_effect_spawn(nullptr, glm::vec3(1, 2, 3), glm::vec3(0, 1, 0), 5.0f);
    scene.add_particle_effect_spawn(nullptr, glm::vec3(4, 5, 6));

    ASSERT_EQ(scene.particle_effect_spawns().size(), 2u);

    const auto& spawn0 = scene.particle_effect_spawns()[0];
    EXPECT_EQ(spawn0.position, glm::vec3(1, 2, 3));
    EXPECT_EQ(spawn0.direction, glm::vec3(0, 1, 0));
    EXPECT_FLOAT_EQ(spawn0.range, 5.0f);

    const auto& spawn1 = scene.particle_effect_spawns()[1];
    EXPECT_EQ(spawn1.position, glm::vec3(4, 5, 6));
    // Default direction
    EXPECT_EQ(spawn1.direction, glm::vec3(1, 0, 0));
    // Default range
    EXPECT_FLOAT_EQ(spawn1.range, -1.0f);
}

TEST(RenderScene, ParticleEffectSpawnsSurviveClearButNotExplicitClear) {
    RenderScene scene;
    scene.add_particle_effect_spawn(nullptr, glm::vec3(0, 0, 0));

    // Regular clear does NOT clear particle spawns (they persist until renderer consumes them)
    scene.clear();
    EXPECT_EQ(scene.particle_effect_spawns().size(), 1u);

    // Explicit clear_particle_effect_spawns does clear them
    scene.clear_particle_effect_spawns();
    EXPECT_TRUE(scene.particle_effect_spawns().empty());
}

TEST(RenderScene, DebugSphereZeroSegmentsProducesZeroLines) {
    RenderScene scene;
    scene.add_debug_sphere({0, 0, 0}, 1.0f, 0xFFFFFFFF, 0);
    EXPECT_EQ(scene.debug_lines().size(), 0u);
}

TEST(RenderScene, DebugSphereOneSegmentProducesThreeLines) {
    RenderScene scene;
    scene.add_debug_sphere({0, 0, 0}, 1.0f, 0xFFFFFFFF, 1);
    // 1 segment per circle * 3 circles = 3 lines
    EXPECT_EQ(scene.debug_lines().size(), 3u);
}

TEST(RenderScene, ClearThenReAddGrowsCorrectly) {
    RenderScene scene;

    // Add 5 models, clear, add 3 more
    for (int i = 0; i < 5; ++i) {
        scene.add_model("model", glm::mat4(1.0f));
    }
    EXPECT_EQ(scene.model_commands().size(), 5u);

    scene.clear();
    EXPECT_EQ(scene.model_commands().size(), 0u);

    for (int i = 0; i < 3; ++i) {
        scene.add_model("model", glm::mat4(1.0f));
    }
    EXPECT_EQ(scene.model_commands().size(), 3u);
}

TEST(RenderScene, MultipleClearsPreserveCapacity) {
    RenderScene scene;

    // Fill with enough commands to force allocation
    for (int i = 0; i < 100; ++i) {
        scene.add_model("model", glm::mat4(1.0f));
    }
    scene.clear();
    size_t capacity_after_first_clear = scene.model_commands().capacity();

    // Second round: add same amount, clear again
    for (int i = 0; i < 100; ++i) {
        scene.add_model("model", glm::mat4(1.0f));
    }
    scene.clear();
    size_t capacity_after_second_clear = scene.model_commands().capacity();

    // std::vector::clear() preserves capacity, so no reallocation should occur
    EXPECT_EQ(capacity_after_first_clear, capacity_after_second_clear);
}

TEST(RenderScene, AddModelWithDefaultParameters) {
    RenderScene scene;
    // Only provide name + transform, relying on default tint, force_non_instanced, no_fog
    scene.add_model("default_model", glm::mat4(1.0f));

    ASSERT_EQ(scene.model_commands().size(), 1u);
    const auto& cmd = scene.model_commands()[0];
    EXPECT_EQ(cmd.model_name, "default_model");
    EXPECT_EQ(cmd.tint, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FALSE(cmd.force_non_instanced);
    EXPECT_FALSE(cmd.no_fog);
    EXPECT_EQ(cmd.model_handle, mmo::engine::INVALID_MODEL_HANDLE);
}

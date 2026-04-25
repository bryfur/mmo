#include "engine/procedural/tree_generator.hpp"
#include "engine/model_loader.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

using namespace mmo::engine::procedural;
using namespace mmo::engine;

TEST(TreeGenerator, GenerateProducesNonEmptyModel) {
    TreeParams p;
    p.seed = 1;
    p.levels = 2;
    auto model = TreeGenerator::generate(p);
    ASSERT_NE(model, nullptr);
    EXPECT_FALSE(model->meshes.empty());
    EXPECT_TRUE(model->loaded);
}

TEST(TreeGenerator, BranchMeshHasGeometry) {
    TreeParams p;
    p.seed = 1;
    p.levels = 1;
    auto model = TreeGenerator::generate(p);
    ASSERT_GE(model->meshes.size(), 1u);
    const auto& branch_mesh = model->meshes[0];
    EXPECT_GT(branch_mesh.vertices.size(), 0u);
    EXPECT_GT(branch_mesh.indices.size(), 0u);
    // Indices should be multiples of 3 (triangles)
    EXPECT_EQ(branch_mesh.indices.size() % 3, 0u);
}

TEST(TreeGenerator, LeavesMeshGenerated) {
    TreeParams p;
    p.seed = 1;
    p.levels = 1;
    p.leaf_count = 5;
    auto model = TreeGenerator::generate(p);
    // Should have 2 meshes: branches + leaves
    ASSERT_GE(model->meshes.size(), 2u);
    const auto& leaf_mesh = model->meshes[1];
    EXPECT_GT(leaf_mesh.vertices.size(), 0u);
    EXPECT_GT(leaf_mesh.indices.size(), 0u);
}

TEST(TreeGenerator, NoLeavesMeshWhenZeroCount) {
    TreeParams p;
    p.seed = 1;
    p.levels = 1;
    p.leaf_count = 0;
    auto model = TreeGenerator::generate(p);
    // Only branch mesh, no leaf mesh
    EXPECT_EQ(model->meshes.size(), 1u);
}

TEST(TreeGenerator, SeededRNGIsDeterministic) {
    TreeParams p;
    p.seed = 42;
    p.levels = 2;
    auto model1 = TreeGenerator::generate(p);
    auto model2 = TreeGenerator::generate(p);
    ASSERT_EQ(model1->meshes[0].vertices.size(), model2->meshes[0].vertices.size());
    // Verify same geometry
    for (size_t i = 0; i < model1->meshes[0].vertices.size(); i++) {
        EXPECT_FLOAT_EQ(model1->meshes[0].vertices[i].position.x, model2->meshes[0].vertices[i].position.x);
    }
}

TEST(TreeGenerator, DifferentSeedsProduceDifferentTrees) {
    TreeParams p;
    p.levels = 2;
    p.seed = 1;
    auto model1 = TreeGenerator::generate(p);
    p.seed = 99;
    auto model2 = TreeGenerator::generate(p);
    // Different seeds should produce different vertex positions
    bool any_different = false;
    size_t n = std::min(model1->meshes[0].vertices.size(), model2->meshes[0].vertices.size());
    for (size_t i = 0; i < n; i++) {
        if (model1->meshes[0].vertices[i].position != model2->meshes[0].vertices[i].position) {
            any_different = true;
            break;
        }
    }
    EXPECT_TRUE(any_different);
}

TEST(TreeGenerator, AABBIsComputed) {
    auto model = TreeGenerator::generate_oak(1);
    // Tree should extend upward from origin
    EXPECT_GT(model->max_y, 0.0f);
    EXPECT_NEAR(model->min_y, 0.0f, 1.0f);
    // Should have some width
    EXPECT_GT(model->max_x - model->min_x, 0.1f);
}

TEST(TreeGenerator, BoundingSphereIsComputed) {
    auto model = TreeGenerator::generate_oak(1);
    EXPECT_GT(model->bounding_half_diag, 0.0f);
}

TEST(TreeGenerator, NormalsAreUnitLength) {
    auto model = TreeGenerator::generate_oak(1);
    for (const auto& mesh : model->meshes) {
        for (const auto& v : mesh.vertices) {
            float len = glm::length(v.normal);
            EXPECT_NEAR(len, 1.0f, 0.01f);
        }
    }
}

TEST(TreeGenerator, OakPresetProducesTree) {
    auto model = TreeGenerator::generate_oak(42);
    ASSERT_NE(model, nullptr);
    EXPECT_GE(model->meshes.size(), 2u); // branches + leaves
}

TEST(TreeGenerator, PinePresetProducesTree) {
    auto model = TreeGenerator::generate_pine(42);
    ASSERT_NE(model, nullptr);
    EXPECT_GE(model->meshes.size(), 2u);
}

TEST(TreeGenerator, DeadPresetHasNoLeaves) {
    auto model = TreeGenerator::generate_dead(42);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->meshes.size(), 1u); // branches only
}

TEST(TreeGenerator, PresetsProduceDifferentShapes) {
    auto oak = TreeGenerator::generate_oak(42);
    auto pine = TreeGenerator::generate_pine(42);
    auto dead = TreeGenerator::generate_dead(42);
    // Different presets should produce different vertex counts
    EXPECT_NE(oak->meshes[0].vertices.size(), pine->meshes[0].vertices.size());
    EXPECT_NE(oak->meshes[0].vertices.size(), dead->meshes[0].vertices.size());
}

TEST(TreeGenerator, MoreLevelsProducesMoreGeometry) {
    TreeParams p;
    p.seed = 1;
    p.levels = 1;
    p.leaf_count = 0;
    auto simple = TreeGenerator::generate(p);
    p.levels = 3;
    auto complex = TreeGenerator::generate(p);
    EXPECT_GT(complex->meshes[0].vertices.size(), simple->meshes[0].vertices.size());
}

TEST(TreeGenerator, DoubleBillboardDoubleLeafQuads) {
    TreeParams p;
    p.seed = 1;
    p.levels = 1;
    p.leaf_count = 1;

    p.double_billboard = false;
    auto single = TreeGenerator::generate(p);

    p.double_billboard = true;
    auto doubled = TreeGenerator::generate(p);

    // Double billboard should have twice the leaf vertices
    ASSERT_GE(single->meshes.size(), 2u);
    ASSERT_GE(doubled->meshes.size(), 2u);
    EXPECT_EQ(doubled->meshes[1].vertices.size(), single->meshes[1].vertices.size() * 2);
}

TEST(TreeGenerator, TrunkOnlyWithZeroLevels) {
    TreeParams p;
    p.seed = 1;
    p.levels = 0;
    p.leaf_count = 1;
    auto model = TreeGenerator::generate(p);
    ASSERT_NE(model, nullptr);
    // Should still have geometry (trunk + leaves at level 0)
    EXPECT_GT(model->meshes[0].vertices.size(), 0u);
}

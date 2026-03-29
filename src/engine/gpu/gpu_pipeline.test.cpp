#include <gtest/gtest.h>
#include "engine/gpu/gpu_pipeline.hpp"

using namespace mmo::engine::gpu;

// =============================================================================
// PipelineConfig default state
// =============================================================================

TEST(PipelineConfigDefaults, InitialState) {
    PipelineConfig config;

    EXPECT_EQ(config.vertex_shader, nullptr);
    EXPECT_EQ(config.fragment_shader, nullptr);
    EXPECT_TRUE(config.vertex_buffers.empty());
    EXPECT_TRUE(config.vertex_attributes.empty());
    EXPECT_EQ(config.primitive_type, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST);
    EXPECT_EQ(config.fill_mode, SDL_GPU_FILLMODE_FILL);
    EXPECT_EQ(config.cull_mode, SDL_GPU_CULLMODE_BACK);
    EXPECT_EQ(config.front_face, SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE);
    EXPECT_TRUE(config.depth_test_enable);
    EXPECT_TRUE(config.depth_write_enable);
    EXPECT_EQ(config.depth_compare_op, SDL_GPU_COMPAREOP_LESS_OR_EQUAL);
    EXPECT_FALSE(config.stencil_test_enable);
    EXPECT_EQ(config.blend_mode, BlendMode::None);
    EXPECT_TRUE(config.has_depth_target);
    EXPECT_FALSE(config.enable_depth_bias);
    EXPECT_TRUE(config.enable_depth_clip);
    EXPECT_EQ(config.sample_count, SDL_GPU_SAMPLECOUNT_1);
}

// =============================================================================
// with_vertex3d()
// =============================================================================

TEST(PipelineConfigVertex, WithVertex3DSetsCorrectLayout) {
    PipelineConfig config;
    auto& ret = config.with_vertex3d();

    // Returns *this for chaining
    EXPECT_EQ(&ret, &config);

    // One vertex buffer with Vertex3D stride
    ASSERT_EQ(config.vertex_buffers.size(), 1u);
    EXPECT_EQ(config.vertex_buffers[0].pitch, sizeof(Vertex3D));
    EXPECT_EQ(config.vertex_buffers[0].slot, 0u);
    EXPECT_EQ(config.vertex_buffers[0].input_rate, SDL_GPU_VERTEXINPUTRATE_VERTEX);

    // 4 attributes: position, normal, texcoord, color
    EXPECT_EQ(config.vertex_attributes.size(), 4u);
}

TEST(PipelineConfigVertex, WithSkinnedVertexSetsCorrectLayout) {
    PipelineConfig config;
    config.with_skinned_vertex();

    ASSERT_EQ(config.vertex_buffers.size(), 1u);
    EXPECT_EQ(config.vertex_buffers[0].pitch, sizeof(SkinnedVertex));

    // 6 attributes: position, normal, texcoord, color, joints, weights
    EXPECT_EQ(config.vertex_attributes.size(), 6u);
}

TEST(PipelineConfigVertex, WithVertex2DSetsCorrectLayout) {
    PipelineConfig config;
    config.with_vertex2d();

    ASSERT_EQ(config.vertex_buffers.size(), 1u);
    EXPECT_EQ(config.vertex_buffers[0].pitch, sizeof(Vertex2D));

    // 3 attributes: position, texcoord, color
    EXPECT_EQ(config.vertex_attributes.size(), 3u);
}

// =============================================================================
// Blend / depth mode builders
// =============================================================================

TEST(PipelineConfigBlend, OpaqueSetsCorrectState) {
    PipelineConfig config;
    // Start from non-default state to verify opaque() actually sets values
    config.blend_mode = BlendMode::Alpha;
    config.depth_test_enable = false;
    config.depth_write_enable = false;

    config.opaque();

    EXPECT_EQ(config.blend_mode, BlendMode::None);
    EXPECT_TRUE(config.depth_test_enable);
    EXPECT_TRUE(config.depth_write_enable);
}

TEST(PipelineConfigBlend, AlphaBlendedSetsCorrectState) {
    PipelineConfig config;
    config.alpha_blended();

    EXPECT_EQ(config.blend_mode, BlendMode::Alpha);
    EXPECT_TRUE(config.depth_test_enable);
    // Transparent objects should not write depth
    EXPECT_FALSE(config.depth_write_enable);
}

TEST(PipelineConfigBlend, AdditiveSetsCorrectState) {
    PipelineConfig config;
    config.additive();

    EXPECT_EQ(config.blend_mode, BlendMode::Additive);
    EXPECT_TRUE(config.depth_test_enable);
    EXPECT_FALSE(config.depth_write_enable);
}

// =============================================================================
// Depth configuration
// =============================================================================

TEST(PipelineConfigDepth, NoDepthDisablesEverything) {
    PipelineConfig config;
    config.no_depth();

    EXPECT_FALSE(config.depth_test_enable);
    EXPECT_FALSE(config.depth_write_enable);
    EXPECT_FALSE(config.has_depth_target);
}

// =============================================================================
// Cull mode builders
// =============================================================================

TEST(PipelineConfigCull, NoCullDisablesCulling) {
    PipelineConfig config;
    config.no_cull();

    EXPECT_EQ(config.cull_mode, SDL_GPU_CULLMODE_NONE);
}

TEST(PipelineConfigCull, CullFrontSetsFrontCulling) {
    PipelineConfig config;
    config.cull_front();

    EXPECT_EQ(config.cull_mode, SDL_GPU_CULLMODE_FRONT);
}

// =============================================================================
// Depth bias
// =============================================================================

TEST(PipelineConfigDepthBias, WithDepthBiasSetsValues) {
    PipelineConfig config;
    config.with_depth_bias(1.5f, 2.0f, 0.5f);

    EXPECT_TRUE(config.enable_depth_bias);
    EXPECT_FLOAT_EQ(config.depth_bias_constant, 1.5f);
    EXPECT_FLOAT_EQ(config.depth_bias_slope, 2.0f);
    EXPECT_FLOAT_EQ(config.depth_bias_clamp, 0.5f);
}

TEST(PipelineConfigDepthBias, WithDepthBiasDefaultClampIsZero) {
    PipelineConfig config;
    config.with_depth_bias(1.0f, 2.0f);

    EXPECT_TRUE(config.enable_depth_bias);
    EXPECT_FLOAT_EQ(config.depth_bias_clamp, 0.0f);
}

// =============================================================================
// Fluent chaining
// =============================================================================

TEST(PipelineConfigChaining, MethodsCombineCorrectly) {
    PipelineConfig config;
    config.with_vertex3d()
          .alpha_blended()
          .no_cull()
          .with_depth_bias(2.0f, 1.0f);

    // All settings should be applied
    EXPECT_EQ(config.vertex_attributes.size(), 4u);
    EXPECT_EQ(config.blend_mode, BlendMode::Alpha);
    EXPECT_FALSE(config.depth_write_enable);
    EXPECT_EQ(config.cull_mode, SDL_GPU_CULLMODE_NONE);
    EXPECT_TRUE(config.enable_depth_bias);
    EXPECT_FLOAT_EQ(config.depth_bias_constant, 2.0f);
}

TEST(PipelineConfigChaining, LaterCallsOverrideEarlier) {
    PipelineConfig config;
    config.with_vertex3d().with_skinned_vertex();

    // skinned_vertex should have overwritten vertex3d
    EXPECT_EQ(config.vertex_attributes.size(), 6u);
    EXPECT_EQ(config.vertex_buffers[0].pitch, sizeof(SkinnedVertex));
}

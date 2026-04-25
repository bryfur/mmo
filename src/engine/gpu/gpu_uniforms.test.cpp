#include <gtest/gtest.h>
#include "engine/gpu/gpu_uniforms.hpp"
#include <cstddef>

using namespace mmo::engine::gpu;

// =============================================================================
// Alignment tests - all uniform structs must be 16-byte aligned for GPU cbuffers
// =============================================================================

TEST(UniformAlignment, AllStructsAre16ByteAligned) {
    EXPECT_EQ(alignof(ModelTransformUniforms), 16u);
    EXPECT_EQ(alignof(ModelLightingUniforms), 16u);
    EXPECT_EQ(alignof(SkyboxFragmentUniforms), 16u);
    EXPECT_EQ(alignof(GridVertexUniforms), 16u);
    EXPECT_EQ(alignof(UIScreenUniforms), 16u);
    EXPECT_EQ(alignof(UIFragmentUniforms), 16u);
    EXPECT_EQ(alignof(InstancedCameraUniforms), 16u);
    EXPECT_EQ(alignof(InstancedLightingUniforms), 16u);
    EXPECT_EQ(alignof(InstancedShadowUniforms), 16u);
    EXPECT_EQ(alignof(ShadowTransformUniforms), 16u);
    EXPECT_EQ(alignof(ShadowTerrainUniforms), 16u);
    EXPECT_EQ(alignof(ShadowDataUniforms), 16u);
    EXPECT_EQ(alignof(GTAOUniforms), 16u);
    EXPECT_EQ(alignof(BlurUniforms), 16u);
    EXPECT_EQ(alignof(CompositeUniforms), 16u);
    EXPECT_EQ(alignof(BloomDownsampleUniforms), 16u);
    EXPECT_EQ(alignof(BloomUpsampleUniforms), 16u);
    EXPECT_EQ(alignof(VolumetricFogUniforms), 16u);
}

// =============================================================================
// Size tests - sizes must be multiples of 16 and match shader cbuffer layout
// =============================================================================

TEST(UniformSizes, SizesAreMultiplesOf16) {
    // GPU uniform buffers require 16-byte aligned sizes
    EXPECT_EQ(sizeof(ModelTransformUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(ModelLightingUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(SkyboxFragmentUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(GridVertexUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(UIScreenUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(UIFragmentUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(InstancedCameraUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(InstancedLightingUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(InstancedShadowUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(ShadowTransformUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(ShadowTerrainUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(ShadowDataUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(GTAOUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(BlurUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(CompositeUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(BloomDownsampleUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(BloomUpsampleUniforms) % 16, 0u);
    EXPECT_EQ(sizeof(VolumetricFogUniforms) % 16, 0u);
}

// =============================================================================
// ModelTransformUniforms - matches model.vert.hlsl TransformUniforms
// =============================================================================

TEST(ModelTransformUniformsLayout, SizeIs288Bytes) {
    // 3 mat4 (192) + vec3+pad (16) + mat4 (64) + int+pad[3] (16) = 288
    EXPECT_EQ(sizeof(ModelTransformUniforms), 288u);
}

TEST(ModelTransformUniformsLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(ModelTransformUniforms, model), 0u);
    EXPECT_EQ(offsetof(ModelTransformUniforms, view), 64u);
    EXPECT_EQ(offsetof(ModelTransformUniforms, projection), 128u);
    EXPECT_EQ(offsetof(ModelTransformUniforms, cameraPos), 192u);
    EXPECT_EQ(offsetof(ModelTransformUniforms, normalMatrix), 208u);
    EXPECT_EQ(offsetof(ModelTransformUniforms, useSkinning), 272u);
}

// =============================================================================
// ModelLightingUniforms - matches model.frag.hlsl LightingUniforms
// =============================================================================

TEST(ModelLightingUniformsLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(ModelLightingUniforms, lightDir), 0u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, lightColor), 16u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, ambientColor), 32u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, tintColor), 48u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, fogColor), 64u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, fogStart), 76u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, fogEnd), 80u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, hasTexture), 84u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, fogEnabled), 88u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, hasNormalMap), 92u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, cameraPos), 96u);
    EXPECT_EQ(offsetof(ModelLightingUniforms, normalScale), 108u);
}

// =============================================================================
// CompositeUniforms - matches composite.frag.hlsl
// =============================================================================

TEST(CompositeUniformsLayout, SizeIs32Bytes) {
    // 8 floats = 32 bytes (2 16-byte blocks: ao/bloom/fog + exposure/tonemap/contrast/saturation)
    EXPECT_EQ(sizeof(CompositeUniforms), 32u);
}

TEST(CompositeUniformsLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(CompositeUniforms, aoStrength), 0u);
    EXPECT_EQ(offsetof(CompositeUniforms, bloomStrength), 4u);
    EXPECT_EQ(offsetof(CompositeUniforms, volumetricFogEnabled), 8u);
    EXPECT_EQ(offsetof(CompositeUniforms, exposure), 16u);
    EXPECT_EQ(offsetof(CompositeUniforms, tonemapMode), 20u);
    EXPECT_EQ(offsetof(CompositeUniforms, contrast), 24u);
    EXPECT_EQ(offsetof(CompositeUniforms, saturation), 28u);
}

TEST(CompositeUniformsLayout, DefaultValues) {
    CompositeUniforms u;
    EXPECT_FLOAT_EQ(u.aoStrength, 1.0f);
    EXPECT_FLOAT_EQ(u.bloomStrength, 0.35f);
    EXPECT_FLOAT_EQ(u.volumetricFogEnabled, 0.0f);
    EXPECT_FLOAT_EQ(u.exposure, 1.0f);
    EXPECT_EQ(u.tonemapMode, 0);
    EXPECT_FLOAT_EQ(u.contrast, 1.0f);
    EXPECT_FLOAT_EQ(u.saturation, 1.0f);
}

// =============================================================================
// VolumetricFogUniforms - matches volumetric_fog.frag.hlsl
// =============================================================================

TEST(VolumetricFogUniformsLayout, SizeIs208Bytes) {
    // 2 mat4 (128) + 4 * vec3+float (64) + 4 floats (16) = 208
    EXPECT_EQ(sizeof(VolumetricFogUniforms), 208u);
}

TEST(VolumetricFogUniformsLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(VolumetricFogUniforms, invViewProjection), 0u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, shadowLightViewProjection), 64u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, lightDir), 128u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, fogDensity), 140u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, lightColor), 144u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, scatterStrength), 156u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, fogColor), 160u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, fogHeight), 172u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, cameraPos), 176u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, fogFalloff), 188u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, nearPlane), 192u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, farPlane), 196u);
    EXPECT_EQ(offsetof(VolumetricFogUniforms, godRaysEnabled), 200u);
}

TEST(VolumetricFogUniformsLayout, DefaultValues) {
    VolumetricFogUniforms u;
    EXPECT_FLOAT_EQ(u.fogDensity, 0.02f);
    EXPECT_FLOAT_EQ(u.scatterStrength, 0.3f);
    EXPECT_FLOAT_EQ(u.fogHeight, 50.0f);
    EXPECT_FLOAT_EQ(u.fogFalloff, 0.01f);
    EXPECT_FLOAT_EQ(u.nearPlane, 0.1f);
    EXPECT_FLOAT_EQ(u.farPlane, 5000.0f);
    EXPECT_FLOAT_EQ(u.godRaysEnabled, 1.0f);
}

// =============================================================================
// ShadowDataUniforms - cascade shadow maps
// =============================================================================

TEST(ShadowDataUniformsLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(ShadowDataUniforms, lightViewProjection), 0u);
    // 4 mat4 = 256 bytes
    EXPECT_EQ(offsetof(ShadowDataUniforms, cascadeSplits), 256u);
    EXPECT_EQ(offsetof(ShadowDataUniforms, shadowMapResolution), 272u);
    EXPECT_EQ(offsetof(ShadowDataUniforms, lightSize), 276u);
    EXPECT_EQ(offsetof(ShadowDataUniforms, shadowEnabled), 280u);
}

// =============================================================================
// GTAOUniforms
// =============================================================================

TEST(GTAOUniformsLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(GTAOUniforms, projection), 0u);
    EXPECT_EQ(offsetof(GTAOUniforms, invProjection), 64u);
    EXPECT_EQ(offsetof(GTAOUniforms, screenSize), 128u);
    EXPECT_EQ(offsetof(GTAOUniforms, invScreenSize), 136u);
    EXPECT_EQ(offsetof(GTAOUniforms, radius), 144u);
    EXPECT_EQ(offsetof(GTAOUniforms, bias), 148u);
    EXPECT_EQ(offsetof(GTAOUniforms, numDirections), 152u);
    EXPECT_EQ(offsetof(GTAOUniforms, numSteps), 156u);
}

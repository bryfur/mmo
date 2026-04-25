#include "engine/gpu/gpu_types.hpp"
#include <cstddef>
#include <gtest/gtest.h>

using namespace mmo::engine::gpu;

// =============================================================================
// Vertex3D layout tests
// =============================================================================

TEST(Vertex3DLayout, SizeIs64Bytes) {
    // pos(12) + normal(12) + uv(8) + color(16) + tangent(16) = 64
    EXPECT_EQ(sizeof(Vertex3D), 64u);
}

TEST(Vertex3DLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(Vertex3D, position), 0u);
    EXPECT_EQ(offsetof(Vertex3D, normal), 12u);   // after vec3 (12 bytes)
    EXPECT_EQ(offsetof(Vertex3D, texcoord), 24u); // after 2x vec3 (24 bytes)
    EXPECT_EQ(offsetof(Vertex3D, color), 32u);    // after vec3+vec3+vec2 (32 bytes)
    EXPECT_EQ(offsetof(Vertex3D, tangent), 48u);  // after vec3+vec3+vec2+vec4 (48 bytes)
}

// =============================================================================
// SkinnedVertex layout tests
// =============================================================================

TEST(SkinnedVertexLayout, SizeIsCorrect) {
    // Vertex3D (64 with tangent) + uint8_t[4] (4) + float[4] (16) = 84
    EXPECT_EQ(sizeof(SkinnedVertex), 84u);
}

TEST(SkinnedVertexLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(SkinnedVertex, position), 0u);
    EXPECT_EQ(offsetof(SkinnedVertex, normal), 12u);
    EXPECT_EQ(offsetof(SkinnedVertex, texcoord), 24u);
    EXPECT_EQ(offsetof(SkinnedVertex, color), 32u);
    EXPECT_EQ(offsetof(SkinnedVertex, tangent), 48u);
    EXPECT_EQ(offsetof(SkinnedVertex, joints), 64u);
    EXPECT_EQ(offsetof(SkinnedVertex, weights), 68u);
}

// =============================================================================
// Vertex2D layout tests
// =============================================================================

TEST(Vertex2DLayout, SizeIs32Bytes) {
    // 2 floats (position) + 2 floats (texcoord) + 4 floats (color) = 8 * 4
    EXPECT_EQ(sizeof(Vertex2D), 32u);
}

TEST(Vertex2DLayout, FieldOffsets) {
    EXPECT_EQ(offsetof(Vertex2D, position), 0u);
    EXPECT_EQ(offsetof(Vertex2D, texcoord), 8u);
    EXPECT_EQ(offsetof(Vertex2D, color), 16u);
}

// =============================================================================
// Vertex attribute descriptions match struct layout
// =============================================================================

TEST(VertexAttributes, Vertex3DAttributesMatchLayout) {
    auto attrs = get_vertex3d_attributes();
    ASSERT_EQ(attrs.size(), 5u);

    // position: location 0, float3, offset 0
    EXPECT_EQ(attrs[0].location, 0u);
    EXPECT_EQ(attrs[0].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3);
    EXPECT_EQ(attrs[0].offset, offsetof(Vertex3D, position));

    // normal: location 1, float3, offset 12
    EXPECT_EQ(attrs[1].location, 1u);
    EXPECT_EQ(attrs[1].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3);
    EXPECT_EQ(attrs[1].offset, offsetof(Vertex3D, normal));

    // texcoord: location 2, float2, offset 24
    EXPECT_EQ(attrs[2].location, 2u);
    EXPECT_EQ(attrs[2].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2);
    EXPECT_EQ(attrs[2].offset, offsetof(Vertex3D, texcoord));

    // color: location 3, float4, offset 32
    EXPECT_EQ(attrs[3].location, 3u);
    EXPECT_EQ(attrs[3].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4);
    EXPECT_EQ(attrs[3].offset, offsetof(Vertex3D, color));

    // tangent: location 4, float4, offset 48
    EXPECT_EQ(attrs[4].location, 4u);
    EXPECT_EQ(attrs[4].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4);
    EXPECT_EQ(attrs[4].offset, offsetof(Vertex3D, tangent));
}

TEST(VertexAttributes, Vertex3DBufferDescMatchesStride) {
    auto desc = get_vertex3d_buffer_desc();
    EXPECT_EQ(desc.pitch, sizeof(Vertex3D));
    EXPECT_EQ(desc.slot, 0u);
    EXPECT_EQ(desc.input_rate, SDL_GPU_VERTEXINPUTRATE_VERTEX);
}

TEST(VertexAttributes, SkinnedVertexAttributesMatchLayout) {
    auto attrs = get_skinned_vertex_attributes();
    ASSERT_EQ(attrs.size(), 7u);

    // First 4 match Vertex3D
    EXPECT_EQ(attrs[0].offset, offsetof(SkinnedVertex, position));
    EXPECT_EQ(attrs[1].offset, offsetof(SkinnedVertex, normal));
    EXPECT_EQ(attrs[2].offset, offsetof(SkinnedVertex, texcoord));
    EXPECT_EQ(attrs[3].offset, offsetof(SkinnedVertex, color));

    // tangent: location 4, float4
    EXPECT_EQ(attrs[4].location, 4u);
    EXPECT_EQ(attrs[4].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4);
    EXPECT_EQ(attrs[4].offset, offsetof(SkinnedVertex, tangent));

    // joints: location 5, ubyte4
    EXPECT_EQ(attrs[5].location, 5u);
    EXPECT_EQ(attrs[5].format, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4);
    EXPECT_EQ(attrs[5].offset, offsetof(SkinnedVertex, joints));

    // weights: location 6, float4
    EXPECT_EQ(attrs[6].location, 6u);
    EXPECT_EQ(attrs[6].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4);
    EXPECT_EQ(attrs[6].offset, offsetof(SkinnedVertex, weights));
}

TEST(VertexAttributes, SkinnedVertexBufferDescMatchesStride) {
    auto desc = get_skinned_vertex_buffer_desc();
    EXPECT_EQ(desc.pitch, sizeof(SkinnedVertex));
    EXPECT_EQ(desc.slot, 0u);
}

TEST(VertexAttributes, Vertex2DAttributesMatchLayout) {
    auto attrs = get_vertex2d_attributes();
    ASSERT_EQ(attrs.size(), 3u);

    EXPECT_EQ(attrs[0].offset, offsetof(Vertex2D, position));
    EXPECT_EQ(attrs[0].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2);

    EXPECT_EQ(attrs[1].offset, offsetof(Vertex2D, texcoord));
    EXPECT_EQ(attrs[1].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2);

    EXPECT_EQ(attrs[2].offset, offsetof(Vertex2D, color));
    EXPECT_EQ(attrs[2].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4);
}

TEST(VertexAttributes, AllAttributesBoundToSlot0) {
    for (const auto& attr : get_vertex3d_attributes()) {
        EXPECT_EQ(attr.buffer_slot, 0u);
    }
    for (const auto& attr : get_skinned_vertex_attributes()) {
        EXPECT_EQ(attr.buffer_slot, 0u);
    }
    for (const auto& attr : get_vertex2d_attributes()) {
        EXPECT_EQ(attr.buffer_slot, 0u);
    }
}

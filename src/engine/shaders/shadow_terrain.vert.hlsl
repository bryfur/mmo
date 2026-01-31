/**
 * Shadow Depth Vertex Shader - Terrain
 *
 * Transforms terrain vertices into light space for shadow map generation.
 * Terrain vertices are already in world space (no model matrix).
 */

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
};

[[vk::binding(0, 1)]]
cbuffer ShadowTerrainUniforms {
    float4x4 lightViewProjection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(lightViewProjection, float4(input.position, 1.0));
    return output;
}

/**
 * Shadow Depth Vertex Shader - Static Models
 *
 * Transforms vertices into light space for shadow map generation.
 * Depth-only pass: no color output, no lighting.
 */

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
};

[[vk::binding(0, 1)]]
cbuffer ShadowTransformUniforms {
    float4x4 lightViewProjection;
    float4x4 model;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(lightViewProjection, worldPos);
    return output;
}

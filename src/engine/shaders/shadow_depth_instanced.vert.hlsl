/**
 * Instanced Shadow Depth Vertex Shader - Static Models
 *
 * Transforms vertices into light space for shadow map generation.
 * Per-instance model matrices read from a storage buffer.
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
cbuffer ShadowUniforms {
    float4x4 lightViewProjection;
};

struct ShadowInstanceData {
    float4x4 model;
};

[[vk::binding(0, 0)]]
StructuredBuffer<ShadowInstanceData> instances;

VSOutput VSMain(VSInput input, uint instanceID : SV_InstanceID) {
    VSOutput output;
    float4 worldPos = mul(instances[instanceID].model, float4(input.position, 1.0));
    output.position = mul(lightViewProjection, worldPos);
    return output;
}

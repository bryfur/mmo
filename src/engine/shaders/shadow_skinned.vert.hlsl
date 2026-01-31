/**
 * Shadow Depth Vertex Shader - Skinned Models
 *
 * Transforms skinned vertices into light space for shadow map generation.
 */

#define MAX_BONES 64

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
    [[vk::location(4)]] int4 joints : BLENDINDICES;
    [[vk::location(5)]] float4 weights : BLENDWEIGHT;
};

struct VSOutput {
    float4 position : SV_Position;
};

[[vk::binding(0, 1)]]
cbuffer ShadowTransformUniforms {
    float4x4 lightViewProjection;
    float4x4 model;
};

[[vk::binding(1, 1)]]
cbuffer BoneUniforms {
    float4x4 boneMatrices[MAX_BONES];
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    // Apply skeletal animation
    float4x4 skinMatrix =
        input.weights.x * boneMatrices[input.joints.x] +
        input.weights.y * boneMatrices[input.joints.y] +
        input.weights.z * boneMatrices[input.joints.z] +
        input.weights.w * boneMatrices[input.joints.w];

    float4 localPos = mul(skinMatrix, float4(input.position, 1.0));
    float4 worldPos = mul(model, localPos);
    output.position = mul(lightViewProjection, worldPos);
    return output;
}

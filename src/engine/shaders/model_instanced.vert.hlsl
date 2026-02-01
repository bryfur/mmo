/**
 * Instanced Model Vertex Shader - SDL3 GPU API
 *
 * Renders multiple instances of the same model in a single draw call.
 * Per-instance transforms are read from a storage buffer indexed by SV_InstanceID.
 */

// Vertex input - locations match get_vertex3d_attributes() in gpu_types.hpp
struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
    float viewDepth : TEXCOORD5;
    float4 instanceTint : TEXCOORD6;
    float noFog : TEXCOORD7;
};

// Shared camera uniforms (pushed once per frame)
[[vk::binding(0, 1)]]
cbuffer CameraUniforms {
    float4x4 view;
    float4x4 projection;
    float3 cameraPos;
    float _padding0;
};

// Per-instance data in storage buffer
struct InstanceData {
    float4x4 model;
    float4x4 normalMatrix;
    float4 tint;
    float noFog;
    float _pad0;
    float _pad1;
    float _pad2;
};

[[vk::binding(0, 0)]]
StructuredBuffer<InstanceData> instances;

VSOutput VSMain(VSInput input, uint instanceID : SV_InstanceID) {
    VSOutput output;

    InstanceData inst = instances[instanceID];

    float4 worldPos = mul(inst.model, float4(input.position, 1.0));
    output.fragPos = worldPos.xyz;

    float3x3 normalMat3x3 = (float3x3)inst.normalMatrix;
    output.normal = normalize(mul(normalMat3x3, input.normal));

    output.texCoord = input.texCoord;
    output.vertexColor = input.color;
    output.instanceTint = inst.tint;
    output.noFog = inst.noFog;
    output.fogDistance = length(worldPos.xyz - cameraPos);

    float4 viewPos = mul(view, worldPos);
    output.viewDepth = -viewPos.z;
    output.position = mul(projection, viewPos);

    return output;
}

/**
 * Terrain Vertex Shader - SDL3 GPU API
 *
 * Transforms terrain vertices with fog support.
 */

// Vertex input - locations match create_terrain_pipeline() in pipeline_registry.cpp
struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
    [[vk::location(3)]] float3 normal : NORMAL;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 vertexColor : TEXCOORD1;
    float3 fragPos : TEXCOORD2;
    float fogDistance : TEXCOORD3;
    float3 normal : TEXCOORD4;
    float viewDepth : TEXCOORD5;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer TransformUniforms {
    float4x4 view;
    float4x4 projection;
    float3 cameraPos;
    float _padding0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    output.fragPos = input.position;
    output.texCoord = input.texCoord;
    output.vertexColor = input.color;
    output.fogDistance = length(input.position - cameraPos);

    // Use vertex normal calculated from heightmap
    output.normal = normalize(input.normal);

    float4 viewPos = mul(view, float4(input.position, 1.0));
    output.viewDepth = -viewPos.z;
    output.position = mul(projection, viewPos);

    return output;
}

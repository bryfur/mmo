/**
 * Model Vertex Shader - SDL3 GPU API
 *
 * Transforms 3D model vertices with lighting support.
 * Outputs world position, normal, texture coordinates, and fog data.
 */

// Vertex input - locations match get_vertex3d_attributes() in gpu_types.hpp
struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
    float viewDepth : TEXCOORD5;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer TransformUniforms {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float3 cameraPos;
    float _padding0;
    float4x4 normalMatrix;  // Pre-computed inverse-transpose of model matrix (CPU-side)
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.fragPos = worldPos.xyz;

    // Transform normal to world space using pre-computed normal matrix
    // normalMatrix should be inverse-transpose of model, computed on CPU
    float3x3 normalMat3x3 = (float3x3)normalMatrix;
    output.normal = normalize(mul(normalMat3x3, input.normal));

    output.texCoord = input.texCoord;
    output.vertexColor = input.color;
    output.fogDistance = length(worldPos.xyz - cameraPos);

    float4 viewPos = mul(view, worldPos);
    output.viewDepth = -viewPos.z;
    output.position = mul(projection, viewPos);

    return output;
}

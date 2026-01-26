/**
 * Model Vertex Shader - SDL3 GPU API
 * 
 * Transforms 3D model vertices with lighting support.
 * Outputs world position, normal, texture coordinates, and fog/shadow data.
 */

// Vertex input
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR0;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
    float4 fragPosLightSpace : TEXCOORD5;
};

// Uniform buffer slot 0 - Camera and transform data
cbuffer TransformUniforms : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float3 cameraPos;
    float _padding0;
    float4x4 lightSpaceMatrix;
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
    output.fragPosLightSpace = mul(lightSpaceMatrix, worldPos);
    
    output.position = mul(projection, mul(view, worldPos));
    
    return output;
}

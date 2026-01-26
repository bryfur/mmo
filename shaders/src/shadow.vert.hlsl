/**
 * Shadow Depth Vertex Shader - SDL3 GPU API
 * 
 * Renders geometry to shadow map (depth pass only).
 */

// Vertex input
struct VSInput {
    float3 position : POSITION;
};

// Vertex output
struct VSOutput {
    float4 position : SV_Position;
};

// Uniform buffer slot 0 - Light space transform
cbuffer ShadowUniforms : register(b0) {
    float4x4 lightSpaceMatrix;
    float4x4 model;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    output.position = mul(lightSpaceMatrix, mul(model, float4(input.position, 1.0)));
    
    return output;
}

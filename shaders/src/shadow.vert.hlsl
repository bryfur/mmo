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

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer ShadowUniforms {
    float4x4 lightSpaceMatrix;
    float4x4 model;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    output.position = mul(lightSpaceMatrix, mul(model, float4(input.position, 1.0)));
    
    return output;
}

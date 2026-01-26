/**
 * UI Vertex Shader - SDL3 GPU API
 * 
 * Simple 2D vertex transformation for UI elements.
 */

// Vertex input
struct VSInput {
    float2 position : POSITION;
    float4 color : COLOR0;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float4 vertexColor : TEXCOORD0;
};

// Uniform buffer slot 0 - Projection
cbuffer UIUniforms : register(b0) {
    float4x4 projection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    output.vertexColor = input.color;
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    
    return output;
}

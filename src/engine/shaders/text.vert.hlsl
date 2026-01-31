/**
 * Text Vertex Shader - SDL3 GPU API
 * 
 * Simple 2D text rendering with font atlas.
 */

// Vertex input
struct VSInput {
    float2 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer TextUniforms {
    float4x4 projection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    output.texCoord = input.texCoord;
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    
    return output;
}

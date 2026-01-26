/**
 * SSAO (Screen Space Ambient Occlusion) Vertex Shader - SDL3 GPU API
 * 
 * Full-screen quad for post-processing passes.
 */

// Vertex input
struct VSInput {
    float2 position : POSITION;
    float2 texCoords : TEXCOORD0;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float2 texCoords : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    output.texCoords = input.texCoords;
    output.position = float4(input.position, 0.0, 1.0);
    
    return output;
}

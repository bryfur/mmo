/**
 * Skybox Vertex Shader - SDL3 GPU API
 * 
 * Renders procedural skybox with the skybox always centered on camera.
 */

// Vertex input
struct VSInput {
    float3 position : POSITION;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer CameraUniforms {
    float4x4 view;
    float4x4 projection;
    float3 cameraPos;
    float _padding0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Position the skybox centered on camera
    output.worldPos = input.position;
    
    // Remove translation from view matrix (skybox stays at camera position)
    float4x4 viewNoTranslation = view;
    viewNoTranslation[3][0] = 0.0;
    viewNoTranslation[3][1] = 0.0;
    viewNoTranslation[3][2] = 0.0;
    
    float4 pos = mul(projection, mul(viewNoTranslation, float4(input.position, 1.0)));
    // Set z to w for maximum depth (skybox always in background)
    output.position = float4(pos.xy, pos.w, pos.w);
    
    return output;
}

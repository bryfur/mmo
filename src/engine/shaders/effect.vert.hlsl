/**
 * Effect/Particle Vertex Shader - SDL3 GPU API
 * 
 * Billboard particles for effects (fire, magic, etc).
 * Uses a single input struct with per-vertex and per-instance attributes
 * separated by slot indices in the pipeline configuration.
 */

// Combined vertex input with per-vertex and per-instance attributes
// Per-vertex data comes from slot 0, per-instance from slot 1
// Note: Uses Vertex3D layout but only reads xy of position
struct VSInput {
    // Per-vertex attributes (slot 0) - locations match Vertex3D
    [[vk::location(0)]] float2 position : POSITION;   // -1 to 1 quad coordinates (reads xy of float3)
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    // Per-instance attributes (slot 1, input rate = instance)
    [[vk::location(4)]] float3 worldPos : TEXCOORD1;
    [[vk::location(5)]] float size : TEXCOORD2;
    [[vk::location(6)]] float4 color : TEXCOORD3;
    [[vk::location(7)]] float rotation : TEXCOORD4;
    [[vk::location(8)]] float life : TEXCOORD5;  // 0-1 normalized
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 color : TEXCOORD1;
    float life : TEXCOORD2;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer EffectUniforms {
    float4x4 viewProjection;
    float3 cameraRight;
    float _padding0;
    float3 cameraUp;
    float _padding1;
};

// 2D rotation matrix - using explicit assignment for HLSL compatibility
float2x2 rotate2D(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    float2x2 rotation;
    rotation[0] = float2(c, -s);
    rotation[1] = float2(s, c);
    return rotation;
}

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Apply rotation to quad position
    float2 rotatedPos = mul(rotate2D(input.rotation), input.position);
    
    // Billboard position: expand quad in camera space
    float3 worldPos = input.worldPos 
                    + cameraRight * rotatedPos.x * input.size
                    + cameraUp * rotatedPos.y * input.size;
    
    output.position = mul(viewProjection, float4(worldPos, 1.0));
    output.texCoord = input.texCoord;
    output.color = input.color;
    output.life = input.life;
    
    return output;
}

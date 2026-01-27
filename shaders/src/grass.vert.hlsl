/**
 * Grass Vertex Shader - SDL3 GPU API
 * 
 * Instanced grass rendering with wind animation.
 * Uses a single input struct with per-vertex and per-instance attributes
 * separated by slot indices in the pipeline configuration.
 */

// Combined vertex input with per-vertex and per-instance attributes
// Per-vertex data comes from slot 0, per-instance from slot 1
struct VSInput {
    // Per-vertex attributes (slot 0)
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    // Per-instance attributes (slot 1, input rate = instance)
    float3 instancePos : TEXCOORD1;
    float instanceRotation : TEXCOORD2;
    float instanceScale : TEXCOORD3;
    float instanceColorVariation : TEXCOORD4;  // Matches GrassInstance in gpu_types.hpp
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
    float colorVariation : TEXCOORD3;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer GrassUniforms {
    float4x4 viewProjection;
    float time;
    float windStrength;
    float2 windDirection;
    float3 cameraPos;
    float _padding0;
};

// Rotation matrix around Y axis - using explicit row assignment for HLSL compatibility
float3x3 rotateY(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    float3x3 rotation;
    rotation[0] = float3(c, 0.0, s);
    rotation[1] = float3(0.0, 1.0, 0.0);
    rotation[2] = float3(-s, 0.0, c);
    return rotation;
}

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Rotate the grass blade
    float3 rotatedPos = mul(rotateY(input.instanceRotation), input.position);
    float3 rotatedNormal = mul(rotateY(input.instanceRotation), input.normal);
    
    // Apply scale
    rotatedPos *= input.instanceScale;
    
    // Apply wind effect (stronger at top of blade)
    float windFactor = input.position.y; // Top of blade moves more
    float windTime = time * 2.0 + input.instancePos.x * 0.1 + input.instancePos.z * 0.1;
    float2 windOffset = windDirection * windStrength * windFactor * sin(windTime);
    
    // Final world position
    float3 worldPos = input.instancePos + rotatedPos + float3(windOffset.x, 0.0, windOffset.y);
    
    output.worldPos = worldPos;
    output.normal = rotatedNormal;
    output.texCoord = input.texCoord;
    output.colorVariation = input.instanceColorVariation;
    output.position = mul(viewProjection, float4(worldPos, 1.0));
    
    return output;
}

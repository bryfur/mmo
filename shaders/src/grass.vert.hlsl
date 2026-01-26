/**
 * Grass Vertex Shader - SDL3 GPU API
 * 
 * Instanced grass rendering with wind animation.
 */

// Per-vertex input (grass blade mesh)
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
};

// Per-instance input
struct InstanceInput {
    float3 instancePos : TEXCOORD1;
    float instanceRotation : TEXCOORD2;
    float instanceScale : TEXCOORD3;
    float instanceTilt : TEXCOORD4;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
};

// Uniform buffer slot 0 - Transform and wind data
cbuffer GrassUniforms : register(b0) {
    float4x4 viewProjection;
    float time;
    float windStrength;
    float2 windDirection;
    float3 cameraPos;
    float _padding0;
};

// Rotation matrix around Y axis
float3x3 rotateY(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float3x3(
        c, 0.0, s,
        0.0, 1.0, 0.0,
        -s, 0.0, c
    );
}

VSOutput VSMain(VSInput input, InstanceInput instance) {
    VSOutput output;
    
    // Rotate the grass blade
    float3 rotatedPos = mul(rotateY(instance.instanceRotation), input.position);
    float3 rotatedNormal = mul(rotateY(instance.instanceRotation), input.normal);
    
    // Apply scale
    rotatedPos *= instance.instanceScale;
    
    // Apply wind effect (stronger at top of blade)
    float windFactor = input.position.y; // Top of blade moves more
    float windTime = time * 2.0 + instance.instancePos.x * 0.1 + instance.instancePos.z * 0.1;
    float2 windOffset = windDirection * windStrength * windFactor * sin(windTime);
    
    // Apply tilt
    float tiltAngle = instance.instanceTilt;
    float3 tiltedPos = rotatedPos;
    tiltedPos.x += rotatedPos.y * sin(tiltAngle);
    
    // Final world position
    float3 worldPos = instance.instancePos + tiltedPos + float3(windOffset.x, 0.0, windOffset.y);
    
    output.worldPos = worldPos;
    output.normal = rotatedNormal;
    output.texCoord = input.texCoord;
    output.position = mul(viewProjection, float4(worldPos, 1.0));
    
    return output;
}

/**
 * Effect/Particle Vertex Shader - SDL3 GPU API
 * 
 * Billboard particles for effects (fire, magic, etc).
 */

// Vertex input (quad corners)
struct VSInput {
    float2 position : POSITION;   // -1 to 1 quad coordinates
    float2 texCoord : TEXCOORD0;
};

// Per-instance particle data
struct ParticleInput {
    float3 worldPos : TEXCOORD1;
    float size : TEXCOORD2;
    float4 color : TEXCOORD3;
    float rotation : TEXCOORD4;
    float life : TEXCOORD5;  // 0-1 normalized
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 color : TEXCOORD1;
    float life : TEXCOORD2;
};

// Uniform buffer slot 0 - Camera data
cbuffer EffectUniforms : register(b0) {
    float4x4 viewProjection;
    float3 cameraRight;
    float _padding0;
    float3 cameraUp;
    float _padding1;
};

// 2D rotation matrix
float2x2 rotate2D(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float2x2(c, -s, s, c);
}

VSOutput VSMain(VSInput input, ParticleInput particle) {
    VSOutput output;
    
    // Apply rotation to quad position
    float2 rotatedPos = mul(rotate2D(particle.rotation), input.position);
    
    // Billboard position: expand quad in camera space
    float3 worldPos = particle.worldPos 
                    + cameraRight * rotatedPos.x * particle.size
                    + cameraUp * rotatedPos.y * particle.size;
    
    output.position = mul(viewProjection, float4(worldPos, 1.0));
    output.texCoord = input.texCoord;
    output.color = particle.color;
    output.life = particle.life;
    
    return output;
}

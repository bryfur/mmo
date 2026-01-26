/**
 * Grass Fragment Shader - SDL3 GPU API
 * 
 * Renders grass blades with alpha testing and simple lighting.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
    float colorVariation : TEXCOORD3;
};

// Uniform buffer slot 0 - Lighting
cbuffer GrassLightingUniforms : register(b0) {
    float3 lightDir;
    float _padding0;
    float3 lightColor;
    float _padding1;
    float3 ambientColor;
    float alphaThreshold;
};

// Texture and sampler bindings
Texture2D grassTexture : register(t0);
SamplerState grassSampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    // Sample grass texture
    float4 texColor = grassTexture.Sample(grassSampler, input.texCoord);
    
    // Alpha test (discard transparent pixels)
    if (texColor.a < alphaThreshold) {
        discard;
    }
    
    // Simple diffuse lighting
    float3 norm = normalize(input.normal);
    float3 lightDirection = normalize(-lightDir);
    float diff = max(dot(norm, lightDirection), 0.0);
    
    float3 lighting = ambientColor + diff * lightColor;
    
    // Apply per-instance color variation
    float3 color = texColor.rgb * lighting * (1.0 + input.colorVariation * 0.2);
    
    return float4(color, texColor.a);
}

/**
 * Effect/Particle Fragment Shader - SDL3 GPU API
 * 
 * Renders particles with texture and per-particle color/alpha.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 color : TEXCOORD1;
    float life : TEXCOORD2;
};

// Texture and sampler bindings
Texture2D effectTexture : register(t0);
SamplerState effectSampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    // Sample effect texture
    float4 texColor = effectTexture.Sample(effectSampler, input.texCoord);
    
    // Apply particle color and alpha
    float4 finalColor = texColor * input.color;
    
    // Optional: fade based on lifetime
    // finalColor.a *= input.life;
    
    return finalColor;
}

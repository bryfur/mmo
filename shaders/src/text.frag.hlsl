/**
 * Text Fragment Shader - SDL3 GPU API
 * 
 * Renders text from font atlas with color tinting.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

// Uniform buffer slot 0 - Text color
cbuffer TextColorUniforms : register(b0) {
    float4 textColor;
};

// Texture and sampler bindings
Texture2D textTexture : register(t0);
SamplerState textSampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    float4 sampled = textTexture.Sample(textSampler, input.texCoord);
    return textColor * sampled;
}

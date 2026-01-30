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

// Uniform buffer - SDL3 GPU SPIR-V requires fragment uniforms in set 3
[[vk::binding(0, 3)]]
cbuffer TextColorUniforms {
    float4 textColor;
};

// Texture and sampler - SDL3 GPU SPIR-V requires fragment textures in set 2
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D textTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState textSampler;

float4 PSMain(PSInput input) : SV_Target {
    float4 sampled = textTexture.Sample(textSampler, input.texCoord);
    return textColor * sampled;
}

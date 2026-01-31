/**
 * Composite Fragment Shader
 *
 * Combines the offscreen color buffer with the blurred AO texture.
 * Outputs color * lerp(1.0, ao, aoStrength) to the swapchain.
 */

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 3)]]
cbuffer CompositeUniforms : register(b0, space3) {
    float aoStrength;
    float _padding[3];
};

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D colorTexture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState colorSampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> aoTexture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState aoSampler : register(s1, space2);

float4 PSMain(PSInput input) : SV_Target {
    float3 color = colorTexture.Sample(colorSampler, input.texCoord).rgb;
    float ao = aoTexture.Sample(aoSampler, input.texCoord).r;

    float aoFactor = lerp(1.0, ao, aoStrength);
    return float4(color * aoFactor, 1.0);
}

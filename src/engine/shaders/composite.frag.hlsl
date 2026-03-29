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
    float bloomStrength;
    float volumetricFogEnabled;
    float _padding;
};

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D colorTexture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState colorSampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> aoTexture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState aoSampler : register(s1, space2);

[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D bloomTexture : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState bloomSampler : register(s2, space2);

[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D fogTexture : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState fogSampler : register(s3, space2);

// ACES filmic tone mapping (fitted curve)
float3 acesToneMap(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(PSInput input) : SV_Target {
    float3 color = colorTexture.Sample(colorSampler, input.texCoord).rgb;
    float ao = aoTexture.Sample(aoSampler, input.texCoord).r;

    float aoFactor = lerp(1.0, ao, aoStrength);
    color *= aoFactor;

    // Apply volumetric fog (premultiplied alpha blend: scene * transmittance + inscattered)
    if (volumetricFogEnabled > 0.5) {
        float4 vfog = fogTexture.Sample(fogSampler, input.texCoord);
        color = color * (1.0 - vfog.a) + vfog.rgb;
    }

    // Add bloom contribution
    float3 bloom = bloomTexture.Sample(bloomSampler, input.texCoord).rgb;
    color += bloom * bloomStrength;

    // Tone map HDR values (e.g., sun disc at 5x intensity)
    color = acesToneMap(color);

    return float4(color, 1.0);
}

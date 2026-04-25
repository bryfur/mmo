// Composite Fragment Shader.
// Input: linear HDR scene color + (optional) AO / bloom / volumetric fog.
// Output: gamma-encoded LDR to the swapchain (UNORM).
// All math runs in linear light; tonemapping (ACES fitted) is applied once,
// then gamma 2.2 is encoded explicitly because the swapchain is plain UNORM.

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
    float exposure;
    int tonemapMode;
    float contrast;
    float saturation;
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

#include "lighting.hlsli"

float3 reinhard(float3 c) {
    return c / (c + 1.0);
}

float4 PSMain(PSInput input) : SV_Target {
    float3 color = colorTexture.Sample(colorSampler, input.texCoord).rgb;

    float ao = aoTexture.Sample(aoSampler, input.texCoord).r;
    float aoFactor = lerp(1.0, ao, aoStrength);
    color *= aoFactor;

    // Premultiplied fog blend (scene * transmittance + inscattered) stays linear.
    if (volumetricFogEnabled > 0.5) {
        float4 vfog = fogTexture.Sample(fogSampler, input.texCoord);
        color = color * (1.0 - vfog.a) + vfog.rgb;
    }

    // Bloom is additive in linear HDR.
    float3 bloom = bloomTexture.Sample(bloomSampler, input.texCoord).rgb;
    color += bloom * bloomStrength;

    // Exposure scales linear HDR before tonemap.
    color *= exposure;

    // Tonemap linear HDR -> [0,1] LDR.
    if (tonemapMode == 0) {
        color = acesFitted(color);
    } else if (tonemapMode == 1) {
        color = acesNarkowicz(color);
    } else if (tonemapMode == 2) {
        color = reinhard(color);
    } else {
        color = saturate(color);
    }

    // Post-tonemap contrast around mid-grey (0.5).
    color = (color - 0.5) * contrast + 0.5;

    // Saturation: blend between luminance and color.
    float lum = luminance(color);
    color = lerp(float3(lum, lum, lum), color, saturation);

    color = saturate(color);

    // Gamma-encode for UNORM swapchain.
    color = linearToSrgb(color);

    return float4(color, 1.0);
}

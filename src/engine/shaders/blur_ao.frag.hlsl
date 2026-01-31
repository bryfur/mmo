/**
 * Bilateral Blur Fragment Shader for AO
 *
 * Performs a depth-aware Gaussian blur in one direction (horizontal or vertical).
 * Run twice (H then V) for a separable 2D blur.
 * Depth comparison preserves edges and prevents AO bleeding across surfaces.
 */

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 3)]]
cbuffer BlurUniforms : register(b0, space3) {
    float2 direction;       // (1, 0) for horizontal, (0, 1) for vertical
    float2 invScreenSize;
    float sharpness;
    float _padding[3];
};

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> aoTexture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState aoSampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> depthTexture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState depthSampler : register(s1, space2);

float PSMain(PSInput input) : SV_Target {
    float centerAO = aoTexture.SampleLevel(aoSampler, input.texCoord, 0);
    float centerDepth = depthTexture.SampleLevel(depthSampler, input.texCoord, 0);

    // Skip skybox
    if (centerDepth >= 0.9999) {
        return 1.0;
    }

    static const int KERNEL_RADIUS = 3;
    static const float weights[7] = {0.03125, 0.109375, 0.21875, 0.28125, 0.21875, 0.109375, 0.03125};

    float totalWeight = weights[KERNEL_RADIUS]; // center weight
    float blurredAO = centerAO * totalWeight;

    float2 step = direction * invScreenSize;

    for (int i = -KERNEL_RADIUS; i <= KERNEL_RADIUS; ++i) {
        if (i == 0) continue;

        float2 sampleUV = input.texCoord + step * float(i);
        float sampleAO = aoTexture.SampleLevel(aoSampler, sampleUV, 0);
        float sampleDepth = depthTexture.SampleLevel(depthSampler, sampleUV, 0);

        // Depth-aware weight: reject samples across depth discontinuities
        float depthDiff = abs(sampleDepth - centerDepth);
        float depthWeight = exp(-depthDiff * sharpness);

        float w = weights[i + KERNEL_RADIUS] * depthWeight;
        blurredAO += sampleAO * w;
        totalWeight += w;
    }

    return blurredAO / totalWeight;
}

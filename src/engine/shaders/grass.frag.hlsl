/**
 * Grass Fragment Shader - SDL3 GPU API
 *
 * Renders grass blades using vertex colors with PCSS shadows and fog support.
 */

struct PSInput {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
    float4 color : TEXCOORD3;
    float viewDepth : TEXCOORD4;
};

[[vk::binding(0, 3)]]
cbuffer GrassLightingUniforms {
    float3 cameraPos;
    float fogStart;
    float3 fogColor;
    float fogEnd;
    int fogEnabled;
    int _padding0;
    int _padding1;
    int _padding2;
    float3 lightDir;
    float _padding3;
};

[[vk::binding(1, 3)]]
cbuffer ShadowUniforms {
    float4x4 shadowLightViewProjection[4];
    float4 cascadeSplits;
    float shadowMapResolution;
    float lightSize;
    float shadowEnabled;
    float _shadowPad0;
};

// Shadow cascade textures - sampler slots 0-3 (grass has no base texture)
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D shadowMap0;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState shadowSampler0;

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D shadowMap1;
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState shadowSampler1;

[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D shadowMap2;
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState shadowSampler2;

[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D shadowMap3;
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState shadowSampler3;

float sampleShadowMap(int cascade, float2 uv) {
    if (cascade == 0) return shadowMap0.SampleLevel(shadowSampler0, uv, 0).r;
    if (cascade == 1) return shadowMap1.SampleLevel(shadowSampler1, uv, 0).r;
    if (cascade == 2) return shadowMap2.SampleLevel(shadowSampler2, uv, 0).r;
    return shadowMap3.SampleLevel(shadowSampler3, uv, 0).r;
}

#include "shadow_common.hlsli"

float4 PSMain(PSInput input) : SV_Target {
    float3 color = input.color.rgb;

    float3 norm = normalize(input.normal);
    float3 ld = normalize(lightDir);
    float diff = max(dot(norm, -ld), 0.0);

    float shadow = calcShadow(input.worldPos, input.viewDepth);

    float3 ambient = float3(0.3, 0.35, 0.3);
    float3 lighting = ambient * lerp(0.5, 1.0, shadow) + diff * float3(1.0, 0.95, 0.9) * shadow;
    color *= lighting;

    if (fogEnabled != 0) {
        float dist = length(input.worldPos - cameraPos);
        float fogFactor = saturate((dist - fogStart) / (fogEnd - fogStart));
        color = lerp(color, fogColor, fogFactor);
    }

    return float4(color, input.color.a);
}

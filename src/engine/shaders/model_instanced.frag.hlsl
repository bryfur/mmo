/**
 * Instanced Model Fragment Shader - SDL3 GPU API
 *
 * Renders 3D models with diffuse lighting, PCSS shadows, and fog.
 * Receives per-instance tint from vertex shader instead of uniform.
 */

struct PSInput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
    float viewDepth : TEXCOORD5;
    float4 instanceTint : TEXCOORD6;
    float noFog : TEXCOORD7;
};

[[vk::binding(0, 3)]]
cbuffer LightingUniforms {
    float3 lightDir;
    float _padding0;
    float3 lightColor;
    float _padding1;
    float3 ambientColor;
    float _padding2;
    float3 fogColor;
    float fogStart;
    float fogEnd;
    int hasTexture;
    int fogEnabled;
    float _padding3;
    float3 cameraPos;
    float _padding4;
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

// Base color texture - sampler slot 0
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D baseColorTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState baseColorSampler;

// Shadow cascade textures - sampler slots 1-4
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D shadowMap0;
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState shadowSampler0;

[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D shadowMap1;
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState shadowSampler1;

[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D shadowMap2;
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState shadowSampler2;

[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
Texture2D shadowMap3;
[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
SamplerState shadowSampler3;

float sampleShadowMap(int cascade, float2 uv) {
    if (cascade == 0) return shadowMap0.SampleLevel(shadowSampler0, uv, 0).r;
    if (cascade == 1) return shadowMap1.SampleLevel(shadowSampler1, uv, 0).r;
    if (cascade == 2) return shadowMap2.SampleLevel(shadowSampler2, uv, 0).r;
    return shadowMap3.SampleLevel(shadowSampler3, uv, 0).r;
}

#include "shadow_common.hlsli"

float4 PSMain(PSInput input) : SV_Target {
    float3 norm = normalize(input.normal);
    float3 lightDirection = -lightDir;

    float diff = max(dot(norm, lightDirection), 0.0);
    float3 diffuse = diff * lightColor;

    float shadow = calcShadow(input.fragPos, input.viewDepth);

    float3 lighting = ambientColor * lerp(0.5, 1.0, shadow) + diffuse * shadow;

    float4 baseColor;
    if (hasTexture == 1) {
        baseColor = baseColorTexture.Sample(baseColorSampler, input.texCoord);
        if (baseColor.a < 0.5) discard;
    } else {
        baseColor = input.vertexColor * input.instanceTint;
    }

    float3 result = lighting * baseColor.rgb;

    float3 viewDir = normalize(cameraPos - input.fragPos);
    float rim = 1.0 - max(dot(viewDir, norm), 0.0);
    rim = smoothstep(0.6, 1.0, rim);
    result += rim * 0.3 * baseColor.rgb;

    if (fogEnabled == 1 && input.noFog < 0.5) {
        float fogFactor = saturate((input.fogDistance - fogStart) / (fogEnd - fogStart));
        fogFactor = 1.0 - exp(-fogFactor * 2.0);
        result = lerp(result, fogColor, fogFactor);
    }

    return float4(result, baseColor.a);
}

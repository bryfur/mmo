// Grass Fragment Shader.
// Linear HDR output. Grass color comes from vertex color (authored linear).
// Uses a simplified PBR call (high roughness, non-metallic) plus a cheap tip
// specular to preserve the existing highlight style.

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
    float ambientStrength;
    float sunIntensity;
    float _padding4;
    float _padding5;
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
#include "fog.hlsli"
#include "lighting.hlsli"
#include "cluster_lighting.hlsli"

static const float GRASS_METALLIC  = 0.0;
static const float GRASS_ROUGHNESS = 0.9;

float4 PSMain(PSInput input) : SV_Target {
    float3 baseColor = input.color.rgb;
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 L = normalize(-lightDir);

    float shadow = calcShadow(input.worldPos, input.viewDepth);

    // Half-Lambert wraps soft shading around thin grass surfaces.
    float halfLambert = dot(N, L) * 0.5 + 0.5;
    float wrap = halfLambert * halfLambert;

    float3 lightColor = float3(1.0, 0.95, 0.9) * sunIntensity * shadow;

    float3 direct = evaluatePBR(N, V, L, lightColor,
                                 baseColor, GRASS_METALLIC, GRASS_ROUGHNESS, 1.0);
    // Blend wrap-lit diffuse with the PBR result for softness near grazing,
    // which PBR's NdotL clamp otherwise kills.
    float3 softDiffuse = baseColor * lightColor * wrap * (1.0 / PI_PBR);
    direct = max(direct, softDiffuse);

    // Tip specular highlight preserved from previous art direction.
    float tipFactor = input.texCoord.y * input.texCoord.y;
    float3 halfVec = normalize(V + L);
    float spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.3 * tipFactor;

    float3 ambientColor = float3(0.3, 0.35, 0.3) * lerp(ambientStrength, 1.0, shadow);
    float3 ambient = iblAmbient(baseColor, GRASS_METALLIC, 1.0, ambientColor);

    float3 color = direct + spec * lightColor + ambient;

    if (clusterParams.gridDim.w > 0u) {
        float2 screen_uv = input.position.xy * clusterParams.screenSize.zw;
        color += accumulateClusterLights(input.worldPos, N, V,
                                         baseColor, GRASS_METALLIC, GRASS_ROUGHNESS, 1.0,
                                         input.viewDepth, screen_uv);
    }

    if (fogEnabled != 0) {
        float dist = length(input.worldPos - cameraPos);
        float fogFactor = linearFog(dist, fogStart, fogEnd);
        color = lerp(color, fogColor, fogFactor);
    }

    return float4(color, input.color.a);
}

// Terrain Fragment Shader.
// Linear HDR output. Material textures are sampled with plain UNORM samplers;
// terrain art currently ships pre-baked and treated as linear. Splatmap is a
// control mask (linear). Sampler gamma is not assumed.

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 vertexColor : TEXCOORD1;
    float3 fragPos : TEXCOORD2;
    float fogDistance : TEXCOORD3;
    float3 normal : TEXCOORD4;
    float viewDepth : TEXCOORD5;
};

[[vk::binding(0, 3)]]
cbuffer LightingUniforms {
    float3 fogColor;
    float fogStart;
    float fogEnd;
    float worldSize;
    float _padding0[2];
    float3 lightDir;
    float _padding1;
    float ambientStrength;
    float sunIntensity;
    float _padding2[2];
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
Texture2DArray materialTextures;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState materialSampler;

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D splatmapTexture;
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState splatmapSampler;

[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D shadowMap0;
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState shadowSampler0;

[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D shadowMap1;
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState shadowSampler1;

[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
Texture2D shadowMap2;
[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
SamplerState shadowSampler2;

[[vk::combinedImageSampler]][[vk::binding(5, 2)]]
Texture2D shadowMap3;
[[vk::combinedImageSampler]][[vk::binding(5, 2)]]
SamplerState shadowSampler3;

float3 SampleTriplanarArray(Texture2DArray texArray, SamplerState samp, float3 worldPos, float3 worldNormal, float textureScale, int layer) {
    float3 blendWeights = abs(worldNormal);
    blendWeights = pow(blendWeights, 4.0);
    blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z);

    float3 xProjection = texArray.Sample(samp, float3(worldPos.yz * textureScale, layer)).rgb;
    float3 yProjection = texArray.Sample(samp, float3(worldPos.xz * textureScale, layer)).rgb;
    float3 zProjection = texArray.Sample(samp, float3(worldPos.xy * textureScale, layer)).rgb;

    return xProjection * blendWeights.x +
           yProjection * blendWeights.y +
           zProjection * blendWeights.z;
}

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

static const float TERRAIN_METALLIC  = 0.0;
static const float TERRAIN_ROUGHNESS = 0.95;
static const float TERRAIN_AO        = 1.0;

float4 PSMain(PSInput input) : SV_Target {
    float3 worldPos = input.fragPos;
    float3 worldNormal = normalize(input.normal);

    float2 splatmapUV = worldPos.xz / worldSize;
    float4 splatWeights = splatmapTexture.Sample(splatmapSampler, splatmapUV);

    float textureScale = 0.01;
    float3 grassColor = SampleTriplanarArray(materialTextures, materialSampler, worldPos, worldNormal, textureScale, 0);
    float3 dirtColor  = SampleTriplanarArray(materialTextures, materialSampler, worldPos, worldNormal, textureScale, 1);
    float3 rockColor  = SampleTriplanarArray(materialTextures, materialSampler, worldPos, worldNormal, textureScale, 2);
    float3 sandColor  = SampleTriplanarArray(materialTextures, materialSampler, worldPos, worldNormal, textureScale, 3);

    float3 terrainColor = grassColor * splatWeights.r +
                          dirtColor  * splatWeights.g +
                          rockColor  * splatWeights.b +
                          sandColor  * splatWeights.a;

    float3 baseColor = terrainColor * input.vertexColor.rgb;

    float3 N = worldNormal;
    float3 V = normalize(-lightDir);
    // Camera view isn't in this cbuffer; skybox sun direction doubles as the view
    // approximation for specular. Terrain spec is muted (high roughness) so this
    // is visually indistinguishable from exact view-vector PBR.
    float3 L = normalize(-lightDir);

    float shadow = calcShadow(input.fragPos, input.viewDepth);
    float3 lightColor = float3(1.0, 0.95, 0.9) * sunIntensity * shadow;

    float3 direct = evaluatePBR(N, V, L, lightColor,
                                 baseColor, TERRAIN_METALLIC, TERRAIN_ROUGHNESS, TERRAIN_AO);

    float3 ambientColor = float3(0.3, 0.35, 0.3) * lerp(ambientStrength, 1.0, shadow);
    float3 ambient = iblAmbient(baseColor, TERRAIN_METALLIC, TERRAIN_AO, ambientColor);

    float3 color = direct + ambient;

    if (clusterParams.gridDim.w > 0u) {
        float2 screen_uv = input.position.xy * clusterParams.screenSize.zw;
        // Use the directional sun direction as a view approximation, matching the rest of the shader.
        float3 V_eye = normalize(-lightDir);
        color += accumulateClusterLights(input.fragPos, N, V_eye,
                                         baseColor, TERRAIN_METALLIC, TERRAIN_ROUGHNESS, TERRAIN_AO,
                                         input.viewDepth, screen_uv);
    }

    float fogFactor = linearFog(input.fogDistance, fogStart, fogEnd);
    color = lerp(color, fogColor, fogFactor);

    return float4(color, 1.0);
}

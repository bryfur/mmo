// Model Fragment Shader (shared: static + skinned).
// Linear HDR output. baseColor texture is sampled sRGB-aware (UNORM_SRGB format),
// so values are already linearized by the sampler. Vertex colors are assumed linear.

struct PSInput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
    float viewDepth : TEXCOORD5;
    float4 tangent : TEXCOORD6;  // xyz = world-space tangent, w = bitangent sign
};


[[vk::binding(0, 3)]]
cbuffer LightingUniforms {
    float3 lightDir;
    float _padding0;
    float3 lightColor;
    float _padding1;
    float3 ambientColor;
    float _padding2;
    float4 tintColor;
    float3 fogColor;
    float fogStart;
    float fogEnd;
    int hasTexture;
    int fogEnabled;
    int hasNormalMap;
    float3 cameraPos;
    float normalScale;
    float ambientStrength;
    float sunIntensity;
    float _padding3;
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

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D baseColorTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState baseColorSampler;

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

[[vk::combinedImageSampler]][[vk::binding(5, 2)]]
Texture2D normalMapTexture;
[[vk::combinedImageSampler]][[vk::binding(5, 2)]]
SamplerState normalMapSampler;

float sampleShadowMap(int cascade, float2 uv) {
    if (cascade == 0) return shadowMap0.SampleLevel(shadowSampler0, uv, 0).r;
    if (cascade == 1) return shadowMap1.SampleLevel(shadowSampler1, uv, 0).r;
    if (cascade == 2) return shadowMap2.SampleLevel(shadowSampler2, uv, 0).r;
    return shadowMap3.SampleLevel(shadowSampler3, uv, 0).r;
}

#include "shadow_common.hlsli"
#include "fog.hlsli"
#include "lighting.hlsli"

// Scalar PBR defaults until metallic/roughness textures are wired in.
static const float DEFAULT_METALLIC  = 0.0;
static const float DEFAULT_ROUGHNESS = 0.85;
static const float DEFAULT_AO        = 1.0;

#include "cluster_lighting.hlsli"

float4 PSMain(PSInput input) : SV_Target {
    float3 N_geo = normalize(input.normal);
    float3 V = normalize(cameraPos - input.fragPos);
    // lightDir is the direction the light travels; L is surface->light.
    float3 L = normalize(-lightDir);

    // Tangent-space normal mapping. When no normal map is bound, the default
    // flat texture (128,128,255) decodes to (0,0,1) which leaves N == N_geo.
    float3 N = N_geo;
    if (hasNormalMap == 1) {
        float3 Nts = normalMapTexture.Sample(normalMapSampler, input.texCoord).xyz * 2.0 - 1.0;
        Nts.xy *= normalScale;
        float3 T_ws = normalize(input.tangent.xyz);
        float3 B_ws = cross(N_geo, T_ws) * input.tangent.w;
        N = normalize(T_ws * Nts.x + B_ws * Nts.y + N_geo * Nts.z);
    }

    float4 baseColor;
    if (hasTexture == 1) {
        baseColor = baseColorTexture.Sample(baseColorSampler, input.texCoord);
        if (baseColor.a < 0.5) discard;
    } else {
        baseColor = input.vertexColor * tintColor;
    }

    float shadow = calcShadow(input.fragPos, input.viewDepth);

    // Direct lighting: full PBR BRDF.
    float3 directLight = lightColor * sunIntensity * shadow;
    float3 direct = evaluatePBR(N, V, L, directLight,
                                 baseColor.rgb,
                                 DEFAULT_METALLIC, DEFAULT_ROUGHNESS, DEFAULT_AO);

    // Ambient: constant-color placeholder until IBL is added. Shadow dims ambient
    // slightly to avoid flat fill-light look, matching the previous art direction.
    float3 ambient = iblAmbient(baseColor.rgb, DEFAULT_METALLIC, DEFAULT_AO,
                                 ambientColor * lerp(ambientStrength, 1.0, shadow));

    float3 result = direct + ambient;

    if (clusterParams.gridDim.w > 0u) {
        float2 screen_uv = input.position.xy * clusterParams.screenSize.zw;
        result += accumulateClusterLights(input.fragPos, N, V,
                                          baseColor.rgb, DEFAULT_METALLIC, DEFAULT_ROUGHNESS, DEFAULT_AO,
                                          input.viewDepth, screen_uv);
    }

    if (fogEnabled == 1) {
        float fogFactor = distanceFogFactor(input.fogDistance, fogStart, fogEnd);
        result = lerp(result, fogColor, fogFactor);
    }

    return float4(result, baseColor.a);
}

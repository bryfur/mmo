/**
 * Skinned Model Fragment Shader - SDL3 GPU API
 *
 * Renders skinned 3D models with diffuse lighting, PCSS shadows, and fog.
 */

struct PSInput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
    float viewDepth : TEXCOORD5;
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

static const float2 poissonDisk[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2(0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543,  0.27676845), float2(0.97484398,  0.75648379),
    float2(0.44323325, -0.97511554),  float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514,  0.19090188),
    float2(-0.24188840,  0.99706507), float2(-0.81409955,  0.91437590),
    float2(0.19984126,  0.78641367),  float2(0.14383161, -0.14100790)
};

float sampleShadowMap(int cascade, float2 uv) {
    if (cascade == 0) return shadowMap0.SampleLevel(shadowSampler0, uv, 0).r;
    if (cascade == 1) return shadowMap1.SampleLevel(shadowSampler1, uv, 0).r;
    if (cascade == 2) return shadowMap2.SampleLevel(shadowSampler2, uv, 0).r;
    return shadowMap3.SampleLevel(shadowSampler3, uv, 0).r;
}

int selectCascade(float depth) {
    if (depth < cascadeSplits.x) return 0;
    if (depth < cascadeSplits.y) return 1;
    if (depth < cascadeSplits.z) return 2;
    return 3;
}

float3 getShadowCoord(float3 worldPos, int cascade) {
    float4 lightSpacePos = mul(shadowLightViewProjection[cascade], float4(worldPos, 1.0));
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = 1.0 - (projCoords.y * 0.5 + 0.5);
    return projCoords;
}

float calcShadow(float3 worldPos, float depth) {
    if (shadowEnabled < 0.5) return 1.0;

    int cascade = selectCascade(depth);
    float3 sc = getShadowCoord(worldPos, cascade);

    if (sc.x < 0.0 || sc.x > 1.0 ||
        sc.y < 0.0 || sc.y > 1.0 ||
        sc.z < 0.0 || sc.z > 1.0)
        return 1.0;

    float bias = 0.005;

    // Hard shadows (mode 1)
    if (shadowEnabled < 1.5) {
        float d = sampleShadowMap(cascade, sc.xy);
        return (d < sc.z - bias) ? 0.3 : 1.0;
    }

    // PCSS (mode 2)
    float texelSize = 1.0 / shadowMapResolution;

    float searchRadius = lightSize * texelSize;
    float blockerSum = 0.0;
    int blockerCount = 0;
    for (int i = 0; i < 16; i++) {
        float2 offset = poissonDisk[i] * searchRadius;
        float d = sampleShadowMap(cascade, sc.xy + offset);
        if (d < sc.z - bias) {
            blockerSum += d;
            blockerCount++;
        }
    }

    if (blockerCount == 0) return 1.0;

    float avgBlockerDepth = blockerSum / float(blockerCount);
    float penumbra = (sc.z - avgBlockerDepth) * lightSize / avgBlockerDepth;
    float filterRadius = penumbra * texelSize;
    filterRadius = clamp(filterRadius, texelSize, texelSize * 20.0);

    float shadow = 0.0;
    for (int j = 0; j < 16; j++) {
        float2 offset = poissonDisk[j] * filterRadius;
        float d = sampleShadowMap(cascade, sc.xy + offset);
        shadow += (d < sc.z - bias) ? 0.0 : 1.0;
    }
    shadow /= 16.0;

    return lerp(0.3, 1.0, shadow);
}

float4 PSMain(PSInput input) : SV_Target {
    float3 norm = normalize(input.normal);
    float3 lightDirection = normalize(-lightDir);

    float diff = max(dot(norm, lightDirection), 0.0);
    float3 diffuse = diff * lightColor;

    float shadow = calcShadow(input.fragPos, input.viewDepth);

    float3 lighting = ambientColor * lerp(0.5, 1.0, shadow) + diffuse * shadow;

    float4 baseColor;
    if (hasTexture == 1) {
        baseColor = baseColorTexture.Sample(baseColorSampler, input.texCoord);
    } else {
        baseColor = input.vertexColor * tintColor;
    }

    float3 result = lighting * baseColor.rgb;

    float3 viewDir = normalize(-input.fragPos);
    float rim = 1.0 - max(dot(viewDir, norm), 0.0);
    rim = smoothstep(0.6, 1.0, rim);
    result += rim * 0.3 * baseColor.rgb;

    if (fogEnabled == 1) {
        float fogFactor = saturate((input.fogDistance - fogStart) / (fogEnd - fogStart));
        fogFactor = 1.0 - exp(-fogFactor * 2.0);
        result = lerp(result, fogColor, fogFactor);
    }

    return float4(result, baseColor.a);
}

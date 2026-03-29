/**
 * Common Shadow Functions - PCSS Cascaded Shadow Maps
 *
 * Shared by all fragment shaders that receive shadows.
 * Requires: sampleShadowMap(int cascade, float2 uv) to be defined before including.
 * Requires: ShadowUniforms cbuffer with shadowLightViewProjection, cascadeSplits,
 *           shadowMapResolution, lightSize, shadowEnabled to be declared before including.
 */

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

    // Hard shadows with 4-tap PCF (mode 1)
    if (shadowEnabled < 1.5) {
        float texelSize = 1.0 / shadowMapResolution;
        float shadow = 0.0;
        [unroll]
        for (int i = 0; i < 4; i++) {
            float2 offset = poissonDisk[i] * texelSize;
            float d = sampleShadowMap(cascade, sc.xy + offset);
            shadow += (d < sc.z - bias) ? 0.0 : 1.0;
        }
        shadow /= 4.0;
        return lerp(0.3, 1.0, shadow);
    }

    // PCSS (mode 2)
    float texelSize = 1.0 / shadowMapResolution;

    // Blocker search
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

    // PCF with variable radius
    float shadow = 0.0;
    for (int j = 0; j < 16; j++) {
        float2 offset = poissonDisk[j] * filterRadius;
        float d = sampleShadowMap(cascade, sc.xy + offset);
        shadow += (d < sc.z - bias) ? 0.0 : 1.0;
    }
    shadow /= 16.0;

    return lerp(0.3, 1.0, shadow);
}

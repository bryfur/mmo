/**
 * Terrain Fragment Shader - SDL3 GPU API
 *
 * Renders terrain with texture, PCSS shadows, and fog.
 */

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
    float worldSize;  // Size of the terrain world (assumes square)
    float _padding0[2];
    float3 lightDir;
    float _padding1;
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

// Material texture array - sampler slot 0 (4 layers: grass, dirt, rock, sand)
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2DArray materialTextures;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState materialSampler;

// Splatmap texture - sampler slot 1
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D splatmapTexture;
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState splatmapSampler;

// Shadow cascade textures - sampler slots 2-5 (shifted from 1-4)
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

// Triplanar texture sampling for Texture2DArray - eliminates stretching on steep slopes
float3 SampleTriplanarArray(Texture2DArray texArray, SamplerState samp, float3 worldPos, float3 worldNormal, float textureScale, int layer) {
    // Calculate blend weights based on normal direction
    float3 blendWeights = abs(worldNormal);
    blendWeights = pow(blendWeights, 4.0); // Sharper transitions between projections
    blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z); // Normalize

    // Sample from three planar projections
    float3 xProjection = texArray.Sample(samp, float3(worldPos.yz * textureScale, layer)).rgb;
    float3 yProjection = texArray.Sample(samp, float3(worldPos.xz * textureScale, layer)).rgb;
    float3 zProjection = texArray.Sample(samp, float3(worldPos.xy * textureScale, layer)).rgb;

    // Blend based on surface orientation
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

float4 PSMain(PSInput input) : SV_Target {
    float3 worldPos = input.fragPos;
    float3 worldNormal = normalize(input.normal);

    // Sample splatmap to get material weights (R=grass, G=dirt, B=rock, A=sand)
    // Map world position to 0-1 UV range
    float2 splatmapUV = worldPos.xz / worldSize;
    float4 splatWeights = splatmapTexture.Sample(splatmapSampler, splatmapUV);

    // Sample all 4 materials from the texture array using triplanar mapping
    float textureScale = 0.01; // Scale for material textures
    float3 grassColor = SampleTriplanarArray(materialTextures, materialSampler, worldPos, worldNormal, textureScale, 0);
    float3 dirtColor = SampleTriplanarArray(materialTextures, materialSampler, worldPos, worldNormal, textureScale, 1);
    float3 rockColor = SampleTriplanarArray(materialTextures, materialSampler, worldPos, worldNormal, textureScale, 2);
    float3 sandColor = SampleTriplanarArray(materialTextures, materialSampler, worldPos, worldNormal, textureScale, 3);

    // Blend materials based on splatmap weights
    float3 terrainColor = grassColor * splatWeights.r +
                          dirtColor * splatWeights.g +
                          rockColor * splatWeights.b +
                          sandColor * splatWeights.a;

    // Apply vertex color tint
    float3 color = terrainColor * input.vertexColor.rgb;

    // ===================================================================
    // LIGHTING AND SHADOWS
    // ===================================================================
    float3 lightDirection = normalize(-lightDir);
    float3 norm = normalize(input.normal);
    float diff = max(dot(norm, lightDirection), 0.0);

    float shadow = calcShadow(input.fragPos, input.viewDepth);

    float3 ambient = float3(0.3, 0.35, 0.3);
    float3 lighting = ambient * lerp(0.5, 1.0, shadow) + diff * float3(1.0, 0.95, 0.9) * shadow;
    color *= lighting;

    // ===================================================================
    // FOG
    // ===================================================================
    float fogFactor = saturate((input.fogDistance - fogStart) / (fogEnd - fogStart));
    color = lerp(color, fogColor, fogFactor);

    return float4(color, 1.0);
}

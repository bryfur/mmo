/**
 * Volumetric Fog Fragment Shader (God Rays / Light Shafts)
 *
 * Screen-space ray marching through height-based fog volume.
 * For each pixel, marches from camera toward the world position,
 * sampling the shadow map to determine lit vs shadowed fog.
 * Produces inscattered light (god rays) and fog extinction.
 *
 * Output: RGB = inscattered light, A = 1 - transmittance (opacity)
 */

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 3)]]
cbuffer VolumetricFogUniforms : register(b0, space3) {
    float4x4 invViewProjection;
    float4x4 shadowLightViewProjection;
    float3 lightDir;
    float fogDensity;
    float3 lightColor;
    float scatterStrength;
    float3 fogColor;
    float fogHeight;
    float3 cameraPos;
    float fogFalloff;
    float nearPlane;
    float farPlane;
    float godRaysEnabled;
    float densityMultiplier;
};

// Depth texture
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depthTexture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depthSampler : register(s0, space2);

// Shadow map (cascade 0 for god rays)
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D shadowMap : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState shadowSampler : register(s1, space2);

float3 reconstructWorldPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;  // Flip Y for Vulkan
    float4 worldPos = mul(invViewProjection, clipPos);
    return worldPos.xyz / worldPos.w;
}

float sampleShadow(float3 worldPos) {
    float4 lightSpace = mul(shadowLightViewProjection, float4(worldPos, 1.0));
    float3 projCoords = lightSpace.xyz / lightSpace.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = 1.0 - (projCoords.y * 0.5 + 0.5);

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;

    static const float2 pcfOffsets[4] = {
        float2(-0.7071, -0.7071),
        float2( 0.7071, -0.7071),
        float2(-0.7071,  0.7071),
        float2( 0.7071,  0.7071)
    };

    float2 texelSize = float2(1.0, 1.0) / 2048.0;
    float bias = 0.0015;
    float visibility = 0.0;
    [unroll]
    for (int i = 0; i < 4; i++) {
        float2 uv = projCoords.xy + pcfOffsets[i] * texelSize;
        float shadowDepth = shadowMap.SampleLevel(shadowSampler, uv, 0).r;
        visibility += (shadowDepth < projCoords.z - bias) ? 0.0 : 1.0;
    }
    return visibility * 0.25;
}

// Height-based fog density - smooth exponential falloff from ground level
float getFogDensity(float3 pos) {
    // Smooth falloff: densest at y=0, fades exponentially with height
    // fogHeight controls the scale (higher = fog extends higher)
    // No hard cutoff - just a continuous gradient
    float normalizedHeight = pos.y / max(fogHeight, 1.0);
    float heightFactor = exp(-normalizedHeight * normalizedHeight * fogFalloff * 10.0);
    return fogDensity * densityMultiplier * heightFactor;
}

float4 PSMain(PSInput input) : SV_Target {
    float depth = depthTexture.SampleLevel(depthSampler, input.texCoord, 0).r;

    // Skip sky pixels
    if (depth >= 1.0) return float4(0, 0, 0, 0);

    float3 worldPos = reconstructWorldPos(input.texCoord, depth);
    float3 rayDir = worldPos - cameraPos;
    float rayLength = length(rayDir);
    rayDir /= rayLength;

    // Mie scattering phase function (simplified forward scattering)
    float3 lightDirection = normalize(-lightDir);
    float cosAngle = dot(rayDir, lightDirection);
    float phase = 0.25 + 0.75 * pow(max(0.0, cosAngle), 8.0);

    // Ray march through the volume
    int numSteps = 24;
    float startDist = 100.0;  // Skip fog close to camera
    float maxDist = min(rayLength, 3000.0);
    if (maxDist <= startDist) return float4(0, 0, 0, 0);
    float stepSize = (maxDist - startDist) / float(numSteps);

    float3 fog = float3(0, 0, 0);
    float transmittance = 1.0;

    // Interleaved gradient noise (less visible pattern than white noise hash)
    float2 screenPos = input.position.xy;
    float dither = frac(52.9829189 * frac(dot(screenPos, float2(0.06711056, 0.00583715))));
    float3 pos = cameraPos + rayDir * (startDist + stepSize * dither);

    for (int i = 0; i < numSteps; i++) {
        float density = getFogDensity(pos);

        if (density > 0.001) {
            // Ambient fog contribution (always active)
            float3 ambientFog = fogColor * 0.2;

            // God rays: inscattered light through unshadowed fog
            float3 inScatter = float3(0, 0, 0);
            if (godRaysEnabled > 0.5) {
                float shadow = sampleShadow(pos);
                inScatter = shadow * lightColor * phase * scatterStrength;
            }

            float extinction = density * stepSize;
            float stepTransmittance = exp(-extinction);

            fog += transmittance * (inScatter + ambientFog) * (1.0 - stepTransmittance);
            transmittance *= stepTransmittance;
        }

        pos += rayDir * stepSize;
    }

    return float4(fog, 1.0 - transmittance);
}

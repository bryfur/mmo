/**
 * SSAO (Screen-Space Ambient Occlusion) Fragment Shader
 *
 * Simpler than GTAO — uses random hemisphere samples around each fragment
 * to estimate occlusion. Cheaper but lower quality.
 * Outputs a single-channel AO value (1.0 = no occlusion, 0.0 = full occlusion).
 */

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 3)]]
cbuffer GTAOUniforms : register(b0, space3) {
    float4x4 projection;
    float4x4 invProjection;
    float2 screenSize;
    float2 invScreenSize;
    float radius;
    float bias;
    int numDirections;
    int numSteps;
};

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depthTexture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depthSampler : register(s0, space2);

static const float PI = 3.14159265359;

float3 viewPosFromDepth(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 viewPos = mul(invProjection, clipPos);
    return viewPos.xyz / viewPos.w;
}

float interleavedGradientNoise(float2 pos) {
    return frac(52.9829189 * frac(dot(pos, float2(0.06711056, 0.00583715))));
}

// Simple hash to generate pseudo-random 3D sample directions
float3 randomSample(float2 pixelPos, int index) {
    float n1 = interleavedGradientNoise(pixelPos + float2(index * 7.13, index * 3.71));
    float n2 = interleavedGradientNoise(pixelPos + float2(index * 5.37, index * 11.29));
    float n3 = interleavedGradientNoise(pixelPos + float2(index * 13.07, index * 2.53));

    // Map to hemisphere (z >= 0)
    float theta = n1 * 2.0 * PI;
    float phi = acos(1.0 - 2.0 * n2) * 0.5; // hemisphere only
    float r = n3;

    float3 s;
    s.x = sin(phi) * cos(theta);
    s.y = sin(phi) * sin(theta);
    s.z = cos(phi);
    return s * r;
}

float PSMain(PSInput input) : SV_Target {
    float depth = depthTexture.SampleLevel(depthSampler, input.texCoord, 0);

    if (depth >= 0.9999) {
        return 1.0;
    }

    float3 viewPos = viewPosFromDepth(input.texCoord, depth);

    // Reconstruct normal from screen-space derivatives
    float3 dpdx = ddx(viewPos);
    float3 dpdy = ddy(viewPos);
    float3 viewNormal = normalize(cross(dpdy, dpdx));

    int totalSamples = numDirections * numSteps;
    float occlusion = 0.0;

    for (int i = 0; i < totalSamples; ++i) {
        float3 sampleDir = randomSample(input.position.xy, i);

        // Flip if pointing away from normal (orient to hemisphere)
        if (dot(sampleDir, viewNormal) < 0.0) {
            sampleDir = -sampleDir;
        }

        // Scale by radius and offset from view position
        float3 samplePos = viewPos + sampleDir * radius;

        // Project sample to screen space
        float4 clipPos = mul(projection, float4(samplePos, 1.0));
        clipPos.xy /= clipPos.w;
        clipPos.y = -clipPos.y;
        float2 sampleUV = clipPos.xy * 0.5 + 0.5;

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
            continue;
        }

        float sampleDepth = depthTexture.SampleLevel(depthSampler, sampleUV, 0);
        float3 actualPos = viewPosFromDepth(sampleUV, sampleDepth);

        // Check if the actual surface is closer than our sample (occluding)
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(viewPos.z - actualPos.z), 0.001));
        occlusion += (actualPos.z >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion /= max(float(totalSamples), 1.0);
    return saturate(1.0 - occlusion);
}

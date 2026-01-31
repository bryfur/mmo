/**
 * GTAO (Ground Truth Ambient Occlusion) Fragment Shader
 *
 * Reads the depth buffer, reconstructs view-space positions, estimates normals
 * from screen-space derivatives, and performs spiral sampling to compute AO.
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

// Reconstruct view-space position from UV + depth
float3 viewPosFromDepth(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y; // Vulkan Y flip
    float4 viewPos = mul(invProjection, clipPos);
    return viewPos.xyz / viewPos.w;
}

// Simple hash for pseudo-random rotation per pixel
float interleavedGradientNoise(float2 pos) {
    return frac(52.9829189 * frac(dot(pos, float2(0.06711056, 0.00583715))));
}

float PSMain(PSInput input) : SV_Target {
    float depth = depthTexture.SampleLevel(depthSampler, input.texCoord, 0);

    // Skip skybox
    if (depth >= 0.9999) {
        return 1.0;
    }

    float3 viewPos = viewPosFromDepth(input.texCoord, depth);

    // Reconstruct normal from screen-space derivatives of view position
    float3 dpdx = ddx(viewPos);
    float3 dpdy = ddy(viewPos);
    float3 viewNormal = normalize(cross(dpdy, dpdx));

    // Per-pixel rotation angle for noise reduction
    float rotationAngle = interleavedGradientNoise(input.position.xy) * 2.0 * PI;

    float occlusion = 0.0;
    float sampleCount = 0.0;

    // Screen-space radius scaled by projection
    float screenRadius = radius * projection[0][0] * 0.5 / max(-viewPos.z, 0.1);
    // Clamp to avoid too large or too small radii
    screenRadius = clamp(screenRadius, 2.0 * invScreenSize.x, 0.2);

    for (int dir = 0; dir < numDirections; ++dir) {
        float angle = rotationAngle + (float(dir) / float(numDirections)) * 2.0 * PI;
        float2 direction = float2(cos(angle), sin(angle));

        for (int step = 1; step <= numSteps; ++step) {
            float t = float(step) / float(numSteps);
            float2 sampleUV = input.texCoord + direction * screenRadius * t;

            // Bounds check
            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
                continue;
            }

            float sampleDepth = depthTexture.SampleLevel(depthSampler, sampleUV, 0);
            float3 samplePos = viewPosFromDepth(sampleUV, sampleDepth);

            float3 sampleVec = samplePos - viewPos;
            float sampleDist = length(sampleVec);

            if (sampleDist < 0.001) {
                continue;
            }

            float3 sampleDir = sampleVec / sampleDist;

            // Cosine-weighted contribution
            float cosAngle = max(0.0, dot(viewNormal, sampleDir) - bias);

            // Distance falloff
            float falloff = 1.0 - smoothstep(0.0, radius, sampleDist);

            occlusion += cosAngle * falloff;
            sampleCount += 1.0;
        }
    }

    if (sampleCount > 0.0) {
        occlusion /= sampleCount;
    }

    float ao = saturate(1.0 - occlusion);
    return ao;
}

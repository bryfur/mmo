/**
 * Skybox Fragment Shader - SDL3 GPU API
 *
 * Simple procedural sky: gradient, sun disc, and subtle horizon haze.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float2 clipPos : TEXCOORD0;
};

// Uniform buffer - SDL3 GPU SPIR-V requires fragment uniforms in set 3
[[vk::binding(0, 3)]]
cbuffer SkyUniforms {
    float4x4 invViewProjection;
    float time;
    float3 sunDirection;  // Direction TO sun (normalized)
};

float4 PSMain(PSInput input) : SV_Target {
    // Compute world-space ray direction per-pixel
    float4 worldPos = mul(invViewProjection, float4(input.clipPos, 1.0, 1.0));
    float3 dir = normalize(worldPos.xyz / worldPos.w);

    float3 sunDir = normalize(sunDirection);
    float sunHeight = sunDir.y;
    float dayFactor = saturate(sunHeight * 2.0 + 0.5);

    // Sky gradient
    float horizon = smoothstep(-0.1, 0.4, dir.y);
    float3 skyTop = lerp(float3(0.02, 0.05, 0.12), float3(0.25, 0.45, 0.85), dayFactor);
    float3 skyHorizon = lerp(float3(0.08, 0.1, 0.18), float3(0.55, 0.65, 0.8), dayFactor);
    float3 skyColor = lerp(skyHorizon, skyTop, horizon);

    // Sun disc and glow
    float sunAngle = acos(clamp(dot(dir, sunDir), -1.0, 1.0));
    float sunDisc = smoothstep(0.02, 0.01, sunAngle);
    float corona = exp(-sunAngle * sunAngle / 0.02) * 0.5;
    float glare = exp(-sunAngle / 0.4) * 0.2;

    skyColor += sunDisc * float3(1.0, 0.95, 0.85) * 5.0;
    skyColor += corona * float3(1.0, 0.8, 0.5);
    skyColor += glare * float3(1.0, 0.9, 0.7) * (1.0 - horizon);

    // Subtle warm tint near horizon on the sun side
    float sunSide = saturate(dot(normalize(float2(dir.x, dir.z)), normalize(float2(sunDir.x, sunDir.z))));
    float horizonGlow = (1.0 - horizon) * sunSide * dayFactor * 0.3;
    skyColor += float3(0.8, 0.4, 0.1) * horizonGlow;

    return float4(skyColor, 1.0);
}

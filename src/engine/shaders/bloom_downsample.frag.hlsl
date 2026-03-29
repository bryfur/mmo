/**
 * Bloom Downsample Fragment Shader
 *
 * Extracts bright pixels above a luminance threshold and downsamples
 * using a 13-tap filter for high quality (Jimenez 2014 / Call of Duty).
 * Each mip level reads from the previous level.
 */

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 3)]]
cbuffer BloomDownsampleUniforms : register(b0, space3) {
    float2 srcTexelSize;   // 1.0 / source resolution
    float threshold;       // luminance threshold (first pass only)
    float isFirstPass;     // 1.0 for first pass (apply threshold), 0.0 otherwise
};

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D srcTexture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState srcSampler : register(s0, space2);

float luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Soft threshold: smooth knee around the threshold value
float3 applyThreshold(float3 color) {
    float lum = luminance(color);
    float knee = threshold * 0.5;
    float soft = lum - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(soft, lum - threshold) / max(lum, 0.00001);
    return color * max(contribution, 0.0);
}

float4 PSMain(PSInput input) : SV_Target {
    float2 uv = input.texCoord;
    float2 ts = srcTexelSize;

    // 13-tap downsample filter (Jimenez 2014)
    // Center sample
    float3 a = srcTexture.SampleLevel(srcSampler, uv, 0).rgb;

    // Inner ring (4 samples at half-texel offsets)
    float3 b = srcTexture.SampleLevel(srcSampler, uv + float2(-0.5, -0.5) * ts, 0).rgb;
    float3 c = srcTexture.SampleLevel(srcSampler, uv + float2( 0.5, -0.5) * ts, 0).rgb;
    float3 d = srcTexture.SampleLevel(srcSampler, uv + float2(-0.5,  0.5) * ts, 0).rgb;
    float3 e = srcTexture.SampleLevel(srcSampler, uv + float2( 0.5,  0.5) * ts, 0).rgb;

    // Outer ring (8 samples at 1-texel offsets)
    float3 f = srcTexture.SampleLevel(srcSampler, uv + float2(-1.0, -1.0) * ts, 0).rgb;
    float3 g = srcTexture.SampleLevel(srcSampler, uv + float2( 0.0, -1.0) * ts, 0).rgb;
    float3 h = srcTexture.SampleLevel(srcSampler, uv + float2( 1.0, -1.0) * ts, 0).rgb;
    float3 i = srcTexture.SampleLevel(srcSampler, uv + float2(-1.0,  0.0) * ts, 0).rgb;
    float3 j = srcTexture.SampleLevel(srcSampler, uv + float2( 1.0,  0.0) * ts, 0).rgb;
    float3 k = srcTexture.SampleLevel(srcSampler, uv + float2(-1.0,  1.0) * ts, 0).rgb;
    float3 l = srcTexture.SampleLevel(srcSampler, uv + float2( 0.0,  1.0) * ts, 0).rgb;
    float3 m = srcTexture.SampleLevel(srcSampler, uv + float2( 1.0,  1.0) * ts, 0).rgb;

    // Weighted combination (anti-firefly: weighted average of groups)
    float3 color = a * 0.125;
    color += (b + c + d + e) * 0.125;
    color += (f + g + i) * 0.03125 * 2.0;
    color += (g + h + j) * 0.03125 * 2.0;
    color += (i + k + l) * 0.03125 * 2.0;
    color += (j + l + m) * 0.03125 * 2.0;

    // Apply threshold on first pass only
    if (isFirstPass > 0.5) {
        color = applyThreshold(color);
    }

    return float4(color, 1.0);
}

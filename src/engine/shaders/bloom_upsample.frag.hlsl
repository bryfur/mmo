/**
 * Bloom Upsample Fragment Shader
 *
 * Upsamples bloom texture using a 9-tap tent filter and additively
 * combines with the next mip level up.
 */

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 3)]]
cbuffer BloomUpsampleUniforms : register(b0, space3) {
    float2 srcTexelSize;   // 1.0 / source (lower-res) texture resolution
    float bloomRadius;     // filter radius multiplier (default 1.0)
    float _padding;
};

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D srcTexture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState srcSampler : register(s0, space2);

float4 PSMain(PSInput input) : SV_Target {
    float2 uv = input.texCoord;
    float2 ts = srcTexelSize * bloomRadius;

    // 9-tap tent filter (3x3 bilinear)
    float3 color = float3(0, 0, 0);

    // Corners (weight 1)
    color += srcTexture.SampleLevel(srcSampler, uv + float2(-1.0, -1.0) * ts, 0).rgb * 1.0;
    color += srcTexture.SampleLevel(srcSampler, uv + float2( 1.0, -1.0) * ts, 0).rgb * 1.0;
    color += srcTexture.SampleLevel(srcSampler, uv + float2(-1.0,  1.0) * ts, 0).rgb * 1.0;
    color += srcTexture.SampleLevel(srcSampler, uv + float2( 1.0,  1.0) * ts, 0).rgb * 1.0;

    // Edges (weight 2)
    color += srcTexture.SampleLevel(srcSampler, uv + float2( 0.0, -1.0) * ts, 0).rgb * 2.0;
    color += srcTexture.SampleLevel(srcSampler, uv + float2(-1.0,  0.0) * ts, 0).rgb * 2.0;
    color += srcTexture.SampleLevel(srcSampler, uv + float2( 1.0,  0.0) * ts, 0).rgb * 2.0;
    color += srcTexture.SampleLevel(srcSampler, uv + float2( 0.0,  1.0) * ts, 0).rgb * 2.0;

    // Center (weight 4)
    color += srcTexture.SampleLevel(srcSampler, uv, 0).rgb * 4.0;

    // Total weight: 4*1 + 4*2 + 1*4 = 16
    color /= 16.0;

    return float4(color, 1.0);
}

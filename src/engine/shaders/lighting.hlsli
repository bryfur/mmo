// lighting.hlsli - shared helpers: color space, luminance, noise, tonemap, depth reconstruction.
// All math assumes linear color unless documented otherwise.

#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

static const float PI_ = 3.14159265358979323846;

float luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Exact sRGB <-> linear (piecewise), used sparingly where sampler gamma doesn't apply.
float3 srgbToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return lerp(lo, hi, step(0.04045, c));
}

float3 linearToSrgb(float3 c) {
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 1e-5), 1.0 / 2.4) - 0.055;
    return lerp(lo, hi, step(0.0031308, c));
}

// Interleaved gradient noise (Jimenez 2014) - low-visibility dither pattern.
float interleavedGradientNoise(float2 screenPos) {
    return frac(52.9829189 * frac(dot(screenPos, float2(0.06711056, 0.00583715))));
}

// ACES tonemapping curves. Input is linear HDR, output is in [0,1].

// Krzysztof Narkowicz 3-coefficient approximation.
float3 acesNarkowicz(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Hill fitted ACES (higher quality, desaturates highlights correctly).
float3 acesFitted(float3 x) {
    const float3x3 ACESInputMat = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );
    const float3x3 ACESOutputMat = float3x3(
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    );
    x = mul(ACESInputMat, x);
    float3 a = x * (x + 0.0245786) - 0.000090537;
    float3 b = x * (0.983729 * x + 0.4329510) + 0.238081;
    x = a / b;
    x = mul(ACESOutputMat, x);
    return saturate(x);
}

// Depth reconstruction. Assumes projection produces Vulkan-style [0,1] z, flipped Y.
float3 viewPosFromDepth(float2 uv, float depth, float4x4 invProjection) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 view = mul(invProjection, clip);
    return view.xyz / view.w;
}

float3 worldPosFromDepth(float2 uv, float depth, float4x4 invViewProjection) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(invViewProjection, clip);
    return world.xyz / world.w;
}

#endif // LIGHTING_HLSLI

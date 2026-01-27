/**
 * Skybox Fragment Shader - SDL3 GPU API
 * 
 * Procedural skybox with sun, clouds, and mountain silhouettes.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
};

// Uniform buffer - SDL3 GPU SPIR-V requires fragment uniforms in set 3
[[vk::binding(0, 3)]]
cbuffer SkyUniforms {
    float time;
    float3 sunDirection;  // Direction TO sun (normalized)
};

// Simple hash function for noise
float hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

// Value noise
float noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + float2(1.0, 0.0));
    float c = hash(i + float2(0.0, 1.0));
    float d = hash(i + float2(1.0, 1.0));
    
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// FBM for mountains
float fbm(float2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    [unroll]
    for (int i = 0; i < 5; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

float4 PSMain(PSInput input) : SV_Target {
    float3 dir = normalize(input.worldPos);
    
    // Sun direction and sun disk calculation
    float3 sunDir = normalize(sunDirection);
    float sunAngle = acos(clamp(dot(dir, sunDir), -1.0, 1.0));
    
    // Sun disk (small bright core)
    float sunDiskRadius = 0.02;  // Angular radius of sun disk
    float sunDisk = smoothstep(sunDiskRadius, sunDiskRadius * 0.5, sunAngle);
    
    // Sun corona/glow (larger soft glow around sun)
    float coronaRadius = 0.15;
    float corona = exp(-sunAngle * sunAngle / (coronaRadius * coronaRadius)) * 0.6;
    
    // Sun glare/rays (subtle light rays)
    float glareRadius = 0.4;
    float glare = exp(-sunAngle / glareRadius) * 0.3;
    
    // Sun color - warm yellow/white
    float3 sunColor = float3(1.0, 0.95, 0.85);
    float3 coronaColor = float3(1.0, 0.8, 0.5);
    float3 glareColor = float3(1.0, 0.9, 0.7);
    
    // Sky gradient - affected by sun position for more realistic lighting
    float horizon = smoothstep(-0.1, 0.3, dir.y);
    
    // Base sky colors - brighten based on sun height
    float sunHeight = sunDir.y;  // How high sun is in sky
    float dayFactor = saturate(sunHeight * 2.0 + 0.5);  // 0 at night, 1 at day
    
    // Day sky colors
    float3 dayTop = float3(0.2, 0.4, 0.8);       // Blue sky at top
    float3 dayHorizon = float3(0.5, 0.6, 0.75);  // Lighter at horizon
    
    // Night sky colors
    float3 nightTop = float3(0.02, 0.05, 0.12);      // Dark blue-black
    float3 nightHorizon = float3(0.08, 0.1, 0.18);   // Slightly lighter
    
    // Blend between day and night based on sun height
    float3 skyTop = lerp(nightTop, dayTop, dayFactor);
    float3 skyHorizon = lerp(nightHorizon, dayHorizon, dayFactor);
    float3 skyColor = lerp(skyHorizon, skyTop, horizon);
    
    // Add sun and glow contribution to sky
    skyColor += sunDisk * sunColor * 5.0;   // Bright sun disk
    skyColor += corona * coronaColor;        // Soft corona
    skyColor += glare * glareColor * (1.0 - horizon);  // Glare fades at horizon
    
    // Add subtle stars at top of sky
    if (dir.y > 0.2) {
        float starIntensity = pow(dir.y - 0.2, 2.0);
        float stars = step(0.998, hash(floor(dir.xz * 500.0)));
        stars *= hash(floor(dir.xz * 500.0 + 0.5)) * 0.5 + 0.5;
        skyColor += float3(stars * starIntensity * 0.8, stars * starIntensity * 0.8, stars * starIntensity * 0.8);
    }
    
    // Mountains on the horizon
    float angle = atan2(dir.x, dir.z);  // Horizontal angle around Y axis
    float2 mountainCoord = float2(angle * 3.0, 0.0);
    
    // Multiple mountain layers for depth
    // Far mountains (blue-gray, misty)
    float mountain1 = fbm(mountainCoord * 1.5 + float2(0.0, 100.0)) * 0.15 + 0.02;
    // Mid mountains (darker)
    float mountain2 = fbm(mountainCoord * 2.5 + float2(50.0, 0.0)) * 0.12 + 0.01;
    // Near mountains/hills (darkest)
    float mountain3 = fbm(mountainCoord * 4.0 + float2(25.0, 50.0)) * 0.08 + 0.005;
    
    // Draw mountains based on vertical direction
    float verticalPos = dir.y;
    
    // Far mountains
    if (verticalPos < mountain1 && verticalPos > -0.1) {
        float fogAmount = smoothstep(0.0, mountain1, verticalPos);
        float3 mountainColor = float3(0.12, 0.15, 0.22);  // Blue-gray
        // Snow caps on far mountains
        float snowLine = mountain1 - 0.03;
        if (verticalPos > snowLine) {
            float snow = smoothstep(snowLine, snowLine + 0.02, verticalPos);
            mountainColor = lerp(mountainColor, float3(0.4, 0.45, 0.5), snow * 0.6);
        }
        skyColor = lerp(mountainColor, skyColor, fogAmount * 0.7);
    }
    
    // Mid mountains
    if (verticalPos < mountain2 && verticalPos > -0.1) {
        float fogAmount = smoothstep(-0.02, mountain2, verticalPos);
        float3 mountainColor = float3(0.08, 0.1, 0.15);  // Darker blue
        // Snow on mid peaks
        float snowLine = mountain2 - 0.02;
        if (verticalPos > snowLine) {
            float snow = smoothstep(snowLine, snowLine + 0.015, verticalPos);
            mountainColor = lerp(mountainColor, float3(0.3, 0.35, 0.4), snow * 0.5);
        }
        skyColor = lerp(mountainColor, skyColor, fogAmount * 0.5);
    }
    
    // Near hills/mountains
    if (verticalPos < mountain3 && verticalPos > -0.1) {
        float fogAmount = smoothstep(-0.03, mountain3, verticalPos);
        float3 mountainColor = float3(0.05, 0.07, 0.1);  // Very dark
        skyColor = lerp(mountainColor, skyColor, fogAmount * 0.3);
    }
    
    // Subtle fog/mist at horizon
    float fog = exp(-abs(dir.y) * 8.0);
    float3 fogColor = float3(0.12, 0.14, 0.2);
    skyColor = lerp(skyColor, fogColor, fog * 0.4);
    
    // Very subtle moving clouds
    float cloudNoise = fbm(float2(angle * 2.0 + time * 0.01, dir.y * 5.0));
    if (dir.y > 0.1 && dir.y < 0.5) {
        float cloudMask = smoothstep(0.1, 0.2, dir.y) * smoothstep(0.5, 0.3, dir.y);
        float clouds = smoothstep(0.4, 0.6, cloudNoise) * cloudMask * 0.15;
        skyColor += float3(clouds, clouds, clouds);
    }
    
    return float4(skyColor, 1.0);
}

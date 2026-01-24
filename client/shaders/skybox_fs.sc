$input v_worldPos

/*
 * Skybox Fragment Shader - bgfx version
 * Procedural sky with sun, clouds, and distant mountains
 */

#include <bgfx_shader.sh>

uniform vec4 u_timeAndSun;  // x = time, yzw = sunDirection

// Simple hash function for noise
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Value noise
float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// FBM for mountains
float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; i++)
    {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

void main()
{
    vec3 dir = normalize(v_worldPos);
    float time = u_timeAndSun.x;
    vec3 sunDirection = u_timeAndSun.yzw;
    
    // Sun direction and sun disk calculation
    vec3 sunDir = normalize(sunDirection);
    float sunAngle = acos(clamp(dot(dir, sunDir), -1.0, 1.0));
    
    // Sun disk (small bright core)
    float sunDiskRadius = 0.02;
    float sunDisk = smoothstep(sunDiskRadius, sunDiskRadius * 0.5, sunAngle);
    
    // Sun corona/glow
    float coronaRadius = 0.15;
    float corona = exp(-sunAngle * sunAngle / (coronaRadius * coronaRadius)) * 0.6;
    
    // Sun glare/rays
    float glareRadius = 0.4;
    float glare = exp(-sunAngle / glareRadius) * 0.3;
    
    // Sun colors
    vec3 sunColor = vec3(1.0, 0.95, 0.85);
    vec3 coronaColor = vec3(1.0, 0.8, 0.5);
    vec3 glareColor = vec3(1.0, 0.9, 0.7);
    
    // Sky gradient
    float horizon = smoothstep(-0.1, 0.3, dir.y);
    float sunHeight = sunDir.y;
    float dayFactor = clamp(sunHeight * 2.0 + 0.5, 0.0, 1.0);
    
    // Day/night sky colors
    vec3 dayTop = vec3(0.2, 0.4, 0.8);
    vec3 dayHorizon = vec3(0.5, 0.6, 0.75);
    vec3 nightTop = vec3(0.02, 0.05, 0.12);
    vec3 nightHorizon = vec3(0.08, 0.1, 0.18);
    
    vec3 skyTop = mix(nightTop, dayTop, dayFactor);
    vec3 skyHorizon = mix(nightHorizon, dayHorizon, dayFactor);
    vec3 skyColor = mix(skyHorizon, skyTop, horizon);
    
    // Add sun and glow
    skyColor += sunDisk * sunColor * 5.0;
    skyColor += corona * coronaColor;
    skyColor += glare * glareColor * (1.0 - horizon);
    
    // Stars at top of sky
    if (dir.y > 0.2)
    {
        float starIntensity = pow(dir.y - 0.2, 2.0);
        float stars = step(0.998, hash(floor(dir.xz * 500.0)));
        stars *= hash(floor(dir.xz * 500.0 + 0.5)) * 0.5 + 0.5;
        skyColor += vec3_splat(stars * starIntensity * 0.8);
    }
    
    // Mountains on horizon
    float angle = atan2(dir.x, dir.z);
    vec2 mountainCoord = vec2(angle * 3.0, 0.0);
    
    // Multiple mountain layers
    float mountain1 = fbm(mountainCoord * 1.5 + vec2(0.0, 100.0)) * 0.15 + 0.02;
    float mountain2 = fbm(mountainCoord * 2.5 + vec2(50.0, 0.0)) * 0.12 + 0.01;
    float mountain3 = fbm(mountainCoord * 4.0 + vec2(25.0, 50.0)) * 0.08 + 0.005;
    
    float verticalPos = dir.y;
    
    // Far mountains
    if (verticalPos < mountain1 && verticalPos > -0.1)
    {
        float fogAmount = smoothstep(0.0, mountain1, verticalPos);
        vec3 mountainColor = vec3(0.12, 0.15, 0.22);
        float snowLine = mountain1 - 0.03;
        if (verticalPos > snowLine)
        {
            float snow = smoothstep(snowLine, snowLine + 0.02, verticalPos);
            mountainColor = mix(mountainColor, vec3(0.4, 0.45, 0.5), snow * 0.6);
        }
        skyColor = mix(mountainColor, skyColor, fogAmount * 0.7);
    }
    
    // Mid mountains
    if (verticalPos < mountain2 && verticalPos > -0.1)
    {
        float fogAmount = smoothstep(-0.02, mountain2, verticalPos);
        vec3 mountainColor = vec3(0.08, 0.1, 0.15);
        float snowLine = mountain2 - 0.02;
        if (verticalPos > snowLine)
        {
            float snow = smoothstep(snowLine, snowLine + 0.015, verticalPos);
            mountainColor = mix(mountainColor, vec3(0.3, 0.35, 0.4), snow * 0.5);
        }
        skyColor = mix(mountainColor, skyColor, fogAmount * 0.5);
    }
    
    // Near hills
    if (verticalPos < mountain3 && verticalPos > -0.1)
    {
        float fogAmount = smoothstep(-0.03, mountain3, verticalPos);
        vec3 mountainColor = vec3(0.05, 0.07, 0.1);
        skyColor = mix(mountainColor, skyColor, fogAmount * 0.3);
    }
    
    // Horizon fog
    float fog = exp(-abs(dir.y) * 8.0);
    vec3 fogColor = vec3(0.12, 0.14, 0.2);
    skyColor = mix(skyColor, fogColor, fog * 0.4);
    
    // Clouds
    float cloudNoise = fbm(vec2(angle * 2.0 + time * 0.01, dir.y * 5.0));
    if (dir.y > 0.1 && dir.y < 0.5)
    {
        float cloudMask = smoothstep(0.1, 0.2, dir.y) * smoothstep(0.5, 0.3, dir.y);
        float clouds = smoothstep(0.4, 0.6, cloudNoise) * cloudMask * 0.15;
        skyColor += vec3_splat(clouds);
    }
    
    gl_FragColor = vec4(skyColor, 1.0);
}

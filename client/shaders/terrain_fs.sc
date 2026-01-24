$input v_texcoord0, v_color0, v_fragPos, v_fogDist, v_shadowCoord, v_normal

/*
 * Terrain Fragment Shader - bgfx version
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_grassTexture, 0);
SAMPLER2D(s_shadowMap, 1);
SAMPLER2D(s_ssaoMap, 2);

uniform vec4 u_fogColor;     // xyz = color
uniform vec4 u_fogParams;    // x = start, y = end
uniform vec4 u_lightDir;     // xyz = direction
uniform vec4 u_flags;        // x = shadowsEnabled, y = ssaoEnabled
uniform vec4 u_screenSize;   // x = width, y = height

float calculateShadow(vec4 shadowCoord)
{
    vec3 projCoords = shadowCoord.xyz / shadowCoord.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0)
    {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    float bias = 0.002;
    
    float shadow = 0.0;
    vec2 texelSize = vec2_splat(1.0) / vec2(4096.0, 4096.0);
    
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            float pcfDepth = texture2D(s_shadowMap, projCoords.xy + vec2(float(x), float(y)) * texelSize).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    
    return shadow;
}

void main()
{
    // Sample the seamless grass texture
    vec4 texColor = texture2D(s_grassTexture, v_texcoord0);
    
    // Use texture color with subtle vertex color variation
    vec3 color = texColor.rgb * mix(vec3_splat(1.0), v_color0.rgb, 0.3);
    
    // Calculate shadow
    float shadow = 0.0;
    if (u_flags.x > 0.5) // shadowsEnabled
    {
        shadow = calculateShadow(v_shadowCoord);
    }
    
    // Get SSAO value
    float ao = 1.0;
    if (u_flags.y > 0.5) // ssaoEnabled
    {
        vec2 screenUV = gl_FragCoord.xy / u_screenSize.xy;
        ao = texture2D(s_ssaoMap, screenUV).r;
    }
    
    // Simple directional lighting with shadow
    vec3 lightDirection = normalize(-u_lightDir.xyz);
    vec3 norm = normalize(v_normal);
    float diff = max(dot(norm, lightDirection), 0.0);
    
    // Lighting calculation
    float ambient = 0.4 * ao;
    float diffuse = diff * 0.6 * (1.0 - shadow * 0.6);
    float light = ambient + diffuse;
    
    // Height-based variation for subtle detail
    light *= 0.9 + 0.1 * sin(v_fragPos.x * 0.01) * cos(v_fragPos.z * 0.01);
    
    color *= light;
    
    // Apply distance fog
    float fogFactor = clamp((v_fogDist - u_fogParams.x) / (u_fogParams.y - u_fogParams.x), 0.0, 1.0);
    fogFactor = 1.0 - exp(-fogFactor * 2.0);
    color = mix(color, u_fogColor.xyz, fogFactor);
    
    gl_FragColor = vec4(color, 1.0);
}

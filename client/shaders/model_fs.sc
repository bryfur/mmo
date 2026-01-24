$input v_fragPos, v_normal, v_texcoord0, v_color0, v_fogDist, v_shadowCoord

/*
 * Model Fragment Shader - bgfx version
 * Renders static 3D models with lighting, fog, shadow mapping, and SSAO
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_shadowMap, 1);
SAMPLER2D(s_ssaoMap, 2);

uniform vec4 u_lightDir;        // xyz = direction, w = unused
uniform vec4 u_lightColor;      // xyz = color, w = unused
uniform vec4 u_ambientColor;    // xyz = ambient, w = unused
uniform vec4 u_tintColor;
uniform vec4 u_fogColor;        // xyz = color, w = unused
uniform vec4 u_fogParams;       // x = start, y = end, z = fogEnabled, w = unused
uniform vec4 u_flags;           // x = hasTexture, y = shadowsEnabled, z = ssaoEnabled, w = unused
uniform vec4 u_screenSize;      // x = width, y = height

// Calculate shadow with PCF soft shadows
float calculateShadow(vec4 shadowCoord, vec3 normal, vec3 lightDirection)
{
    // Perspective divide
    vec3 projCoords = shadowCoord.xyz / shadowCoord.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    // Check if outside shadow map
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0)
    {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    
    // Slope-scaled bias to reduce shadow acne
    float bias = max(0.005 * (1.0 - dot(normal, lightDirection)), 0.001);
    
    // PCF (Percentage-Closer Filtering) for soft shadows
    float shadow = 0.0;
    vec2 texelSize = vec2_splat(1.0) / vec2(4096.0, 4096.0); // Shadow map size
    
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
    vec3 norm = normalize(v_normal);
    vec3 lightDirection = normalize(-u_lightDir.xyz);
    
    // Calculate shadow
    float shadow = 0.0;
    if (u_flags.y > 0.5) // shadowsEnabled
    {
        shadow = calculateShadow(v_shadowCoord, norm, lightDirection);
    }
    
    // Diffuse lighting (reduced when in shadow)
    float diff = max(dot(norm, lightDirection), 0.0);
    vec3 diffuse = diff * u_lightColor.xyz * (1.0 - shadow * 0.7);
    
    // Get SSAO value
    float ao = 1.0;
    if (u_flags.z > 0.5) // ssaoEnabled
    {
        vec2 screenUV = gl_FragCoord.xy / u_screenSize.xy;
        ao = texture2D(s_ssaoMap, screenUV).r;
    }
    
    // Combine lighting with ambient occlusion
    vec3 ambient = u_ambientColor.xyz * ao;
    vec3 lighting = ambient + diffuse;
    
    // Get base color from texture or vertex color
    vec4 baseColor;
    if (u_flags.x > 0.5) // hasTexture
    {
        baseColor = texture2D(s_texColor, v_texcoord0);
    }
    else
    {
        baseColor = v_color0 * u_tintColor;
    }
    
    vec3 result = lighting * baseColor.rgb;
    
    // Slight rim lighting for better visibility
    vec3 viewDir = normalize(-v_fragPos);
    float rim = 1.0 - max(dot(viewDir, norm), 0.0);
    rim = smoothstep(0.6, 1.0, rim);
    result += rim * 0.3 * baseColor.rgb;
    
    // Apply distance fog
    if (u_fogParams.z > 0.5) // fogEnabled
    {
        float fogFactor = clamp((v_fogDist - u_fogParams.x) / (u_fogParams.y - u_fogParams.x), 0.0, 1.0);
        // Use exponential falloff for more natural look
        fogFactor = 1.0 - exp(-fogFactor * 2.0);
        result = mix(result, u_fogColor.xyz, fogFactor);
    }
    
    gl_FragColor = vec4(result, baseColor.a);
}

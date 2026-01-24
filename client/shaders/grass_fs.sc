$input v_worldPos, v_color0, v_shadowCoord, v_fogDist

/*
 * Grass Fragment Shader - bgfx version
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_shadowMap, 0);

uniform vec4 u_fogColor;     // xyz = color
uniform vec4 u_fogParams;    // x = start, y = end
uniform vec4 u_lightDir;     // xyz = direction
uniform vec4 u_flags;        // x = shadowsEnabled

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
    float bias = 0.003;
    
    // Simple shadow check (no PCF for grass to save performance)
    float pcfDepth = texture2D(s_shadowMap, projCoords.xy).r;
    return (currentDepth - bias > pcfDepth) ? 0.5 : 0.0;
}

void main()
{
    vec4 color = v_color0;
    
    // Simple lighting
    vec3 lightDir = normalize(-u_lightDir.xyz);
    float light = 0.6 + 0.4 * max(0.0, dot(vec3(0.0, 1.0, 0.0), lightDir));
    
    // Shadow
    float shadow = 0.0;
    if (u_flags.x > 0.5)
    {
        shadow = calculateShadow(v_shadowCoord);
    }
    light *= (1.0 - shadow * 0.4);
    
    color.rgb *= light;
    
    // Fog
    float fogFactor = clamp((v_fogDist - u_fogParams.x) / (u_fogParams.y - u_fogParams.x), 0.0, 1.0);
    fogFactor = 1.0 - exp(-fogFactor * 2.0);
    color.rgb = mix(color.rgb, u_fogColor.xyz, fogFactor);
    
    gl_FragColor = color;
}

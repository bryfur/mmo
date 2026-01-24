$input v_fragPos, v_normal, v_height, v_distance

/*
 * Mountains Fragment Shader - bgfx version
 */

#include <bgfx_shader.sh>

uniform vec4 u_fogColor;   // xyz = color
uniform vec4 u_fogParams;  // x = density, y = start

void main()
{
    // Base mountain color based on height
    vec3 baseColor;
    float h = v_height;
    
    vec3 darkRock = vec3(0.15, 0.13, 0.12);
    vec3 midRock = vec3(0.25, 0.23, 0.22);
    vec3 lightRock = vec3(0.35, 0.33, 0.32);
    vec3 snow = vec3(0.85, 0.88, 0.92);
    
    if (h < 0.3)
    {
        baseColor = mix(darkRock, midRock, h / 0.3);
    }
    else if (h < 0.6)
    {
        baseColor = mix(midRock, lightRock, (h - 0.3) / 0.3);
    }
    else if (h < 0.75)
    {
        baseColor = mix(lightRock, snow, (h - 0.6) / 0.15);
    }
    else
    {
        baseColor = snow;
    }
    
    // Simple directional lighting
    vec3 lightDir = normalize(vec3(-0.3, -1.0, -0.5));
    vec3 norm = normalize(v_normal);
    float diff = max(dot(norm, -lightDir), 0.0) * 0.6 + 0.4;
    
    vec3 litColor = baseColor * diff;
    
    // Atmospheric fog based on distance
    float fogDensity = u_fogParams.x;
    float fogStart = u_fogParams.y;
    float fogFactor = 1.0 - exp(-fogDensity * max(0.0, v_distance - fogStart));
    fogFactor = clamp(fogFactor, 0.0, 0.95);
    
    // Add subtle blue tint to distant mountains (atmospheric scattering)
    vec3 atmosphereColor = mix(u_fogColor.xyz, vec3(0.4, 0.5, 0.7), 0.3);
    vec3 finalColor = mix(litColor, atmosphereColor, fogFactor);
    
    gl_FragColor = vec4(finalColor, 1.0);
}

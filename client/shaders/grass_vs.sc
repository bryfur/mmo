$input a_position, a_texcoord0
$output v_worldPos, v_color0, v_shadowCoord, v_fogDist

/*
 * Grass Vertex Shader - bgfx version
 * Procedural grass with wind animation
 */

#include <bgfx_shader.sh>

uniform vec4 u_cameraPos;
uniform vec4 u_windParams;       // x = magnitude, y = wavelength, z = period, w = time
uniform vec4 u_grassParams;      // x = spacing, y = viewDistance, z = worldWidth, w = worldHeight
uniform mat4 u_lightSpaceMatrix;

// Hash function for procedural placement
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Height function (simplified terrain sampling)
float getTerrainHeight(vec2 pos, float worldW, float worldH)
{
    // Simple multi-octave noise for terrain height
    float h = 0.0;
    vec2 p = pos / vec2(worldW, worldH);
    h += sin(p.x * 3.14159 * 2.0) * cos(p.y * 3.14159 * 2.0) * 20.0;
    h += sin(p.x * 6.28 + 1.0) * cos(p.y * 6.28 + 2.0) * 10.0;
    return h;
}

void main()
{
    float spacing = u_grassParams.x;
    float viewDist = u_grassParams.y;
    float worldW = u_grassParams.z;
    float worldH = u_grassParams.w;
    float time = u_windParams.w;
    
    // Calculate grid position based on camera
    vec2 gridOffset = floor(u_cameraPos.xz / spacing) * spacing;
    vec2 localPos = a_texcoord0 * spacing + gridOffset;
    
    // Add some randomness to position
    float randOffset = hash(localPos) * spacing * 0.8;
    localPos += vec2(hash(localPos + 0.5) - 0.5, hash(localPos + 1.0) - 0.5) * spacing * 0.4;
    
    // Check if within world bounds
    if (localPos.x < 0.0 || localPos.x > worldW || localPos.y < 0.0 || localPos.y > worldH)
    {
        // Move offscreen
        gl_Position = vec4(0.0, 0.0, -10.0, 1.0);
        return;
    }
    
    // Check distance from camera for LOD
    float dist = length(localPos - u_cameraPos.xz);
    if (dist > viewDist)
    {
        gl_Position = vec4(0.0, 0.0, -10.0, 1.0);
        return;
    }
    
    // Get terrain height
    float terrainY = getTerrainHeight(localPos, worldW, worldH);
    
    // Grass blade height (varies per blade)
    float bladeHeight = 15.0 + hash(localPos + 2.0) * 20.0;
    
    // Build grass blade position
    float vertexHeight = a_position.y * bladeHeight;
    
    // Wind animation
    float windStrength = u_windParams.x;
    float windWave = sin(localPos.x * 0.02 + time * u_windParams.z) * 
                     cos(localPos.y * 0.02 + time * u_windParams.z * 0.7);
    float windOffset = windStrength * windWave * a_position.y;  // More effect at blade tip
    
    vec3 worldPos = vec3(
        localPos.x + a_position.x * 2.0 + windOffset,
        terrainY + vertexHeight,
        localPos.y + a_position.z * 2.0
    );
    
    v_worldPos = worldPos;
    v_fogDist = dist;
    v_shadowCoord = mul(u_lightSpaceMatrix, vec4(worldPos, 1.0));
    
    // Grass color with variation
    float colorVar = hash(localPos + 3.0);
    v_color0 = vec4(
        0.15 + colorVar * 0.1,
        0.45 + colorVar * 0.2,
        0.1 + colorVar * 0.1,
        1.0 - (dist / viewDist) * 0.3  // Fade at distance
    );
    
    gl_Position = mul(u_viewProj, vec4(worldPos, 1.0));
}

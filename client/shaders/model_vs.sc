$input a_position, a_normal, a_texcoord0, a_color0
$output v_fragPos, v_normal, v_texcoord0, v_color0, v_fogDist, v_shadowCoord

/*
 * Model Vertex Shader - bgfx version
 * Renders static 3D models with lighting, fog, and shadow mapping
 */

#include <bgfx_shader.sh>

uniform mat4 u_lightSpaceMatrix;
uniform vec4 u_cameraPos;

void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    v_fragPos = worldPos.xyz;
    
    // Transform normal to world space
    mat3 normalMatrix = mat3(u_model[0]);
    v_normal = normalize(mul(normalMatrix, a_normal));
    
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    
    // Fog distance from camera
    v_fogDist = length(worldPos.xyz - u_cameraPos.xyz);
    
    // Shadow coordinates
    v_shadowCoord = mul(u_lightSpaceMatrix, worldPos);
    
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}

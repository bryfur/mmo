$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0, v_fragPos, v_fogDist, v_shadowCoord, v_normal

/*
 * Terrain Vertex Shader - bgfx version
 */

#include <bgfx_shader.sh>

uniform mat4 u_lightSpaceMatrix;
uniform vec4 u_cameraPos;

void main()
{
    v_fragPos = a_position;
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    v_fogDist = length(a_position - u_cameraPos.xyz);
    v_shadowCoord = mul(u_lightSpaceMatrix, vec4(a_position, 1.0));
    
    // Terrain normal is generally up
    v_normal = vec3(0.0, 1.0, 0.0);
    
    gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
}

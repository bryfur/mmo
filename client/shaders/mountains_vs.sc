$input a_position, a_normal, a_texcoord0
$output v_fragPos, v_normal, v_height, v_distance

/*
 * Mountains Vertex Shader - bgfx version
 */

#include <bgfx_shader.sh>

uniform vec4 u_cameraPos;

void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    v_fragPos = worldPos.xyz;
    
    mat3 normalMatrix = mat3(u_model[0]);
    v_normal = normalize(mul(normalMatrix, a_normal));
    
    v_height = a_texcoord0;
    v_distance = length(worldPos.xyz - u_cameraPos.xyz);
    
    gl_Position = mul(u_viewProj, worldPos);
}

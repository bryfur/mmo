$input a_position, a_color0
$output v_color0

/*
 * Billboard Vertex Shader - bgfx version
 * For 3D health bars that always face the camera
 */

#include <bgfx_shader.sh>

uniform vec4 u_worldPos;   // xyz = world position of billboard center
uniform vec4 u_sizeOffset; // xy = size, zw = offset

void main()
{
    v_color0 = a_color0;
    
    // Get camera right and up vectors from view matrix
    vec3 cameraRight = vec3(u_view[0][0], u_view[1][0], u_view[2][0]);
    vec3 cameraUp = vec3(u_view[0][1], u_view[1][1], u_view[2][1]);
    
    // Billboard position: expand quad in camera space
    vec3 pos = u_worldPos.xyz 
             + cameraRight * (a_position.x * u_sizeOffset.x + u_sizeOffset.z)
             + cameraUp * (a_position.y * u_sizeOffset.y + u_sizeOffset.w);
    
    gl_Position = mul(u_viewProj, vec4(pos, 1.0));
}

$input a_position
$output v_worldPos

/*
 * Skybox Vertex Shader - bgfx version
 */

#include <bgfx_shader.sh>

void main()
{
    v_worldPos = a_position;
    
    // Remove translation from view matrix for skybox
    mat4 viewRotOnly = u_view;
    viewRotOnly[3][0] = 0.0;
    viewRotOnly[3][1] = 0.0;
    viewRotOnly[3][2] = 0.0;
    
    vec4 pos = mul(u_proj, mul(viewRotOnly, vec4(a_position, 1.0)));
    // Set z to w for maximum depth (skybox always behind everything)
    gl_Position = pos.xyww;
}

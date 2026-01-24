$input a_position, a_color0
$output v_color0

/*
 * UI Vertex Shader - bgfx version
 */

#include <bgfx_shader.sh>

void main()
{
    v_color0 = a_color0;
    gl_Position = mul(u_viewProj, vec4(a_position, 0.0, 1.0));
}

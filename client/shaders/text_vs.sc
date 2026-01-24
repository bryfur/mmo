$input a_position, a_texcoord0
$output v_texcoord0

/*
 * Text Vertex Shader - bgfx version
 */

#include <bgfx_shader.sh>

void main()
{
    v_texcoord0 = a_texcoord0;
    gl_Position = mul(u_viewProj, vec4(a_position, 0.0, 1.0));
}

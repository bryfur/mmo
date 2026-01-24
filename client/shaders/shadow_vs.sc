$input a_position

/*
 * Shadow Depth Vertex Shader - bgfx version
 * Renders depth from light's perspective for shadow mapping
 */

#include <bgfx_shader.sh>

uniform mat4 u_lightSpaceMatrix;

void main()
{
    gl_Position = mul(u_lightSpaceMatrix, mul(u_model[0], vec4(a_position, 1.0)));
}

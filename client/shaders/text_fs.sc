$input v_texcoord0

/*
 * Text Fragment Shader - bgfx version
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

uniform vec4 u_textColor;

void main()
{
    vec4 sampled = texture2D(s_texColor, v_texcoord0);
    gl_FragColor = u_textColor * sampled;
}

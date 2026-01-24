vec3 v_worldPos   : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec4 v_color0     : COLOR0    = vec4(0.2, 0.6, 0.2, 1.0);
vec4 v_shadowCoord: TEXCOORD1 = vec4(0.0, 0.0, 0.0, 0.0);
float v_fogDist   : TEXCOORD2 = 0.0;

vec3 a_position : POSITION;
vec2 a_texcoord0: TEXCOORD0;  // Grid position for instancing offset

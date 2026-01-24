vec3 v_fragPos  : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec3 v_normal   : NORMAL    = vec3(0.0, 1.0, 0.0);
float v_height  : TEXCOORD1 = 0.0;
float v_distance: TEXCOORD2 = 0.0;

vec3 a_position : POSITION;
vec3 a_normal   : NORMAL;
float a_texcoord0: TEXCOORD0;  // Height value

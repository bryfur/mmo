$input a_position, a_normal, a_texcoord0, a_color0, a_indices, a_weight
$output v_fragPos, v_normal, v_texcoord0, v_color0, v_fogDist, v_shadowCoord

/*
 * Skinned Model Vertex Shader - bgfx version
 * Renders animated 3D models with skeletal animation, lighting, fog, and shadow mapping
 */

#include <bgfx_shader.sh>

#define MAX_BONES 64

uniform mat4 u_lightSpaceMatrix;
uniform vec4 u_cameraPos;
uniform vec4 u_skinningParams; // x = useSkinning
uniform mat4 u_boneMatrices[MAX_BONES];

void main()
{
    vec4 localPos = vec4(a_position, 1.0);
    vec4 localNormal = vec4(a_normal, 0.0);
    
    if (u_skinningParams.x > 0.5) // useSkinning
    {
        // Get bone indices as integers
        ivec4 joints = ivec4(a_indices);
        vec4 weights = a_weight;
        
        // Apply skeletal animation
        mat4 skinMatrix = 
            weights.x * u_boneMatrices[joints.x] +
            weights.y * u_boneMatrices[joints.y] +
            weights.z * u_boneMatrices[joints.z] +
            weights.w * u_boneMatrices[joints.w];
        
        localPos = mul(skinMatrix, vec4(a_position, 1.0));
        localNormal = mul(skinMatrix, vec4(a_normal, 0.0));
    }
    
    vec4 worldPos = mul(u_model[0], localPos);
    v_fragPos = worldPos.xyz;
    
    // Transform normal to world space
    mat3 normalMatrix = mat3(u_model[0]);
    v_normal = normalize(mul(normalMatrix, localNormal.xyz));
    
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    
    // Fog distance from camera
    v_fogDist = length(worldPos.xyz - u_cameraPos.xyz);
    
    // Shadow coordinates  
    v_shadowCoord = mul(u_lightSpaceMatrix, worldPos);
    
    gl_Position = mul(u_viewProj, worldPos);
}

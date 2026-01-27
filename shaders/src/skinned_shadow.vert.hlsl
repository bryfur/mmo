/**
 * Skinned Shadow Depth Vertex Shader - SDL3 GPU API
 * 
 * Renders skinned geometry to shadow map with skeletal animation support.
 */

#define MAX_BONES 64

// Vertex input
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;      // Unused but may be in vertex format
    float2 texCoord : TEXCOORD0; // Unused but may be in vertex format
    float4 color : COLOR0;       // Unused but may be in vertex format
    int4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

// Vertex output
struct VSOutput {
    float4 position : SV_Position;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer ShadowUniforms {
    float4x4 lightSpaceMatrix;
    float4x4 model;
    int useSkinning;
    float3 _padding0;
};

// Uniform buffer slot 1 - Bone matrices (binding 1 in set 1)
[[vk::binding(1, 1)]]
cbuffer BoneUniforms {
    float4x4 boneMatrices[MAX_BONES];
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    float4 localPos = float4(input.position, 1.0);
    
    if (useSkinning == 1) {
        float4x4 skinMatrix = 
            input.weights.x * boneMatrices[input.joints.x] +
            input.weights.y * boneMatrices[input.joints.y] +
            input.weights.z * boneMatrices[input.joints.z] +
            input.weights.w * boneMatrices[input.joints.w];
        
        localPos = mul(skinMatrix, float4(input.position, 1.0));
    }
    
    output.position = mul(lightSpaceMatrix, mul(model, localPos));
    
    return output;
}

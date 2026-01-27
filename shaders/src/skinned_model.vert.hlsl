/**
 * Skinned Model Vertex Shader - SDL3 GPU API
 * 
 * Transforms 3D model vertices with skeletal animation support.
 * Applies bone matrices for skinned mesh deformation.
 */

#define MAX_BONES 64

// Vertex input
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR0;
    int4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
    float4 fragPosLightSpace : TEXCOORD5;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer TransformUniforms {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float3 cameraPos;
    float _padding0;
    float4x4 lightSpaceMatrix;
    float4x4 normalMatrix;  // Pre-computed inverse-transpose of model matrix (CPU-side)
    int useSkinning;
    float3 _padding1;
};

// Uniform buffer slot 1 - Bone matrices (binding 1 in set 1)
[[vk::binding(1, 1)]]
cbuffer BoneUniforms {
    float4x4 boneMatrices[MAX_BONES];
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    float4 localPos = float4(input.position, 1.0);
    float4 localNormal = float4(input.normal, 0.0);
    
    if (useSkinning == 1) {
        // Apply skeletal animation
        float4x4 skinMatrix = 
            input.weights.x * boneMatrices[input.joints.x] +
            input.weights.y * boneMatrices[input.joints.y] +
            input.weights.z * boneMatrices[input.joints.z] +
            input.weights.w * boneMatrices[input.joints.w];
        
        localPos = mul(skinMatrix, float4(input.position, 1.0));
        localNormal = mul(skinMatrix, float4(input.normal, 0.0));
    }
    
    float4 worldPos = mul(model, localPos);
    output.fragPos = worldPos.xyz;
    
    // Transform normal to world space using pre-computed normal matrix
    float3x3 normalMat3x3 = (float3x3)normalMatrix;
    output.normal = normalize(mul(normalMat3x3, localNormal.xyz));
    
    output.texCoord = input.texCoord;
    output.vertexColor = input.color;
    output.fogDistance = length(worldPos.xyz - cameraPos);
    output.fragPosLightSpace = mul(lightSpaceMatrix, worldPos);
    
    output.position = mul(projection, mul(view, worldPos));
    
    return output;
}

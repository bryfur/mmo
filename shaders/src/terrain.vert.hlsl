/**
 * Terrain Vertex Shader - SDL3 GPU API
 * 
 * Transforms terrain vertices with shadow and fog support.
 */

// Vertex input
struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR0;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 vertexColor : TEXCOORD1;
    float3 fragPos : TEXCOORD2;
    float fogDistance : TEXCOORD3;
    float4 fragPosLightSpace : TEXCOORD4;
    float3 normal : TEXCOORD5;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer TransformUniforms {
    float4x4 view;
    float4x4 projection;
    float3 cameraPos;
    float _padding0;
    float4x4 lightSpaceMatrix;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    output.fragPos = input.position;
    output.texCoord = input.texCoord;
    output.vertexColor = input.color;
    output.fogDistance = length(input.position - cameraPos);
    output.fragPosLightSpace = mul(lightSpaceMatrix, float4(input.position, 1.0));
    
    // Calculate normal from height differences (approximation for terrain)
    // For a proper implementation, normals should come from vertex data
    output.normal = float3(0.0, 1.0, 0.0);
    
    output.position = mul(projection, mul(view, float4(input.position, 1.0)));
    
    return output;
}

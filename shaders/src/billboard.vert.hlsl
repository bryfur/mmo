/**
 * Billboard Vertex Shader - SDL3 GPU API
 * 
 * 3D billboards that always face the camera (for health bars, nameplates, etc).
 */

// Vertex input
struct VSInput {
    float3 position : POSITION;
    float4 color : COLOR0;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float4 vertexColor : TEXCOORD0;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer BillboardUniforms {
    float4x4 view;
    float4x4 projection;
    float3 worldPos;      // World position of billboard center
    float _padding0;
    float2 size;          // Size in world units
    float2 offset;        // Offset from center in local billboard space
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    output.vertexColor = input.color;
    
    // Get camera right and up vectors from view matrix
    float3 cameraRight = float3(view[0][0], view[1][0], view[2][0]);
    float3 cameraUp = float3(view[0][1], view[1][1], view[2][1]);
    
    // Billboard position: expand quad in camera space
    float3 pos = worldPos 
             + cameraRight * (input.position.x * size.x + offset.x)
             + cameraUp * (input.position.y * size.y + offset.y);
    
    output.position = mul(projection, mul(view, float4(pos, 1.0)));
    
    return output;
}

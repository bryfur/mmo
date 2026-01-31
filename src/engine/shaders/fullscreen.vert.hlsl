/**
 * Fullscreen Triangle Vertex Shader
 * Generates a screen-covering triangle from SV_VertexID (no vertex buffers needed).
 * vertexID 0: (-1, -1), vertexID 1: (3, -1), vertexID 2: (-1, 3)
 */

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    float x = (vertexID == 1) ? 3.0 : -1.0;
    float y = (vertexID == 2) ? 3.0 : -1.0;

    output.position = float4(x, y, 0.0, 1.0);
    output.texCoord = float2((x + 1.0) * 0.5, (1.0 - y) * 0.5);

    return output;
}

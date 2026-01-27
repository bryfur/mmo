/**
 * Grid Debug Fragment Shader - SDL3 GPU API
 *
 * Simple passthrough for grid line colors.
 */

struct PSInput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target {
    return input.color;
}

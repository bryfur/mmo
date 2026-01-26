/**
 * SSAO Blur Fragment Shader - SDL3 GPU API
 * 
 * Simple box blur to smooth SSAO noise.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float2 texCoords : TEXCOORD0;
};

// Texture and sampler bindings
Texture2D ssaoInput : register(t0);
SamplerState ssaoSampler : register(s0);

float PSMain(PSInput input) : SV_Target {
    float2 texelSize;
    float width, height;
    ssaoInput.GetDimensions(width, height);
    texelSize = 1.0 / float2(width, height);
    
    float result = 0.0;
    
    [unroll]
    for (int x = -2; x <= 2; ++x) {
        [unroll]
        for (int y = -2; y <= 2; ++y) {
            float2 offset = float2(float(x), float(y)) * texelSize;
            result += ssaoInput.Sample(ssaoSampler, input.texCoords + offset).r;
        }
    }
    
    return result / 25.0;
}

/**
 * SSAO (Screen Space Ambient Occlusion) Fragment Shader - SDL3 GPU API
 * 
 * Calculates ambient occlusion from G-buffer data.
 */

#define KERNEL_SIZE 32

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float2 texCoords : TEXCOORD0;
};

// Uniform buffer slot 0 - SSAO parameters
cbuffer SSAOUniforms : register(b0) {
    float4x4 projection;
    float2 screenSize;
    float radius;
    float bias;
    float3 samples[64];  // Pre-computed hemisphere samples
};

// Texture and sampler bindings
Texture2D gPosition : register(t0);
SamplerState gPositionSampler : register(s0);

Texture2D gNormal : register(t1);
SamplerState gNormalSampler : register(s1);

Texture2D texNoise : register(t2);
SamplerState texNoiseSampler : register(s2);

float PSMain(PSInput input) : SV_Target {
    float2 noiseScale = screenSize / 4.0;
    
    float3 fragPos = gPosition.Sample(gPositionSampler, input.texCoords).xyz;
    float3 normal = normalize(gNormal.Sample(gNormalSampler, input.texCoords).rgb);
    float3 randomVec = normalize(texNoise.Sample(texNoiseSampler, input.texCoords * noiseScale).xyz);
    
    // If position is at far plane or invalid, no occlusion
    float fragDist = length(fragPos);
    if (fragDist < 0.1 || fragDist > 1000.0) {
        return 1.0;
    }
    
    // Create TBN change-of-basis matrix
    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);
    
    float occlusion = 0.0;
    
    [unroll]
    for (int i = 0; i < KERNEL_SIZE; ++i) {
        // Get sample position in view space
        float3 samplePos = mul(TBN, samples[i]);
        samplePos = fragPos + samplePos * radius;
        
        // Project sample position to screen space
        float4 offset = float4(samplePos, 1.0);
        offset = mul(projection, offset);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;
        // SDL3 GPU uses Y-down in texture coordinates
        offset.y = 1.0 - offset.y;
        
        // Get sample depth
        float sampleDepth = gPosition.Sample(gPositionSampler, offset.xy).z;
        
        // Range check and accumulate
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }
    
    occlusion = 1.0 - (occlusion / float(KERNEL_SIZE));
    return pow(occlusion, 2.0);  // Increase contrast
}

/**
 * Shadow Depth Fragment Shader - SDL3 GPU API
 * 
 * Minimal fragment shader for shadow map rendering.
 * Depth is written automatically by the GPU.
 */

// Fragment input
struct PSInput {
    float4 position : SV_Position;
};

// Empty output - depth is written automatically
void PSMain(PSInput input) {
    // Depth is written automatically
    // No color output needed for shadow map
}

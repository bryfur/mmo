/**
 * Shadow Depth Fragment Shader - SDL3 GPU API
 * 
 * Minimal fragment shader for shadow map rendering.
 * This is a depth-only pass - depth is written automatically by the GPU.
 * 
 * NOTE: For SDL3 GPU depth-only rendering, ensure the pipeline is configured with:
 * - No color attachment (or color write mask disabled)
 * - Depth attachment enabled with depth write
 * The void return type indicates no color output is produced.
 */

// Fragment input
struct PSInput {
    float4 position : SV_Position;
};

// Depth-only pass - no color output needed
// Depth buffer is written automatically based on SV_Position.z
void PSMain(PSInput input) {
    // Intentionally empty - depth is written automatically by the GPU
    // This shader exists to satisfy the pipeline requirement for a fragment shader
}

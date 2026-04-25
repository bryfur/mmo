/**
 * Grass Vertex Shader - SDL3 GPU API (chunked, AAA-quality)
 *
 * Design:
 *  - CPU frustum-culls grass chunks and uploads visible chunk origins
 *    into a storage buffer. Each chunk contains BLADES_PER_CHUNK^2 blades.
 *  - Vertex shader derives per-blade position from (chunk_origin, blade_idx),
 *    performs ONE heightmap sample (not 5), and applies a SMOOTH LOD fade
 *    over the last 30% of view distance to eliminate the discrete band
 *    phase-in that plagued the previous version.
 *  - Billboard facing + blade curving + layered wind + base AO retained.
 *
 * Perf target: dispatch ~100-300 visible chunks × 64 blades = 6400-19200
 * instances per frame (was 640,000+), with no per-instance shader culling.
 */

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
    uint instanceID : SV_InstanceID;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
    float4 color : TEXCOORD3;
    float viewDepth : TEXCOORD4;
};

// Per-frame uniforms
[[vk::binding(0, 1)]]
cbuffer GrassUniforms {
    float4x4 viewProjection;
    float3 cameraPos;          // world camera position (xz used)
    float time;
    float3 _pad0;
    float grassViewDistance;
    float2 windDirection;
    float windStrength;
    float chunkSize;           // world units per chunk side
    float bladeSpacing;        // world units between blades within a chunk
    uint bladesPerChunkSide;   // sqrt of blades per chunk
    uint bladesPerChunkSq;     // bladesPerChunkSide * bladesPerChunkSide
    float _pad1;
    float heightmapOriginX;
    float heightmapOriginZ;
    float heightmapWorldSize;
    float heightmapMinHeight;
    float heightmapMaxHeight;
    float3 _pad2;
};

// Heightmap texture + sampler (vertex stage set 0 = samplers first).
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
Texture2D heightmapTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState heightmapSampler;

// Per-visible-chunk origins (xz = world origin). Filled by CPU each frame.
// Within vertex set 0, storage buffers come after samplers (1 sampler -> binding 1).
[[vk::binding(1, 0)]]
StructuredBuffer<float4> ChunkOrigins;

uint murmurHash(uint seed) {
    seed ^= seed >> 16;
    seed *= 0x85ebca6bu;
    seed ^= seed >> 13;
    seed *= 0xc2b2ae35u;
    seed ^= seed >> 16;
    return seed;
}

uint murmurHash2(uint a, uint b) {
    return murmurHash(a ^ (b * 0x9e3779b9u));
}

float hashFloat(int2 p) {
    uint h = murmurHash2(asuint(p.x), asuint(p.y));
    return float(h & 0x00FFFFFFu) / float(0x01000000u);
}

float2 hashFloat2(int2 p) {
    uint h1 = murmurHash2(asuint(p.x), asuint(p.y));
    uint h2 = murmurHash(h1);
    return float2(
        float(h1 & 0x00FFFFFFu) / float(0x01000000u),
        float(h2 & 0x00FFFFFFu) / float(0x01000000u)
    );
}

float sampleTerrainHeight(float2 worldXZ) {
    float2 hmUV = float2(
        (worldXZ.x - heightmapOriginX) / heightmapWorldSize,
        (worldXZ.y - heightmapOriginZ) / heightmapWorldSize
    );
    hmUV = saturate(hmUV);
    float raw = heightmapTexture.SampleLevel(heightmapSampler, hmUV, 0).r;
    return raw * (heightmapMaxHeight - heightmapMinHeight) + heightmapMinHeight;
}

VSOutput VSMain(VSInput input) {
    VSOutput output;

    // Decode: which chunk + which blade in the chunk.
    uint chunk_idx = input.instanceID / bladesPerChunkSq;
    uint blade_idx = input.instanceID % bladesPerChunkSq;

    float2 chunk_origin = ChunkOrigins[chunk_idx].xz;

    uint bx = blade_idx % bladesPerChunkSide;
    uint bz = blade_idx / bladesPerChunkSide;

    // Blade cell within chunk (regular grid).
    float2 cellPos = chunk_origin + float2((float)bx, (float)bz) * bladeSpacing + 0.5 * bladeSpacing;

    // Deterministic per-blade seed from world-space cell coordinate.
    int2 seed = int2(floor(cellPos / max(bladeSpacing, 0.001)));

    // Natural jitter within cell (visual variety, not density tricks).
    float2 jitter = (hashFloat2(seed) - 0.5) * bladeSpacing * 1.0;
    float2 worldXZ = cellPos + jitter;

    // Distance from camera for LOD.
    float2 toCell = worldXZ - cameraPos.xz;
    float dist = length(toCell);

    // === SMOOTH LOD FADE ===
    // Blades smoothly shrink (height) and fade (width) over the last 30% of
    // view distance. No discrete bands, no pop-in. Beyond view distance
    // blade_scale = 0 which collapses to a degenerate triangle (GPU clips).
    float fade_start = grassViewDistance * 0.70;
    float fade_t = saturate((dist - fade_start) / (grassViewDistance - fade_start));
    float lod_scale = 1.0 - fade_t;

    // Clump density via continuous attenuation (not threshold). Sparser clumps
    // look natural without pop-in.
    int2 clumpSeed = int2(floor(cellPos / (bladeSpacing * 6.0)));
    float clump = hashFloat(clumpSeed + int2(42, 17));
    float density = 0.6 + 0.4 * clump;  // attenuates blade height, never fully culls
    lod_scale *= density;

    // Single heightmap sample. Previous version used 5 samples to compute
    // slope — too expensive; we use a constant up-vector (terrain is gentle
    // enough in this game that per-blade slope tilt adds little).
    float terrainY = sampleTerrainHeight(worldXZ);
    float3 bladeUp = float3(0.0, 1.0, 0.0);

    // Per-instance blade properties.
    float h1 = hashFloat(seed);
    float h2 = hashFloat(seed + int2(1, 0));
    float h3 = hashFloat(seed + int2(3, 3));

    float bladeHeight = (2.5 + h2 * 4.0 + h3 * h3 * 4.0) * lod_scale;
    float bladeWidth  = (0.6 + hashFloat(seed + int2(0, 1)) * 0.5);

    // === BILLBOARD FACING ===
    // Align blade "right" perpendicular to view direction from camera to blade.
    // This keeps blades facing camera without the previous sliver issue.
    float3 toBlade = float3(worldXZ.x - cameraPos.x, 0.0, worldXZ.y - cameraPos.z);
    float toBladeLen = max(length(toBlade), 0.001);
    float3 viewDirXZ = toBlade / toBladeLen;
    float3 bladeRight = normalize(cross(bladeUp, viewDirXZ));

    // Per-blade rotation variation (~+/-20 deg) for natural variety.
    float rotOffset = (h1 - 0.5) * 0.7;
    float cosR = cos(rotOffset);
    float sinR = sin(rotOffset);
    bladeRight = normalize(bladeRight * cosR + cross(bladeUp, bladeRight) * sinR);

    float3 bladeFwd = normalize(cross(bladeRight, bladeUp));

    // Transform vertex into blade frame.
    float3 localPos = bladeRight * (input.position.x * bladeWidth)
                    + bladeUp    * (input.position.y * bladeHeight)
                    + bladeFwd   * (input.position.z * bladeWidth);

    // === BLADE CURVE ===
    float heightT = input.position.y;
    float curveMagnitude = 0.15 * bladeHeight;
    float2 curveDir = normalize(hashFloat2(seed + int2(5, 7)) - 0.5);
    float curveAmount = heightT * heightT * curveMagnitude;
    float3 curveOffset = float3(curveDir.x, 0.0, curveDir.y) * curveAmount;

    // === LAYERED WIND ===
    float windFactor = heightT;  // wind affects tip more than base
    float p1 = time * 2.0 + worldXZ.x * 0.10 + worldXZ.y * 0.10;
    float p2 = time * 3.7 + worldXZ.x * 0.07 - worldXZ.y * 0.13;
    float windWave = sin(p1) * 0.7 + sin(p2) * 0.3;
    float2 windOffset = windDirection * windStrength * windFactor * windWave;

    float3 worldPos = float3(worldXZ.x, terrainY, worldXZ.y) + localPos + curveOffset;
    worldPos.x += windOffset.x;
    worldPos.z += windOffset.y;

    // Base AO: darken blade base, lighten tip.
    float ao = lerp(0.45, 1.0, heightT);
    float4 vertColor = input.color;
    vertColor.rgb *= ao;

    output.worldPos = worldPos;
    output.normal = bladeFwd;
    output.texCoord = input.texCoord;
    output.color = vertColor;
    output.viewDepth = length(worldPos - cameraPos);
    output.position = mul(viewProjection, float4(worldPos, 1.0));

    return output;
}

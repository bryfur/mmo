/**
 * Grass Vertex Shader - SDL3 GPU API (Full GPU instanced)
 *
 * Single blade mesh instanced across a grid around the camera.
 * Per-instance position derived from SV_InstanceID.
 * Terrain height sampled from heightmap texture.
 *
 * Features: billboard facing, blade curving, distance-based width expansion,
 * layered wind, slope-aware tilt, distance-based density culling, base AO.
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

// Uniform buffer (set 1 for SDL3 GPU vertex uniforms)
[[vk::binding(0, 1)]]
cbuffer GrassUniforms {
    float4x4 viewProjection;
    float3 cameraGrid;
    float time;
    float windStrength;
    float grassSpacing;
    float grassViewDistance;
    int gridRadius;
    float2 windDirection;
    float heightmapOriginX;
    float heightmapOriginZ;
    float heightmapWorldSize;
    float heightmapMinHeight;
    float heightmapMaxHeight;
    float worldWidth;
    float worldHeight;
    float _grassPad0;
    float2 cameraForward;
    // New fields
    float3 cameraRight;
    float curveFactor;
    float widthExpansionMax;
    float widthExpansionStart;
    float fullDensityDistance;
    float heightmapTexelSize;
};

// Heightmap texture + sampler (set 0 for SDL3 GPU vertex samplers)
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
Texture2D heightmapTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState heightmapSampler;

// Murmur-inspired hash for better distribution and performance
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

// Returns a float in [0, 1) from two integer coordinates
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

// Sample terrain height at a world XZ position
float sampleTerrainHeight(float2 worldXZ) {
    float2 hmUV = float2(
        (worldXZ.x - heightmapOriginX) / heightmapWorldSize,
        (worldXZ.y - heightmapOriginZ) / heightmapWorldSize
    );
    hmUV = saturate(hmUV);
    float rawHeight = heightmapTexture.SampleLevel(heightmapSampler, hmUV, 0).r;
    return rawHeight * (heightmapMaxHeight - heightmapMinHeight) + heightmapMinHeight;
}

VSOutput VSMain(VSInput input) {
    VSOutput output;

    int gridWidth = 2 * gridRadius + 1;
    int gx = (int)(input.instanceID % (uint)gridWidth) - gridRadius;
    int gz = (int)(input.instanceID / (uint)gridWidth) - gridRadius;

    // World position for this grid cell
    float2 cellPos = float2(cameraGrid.x, cameraGrid.z) + float2(gx, gz) * grassSpacing;

    // Integer seed from grid cell for deterministic properties
    int2 seed = int2(floor(cellPos / grassSpacing));

    // Density: cull some cells for sporadic coverage
    float density = hashFloat(seed + int2(7, 13));
    // Use a threshold curve so grass forms clumps
    int2 clumpSeed = int2(floor(cellPos / (grassSpacing * 5.0)));
    float clumpNoise = hashFloat(clumpSeed + int2(42, 17));
    bool culled = density > (clumpNoise * 0.85 + 0.15);

    // Jitter within cell
    float2 jitter = (hashFloat2(seed) - 0.5) * grassSpacing * 1.2;
    float2 worldXZ = cellPos + jitter;

    // World bounds check
    float margin = 50.0;
    float2 townCenter = float2(worldWidth * 0.5, worldHeight * 0.5);
    bool outOfBounds = worldXZ.x < margin || worldXZ.x > worldWidth - margin ||
                       worldXZ.y < margin || worldXZ.y > worldHeight - margin;
    bool inTown = abs(worldXZ.x - townCenter.x) < 200.0 && abs(worldXZ.y - townCenter.y) < 200.0;

    // Distance from camera
    float2 toCell = worldXZ - float2(cameraGrid.x, cameraGrid.z);
    float dist = length(toCell);
    bool tooFar = dist > grassViewDistance;

    // Cull blades behind camera
    bool behindCamera = dist > 50.0 && dot(normalize(toCell), cameraForward) < -0.2;

    // Distance-based density reduction:
    // Beyond fullDensityDistance, halve density at each distance band.
    // d = 1 << floor(distance / fullDensityDistance), cull if hash % d != 0
    bool distanceCulled = false;
    if (fullDensityDistance > 0.0 && dist > fullDensityDistance) {
        uint band = (uint)floor(dist / fullDensityDistance);
        uint d = 1u << min(band, 5u); // cap at 32x reduction
        uint distHash = murmurHash2(asuint(seed.x), asuint(seed.y));
        distanceCulled = (distHash % d) != 0u;
    }

    if (outOfBounds || inTown || tooFar || culled || behindCamera || distanceCulled) {
        output.position = float4(0, 0, 0, 0);
        output.normal = float3(0, 1, 0);
        output.texCoord = float2(0, 0);
        output.worldPos = float3(0, 0, 0);
        output.color = float4(0, 0, 0, 0);
        output.viewDepth = 0;
        return output;
    }

    // Sample heightmap for terrain Y
    float terrainY = sampleTerrainHeight(worldXZ);

    // --- Slope-aware grass direction ---
    // Sample neighboring heights to compute terrain normal
    float texelWorld = heightmapTexelSize;
    float hL = sampleTerrainHeight(worldXZ + float2(-texelWorld, 0.0));
    float hR = sampleTerrainHeight(worldXZ + float2( texelWorld, 0.0));
    float hD = sampleTerrainHeight(worldXZ + float2(0.0, -texelWorld));
    float hU = sampleTerrainHeight(worldXZ + float2(0.0,  texelWorld));

    float3 terrainNormal = normalize(float3(hL - hR, 2.0 * texelWorld, hD - hU));

    // Build a TBN-like frame from the terrain normal (blade "up" = terrain normal)
    float3 bladeUp = terrainNormal;

    // Per-instance properties from hash
    float h1 = hashFloat(seed);
    float h2 = hashFloat(seed + int2(1, 0));
    float h5 = hashFloat(seed + int2(3, 3));

    float heightMult = h5 * h5;
    float bladeHeight = 3.0 + h2 * 6.0 + heightMult * 8.0;
    float bladeWidth = 0.8 + hashFloat(seed + int2(0, 1)) * 0.6;

    // --- Distance-based width expansion ---
    // Far-away blades get wider to maintain visual density and avoid shimmer
    float distFactor = saturate((dist - widthExpansionStart) / (grassViewDistance - widthExpansionStart));
    bladeWidth += widthExpansionMax * distFactor;

    // --- Billboard facing camera ---
    // Compute a right-tangent perpendicular to camera forward and blade up direction.
    // This ensures the blade always faces the camera, eliminating thin slivers.
    float3 camFwd3 = float3(cameraForward.x, 0.0, cameraForward.y);
    float3 bladeRight = normalize(cross(bladeUp, camFwd3));
    // Add a small per-instance rotation offset to avoid uniformity
    float rotOffset = (h1 - 0.5) * 0.6; // +/- ~17 degrees variation
    float cosR = cos(rotOffset);
    float sinR = sin(rotOffset);
    // Rotate bladeRight around bladeUp by rotOffset
    bladeRight = bladeRight * cosR + cross(bladeUp, bladeRight) * sinR;
    bladeRight = normalize(bladeRight);

    // Recompute a forward direction from up and right
    float3 bladeFwd = normalize(cross(bladeRight, bladeUp));

    // Transform blade vertex using the billboard frame
    // position.x = width offset, position.y = height along blade
    float3 localPos = bladeRight * (input.position.x * bladeWidth)
                    + bladeUp * (input.position.y * bladeHeight)
                    + bladeFwd * (input.position.z * bladeWidth);

    // --- Blade curving ---
    // Offset the blade tip in a random XZ direction, scaled by height^2
    float heightT = input.position.y; // 0 at base, 1 at tip (in local blade space)
    float curveMagnitude = curveFactor * bladeHeight;
    float2 curveDir = normalize(hashFloat2(seed + int2(5, 7)) - 0.5);
    float curveAmount = heightT * heightT * curveMagnitude;
    float3 curveOffset = float3(curveDir.x, 0.0, curveDir.y) * curveAmount;

    // --- Better wind ---
    // Two layered sine waves at different frequencies for organic motion
    float windFactor = heightT; // wind affects tip more
    float windPhase1 = time * 2.0 + worldXZ.x * 0.1 + worldXZ.y * 0.1;
    float windPhase2 = time * 3.7 + worldXZ.x * 0.07 - worldXZ.y * 0.13;
    float windWave = sin(windPhase1) * 0.7 + sin(windPhase2) * 0.3;
    float2 windOffset = windDirection * windStrength * windFactor * windWave;

    // Final world position
    float3 worldPos = float3(worldXZ.x, terrainY, worldXZ.y) + localPos + curveOffset;
    worldPos.x += windOffset.x;
    worldPos.z += windOffset.y;

    // Compute normal: tilt blade normal slightly toward the camera for better lighting
    float3 bladeNormal = bladeFwd;

    // --- AO at blade base ---
    // Darken the bottom of each blade using a lerp based on texCoord.y
    float ao = lerp(0.45, 1.0, heightT);
    float4 vertColor = input.color;
    vertColor.rgb *= ao;

    output.worldPos = worldPos;
    output.normal = bladeNormal;
    output.texCoord = input.texCoord;
    output.color = vertColor;
    output.viewDepth = length(worldPos - cameraGrid);
    output.position = mul(viewProjection, float4(worldPos, 1.0));

    return output;
}

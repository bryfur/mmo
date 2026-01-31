/**
 * Grass Vertex Shader - SDL3 GPU API (Full GPU instanced)
 *
 * Single blade mesh instanced across a grid around the camera.
 * Per-instance position derived from SV_InstanceID.
 * Terrain height sampled from heightmap texture.
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
    float2 cameraForward;
};

// Heightmap texture + sampler (set 0 for SDL3 GPU vertex samplers)
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
Texture2D heightmapTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState heightmapSampler;

// Hash functions for deterministic per-instance variation
float hash1(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float2 hash2(float2 p) {
    return frac(sin(float2(
        dot(p, float2(127.1, 311.7)),
        dot(p, float2(269.5, 183.3))
    )) * 43758.5453);
}

float3x3 rotateY(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    float3x3 m;
    m[0] = float3(c, 0.0, s);
    m[1] = float3(0.0, 1.0, 0.0);
    m[2] = float3(-s, 0.0, c);
    return m;
}

VSOutput VSMain(VSInput input) {
    VSOutput output;

    int gridWidth = 2 * gridRadius + 1;
    int gx = (int)(input.instanceID % (uint)gridWidth) - gridRadius;
    int gz = (int)(input.instanceID / (uint)gridWidth) - gridRadius;

    // World position for this grid cell
    float2 cellPos = float2(cameraGrid.x, cameraGrid.z) + float2(gx, gz) * grassSpacing;

    // Seed from grid cell for deterministic properties
    float2 seed = floor(cellPos / grassSpacing);

    // Density: cull some cells for sporadic coverage
    float density = hash1(seed + float2(7.3, 13.1));
    // Use a threshold curve so grass forms clumps
    // Nearby cells share similar density via low-freq noise
    float2 clumpSeed = floor(cellPos / (grassSpacing * 5.0));
    float clumpNoise = hash1(clumpSeed + float2(42.0, 17.0));
    // Combine: high clumpNoise = dense patch, low = sparse
    bool culled = density > (clumpNoise * 0.85 + 0.15);

    // Jitter within cell - larger range for more organic placement
    float2 jitter = (hash2(seed) - 0.5) * grassSpacing * 1.2;
    float2 worldXZ = cellPos + jitter;

    // World bounds check - move off-screen if out of bounds
    float margin = 50.0;
    float2 townCenter = float2(worldWidth * 0.5, worldHeight * 0.5);
    bool outOfBounds = worldXZ.x < margin || worldXZ.x > worldWidth - margin ||
                       worldXZ.y < margin || worldXZ.y > worldHeight - margin;
    bool inTown = abs(worldXZ.x - townCenter.x) < 200.0 && abs(worldXZ.y - townCenter.y) < 200.0;

    // Distance from camera
    float2 toCell = worldXZ - float2(cameraGrid.x, cameraGrid.z);
    float dist = length(toCell);
    bool tooFar = dist > grassViewDistance;

    // Cull blades behind camera (dot < -0.2 gives ~100 degree behind-camera cone)
    bool behindCamera = dist > 50.0 && dot(normalize(toCell), cameraForward) < -0.2;

    if (outOfBounds || inTown || tooFar || culled || behindCamera) {
        // Degenerate - place at origin with zero scale
        output.position = float4(0, 0, 0, 0);
        output.normal = float3(0, 1, 0);
        output.texCoord = float2(0, 0);
        output.worldPos = float3(0, 0, 0);
        output.color = float4(0, 0, 0, 0);
        return output;
    }

    // Sample heightmap for terrain Y
    float2 hmUV = float2(
        (worldXZ.x - heightmapOriginX) / heightmapWorldSize,
        (worldXZ.y - heightmapOriginZ) / heightmapWorldSize
    );
    hmUV = saturate(hmUV);
    float rawHeight = heightmapTexture.SampleLevel(heightmapSampler, hmUV, 0).r;
    float terrainY = rawHeight * (heightmapMaxHeight - heightmapMinHeight) + heightmapMinHeight;

    // Per-instance properties from hash
    float h1 = hash1(seed);
    float h2 = hash1(seed + float2(1.0, 0.0));
    float h5 = hash1(seed + float2(3.0, 3.0));

    float rotation = h1 * 3.14159;
    float heightMult = h5 * h5;
    float bladeHeight = 3.0 + h2 * 6.0 + heightMult * 8.0;
    float bladeWidth = 0.8 + hash1(seed + float2(0.0, 1.0)) * 0.6;

    // Transform blade vertex
    float3 localPos = input.position * float3(bladeWidth, bladeHeight, bladeWidth);
    float3 localNormal = input.normal;

    // Rotate around Y
    float3 rotatedPos = mul(rotateY(rotation), localPos);
    float3 rotatedNormal = mul(rotateY(rotation), localNormal);

    // Wind (stronger at tip, using texCoord.y as height factor)
    float windFactor = input.texCoord.y;
    float windTime = time * 2.0 + worldXZ.x * 0.1 + worldXZ.y * 0.1;
    float2 windOffset = windDirection * windStrength * windFactor * sin(windTime);

    // Final world position
    float3 worldPos = float3(worldXZ.x, terrainY, worldXZ.y) + rotatedPos;
    worldPos.x += windOffset.x;
    worldPos.z += windOffset.y;

    output.worldPos = worldPos;
    output.normal = rotatedNormal;
    output.texCoord = input.texCoord;
    output.color = input.color;
    output.position = mul(viewProjection, float4(worldPos, 1.0));

    return output;
}

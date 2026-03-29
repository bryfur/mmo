#include <gtest/gtest.h>
#include "engine/heightmap.hpp"
#include <cmath>

using namespace mmo::engine;

// Helper: create a heightmap with known dimensions and flat height
static Heightmap make_flat(uint32_t res, float world_size, float height) {
    Heightmap h;
    h.resolution = res;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = world_size;
    h.min_height = -500.0f;
    h.max_height = 500.0f;
    // Encode the desired height into uint16
    float norm = (height - h.min_height) / (h.max_height - h.min_height);
    uint16_t raw = static_cast<uint16_t>(norm * 65535.0f);
    h.height_data.assign(res * res, raw);
    return h;
}

// Helper: encode a height value to uint16 given min/max range
static uint16_t encode_height(float height, float min_h, float max_h) {
    float norm = (height - min_h) / (max_h - min_h);
    return static_cast<uint16_t>(norm * 65535.0f);
}

TEST(Heightmap, ConstructionDimensions) {
    auto h = make_flat(64, 100.0f, 0.0f);
    EXPECT_EQ(h.resolution, 64u);
    EXPECT_EQ(h.height_data.size(), 64u * 64u);
    EXPECT_FLOAT_EQ(h.world_size, 100.0f);
}

TEST(Heightmap, GetHeightLocalReturnsSetValue) {
    auto h = make_flat(4, 10.0f, 42.0f);
    // All cells should return ~42.0
    for (uint32_t z = 0; z < 4; z++) {
        for (uint32_t x = 0; x < 4; x++) {
            EXPECT_NEAR(h.get_height_local(x, z), 42.0f, 0.02f);
        }
    }
}

TEST(Heightmap, GetHeightLocalOutOfBoundsReturnsZero) {
    auto h = make_flat(4, 10.0f, 42.0f);
    EXPECT_FLOAT_EQ(h.get_height_local(4, 0), 0.0f);
    EXPECT_FLOAT_EQ(h.get_height_local(0, 4), 0.0f);
    EXPECT_FLOAT_EQ(h.get_height_local(100, 100), 0.0f);
}

TEST(Heightmap, GetHeightWorldAtGridPoints) {
    // 3x3 grid, world_size = 2.0, origin at (0,0)
    // Grid points are at world coords (0,0), (1,0), (2,0), (0,1), ...
    Heightmap h;
    h.resolution = 3;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 2.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(9);

    // Set specific heights:
    // (0,0)=10, (1,0)=20, (2,0)=30
    // (0,1)=40, (1,1)=50, (2,1)=60
    // (0,2)=70, (1,2)=80, (2,2)=90
    float heights[] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
    for (int i = 0; i < 9; i++) {
        h.height_data[i] = encode_height(heights[i], 0.0f, 100.0f);
    }

    // Grid spacing = world_size / (resolution - 1) = 2.0 / 2 = 1.0
    // World (0,0) -> grid (0,0) -> height 10
    EXPECT_NEAR(h.get_height_world(0.0f, 0.0f), 10.0f, 0.1f);
    // World (2,0) -> grid (2,0) -> height 30
    EXPECT_NEAR(h.get_height_world(2.0f, 0.0f), 30.0f, 0.1f);
    // World (1,1) -> grid (1,1) -> height 50
    EXPECT_NEAR(h.get_height_world(1.0f, 1.0f), 50.0f, 0.1f);
    // World (2,2) -> grid (2,2) -> height 90
    EXPECT_NEAR(h.get_height_world(2.0f, 2.0f), 90.0f, 0.1f);
}

TEST(Heightmap, BilinearInterpolationMidpoint) {
    // 2x2 grid with different corner heights
    Heightmap h;
    h.resolution = 2;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 1.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(4);

    // (0,0)=0, (1,0)=100, (0,1)=0, (1,1)=100
    h.height_data[0] = encode_height(0.0f, 0.0f, 100.0f);
    h.height_data[1] = encode_height(100.0f, 0.0f, 100.0f);
    h.height_data[2] = encode_height(0.0f, 0.0f, 100.0f);
    h.height_data[3] = encode_height(100.0f, 0.0f, 100.0f);

    // Center of the grid should be 50 (bilinear average)
    EXPECT_NEAR(h.get_height_world(0.5f, 0.5f), 50.0f, 0.1f);
    // Quarter along x axis: should be 25
    EXPECT_NEAR(h.get_height_world(0.25f, 0.0f), 25.0f, 0.1f);
}

TEST(Heightmap, BilinearInterpolationNonUniform) {
    // 2x2 grid: corners = 0, 40, 60, 100
    Heightmap h;
    h.resolution = 2;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 1.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(4);

    // h00=0, h10=40, h01=60, h11=100
    h.height_data[0] = encode_height(0.0f, 0.0f, 100.0f);
    h.height_data[1] = encode_height(40.0f, 0.0f, 100.0f);
    h.height_data[2] = encode_height(60.0f, 0.0f, 100.0f);
    h.height_data[3] = encode_height(100.0f, 0.0f, 100.0f);

    // At (0.5, 0.5): bilinear = (0 + 40 + 60 + 100) / 4 = 50
    EXPECT_NEAR(h.get_height_world(0.5f, 0.5f), 50.0f, 0.2f);
    // At (0.0, 0.5): lerp between h00=0 and h01=60 -> 30
    EXPECT_NEAR(h.get_height_world(0.0f, 0.5f), 30.0f, 0.2f);
    // At (1.0, 0.5): lerp between h10=40 and h11=100 -> 70
    EXPECT_NEAR(h.get_height_world(1.0f, 0.5f), 70.0f, 0.2f);
}

TEST(Heightmap, OutOfBoundsWorldQueryClamps) {
    auto h = make_flat(4, 10.0f, 25.0f);
    // Queries outside [0, 10] should clamp to edge values (all 25)
    EXPECT_NEAR(h.get_height_world(-5.0f, 5.0f), 25.0f, 0.1f);
    EXPECT_NEAR(h.get_height_world(15.0f, 5.0f), 25.0f, 0.1f);
    EXPECT_NEAR(h.get_height_world(5.0f, -5.0f), 25.0f, 0.1f);
    EXPECT_NEAR(h.get_height_world(5.0f, 15.0f), 25.0f, 0.1f);
}

TEST(Heightmap, WorldToGridConversion) {
    // With origin at (10, 20) and world_size 100, resolution 11
    // Grid spacing = 100 / 10 = 10.0 world units per cell
    Heightmap h;
    h.resolution = 11;
    h.world_origin_x = 10.0f;
    h.world_origin_z = 20.0f;
    h.world_size = 100.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(11 * 11);

    // Set grid point (5,5) to height 75 (center of the grid)
    h.height_data[5 * 11 + 5] = encode_height(75.0f, 0.0f, 100.0f);
    // Set surrounding points to 75 too so interpolation gives 75
    h.height_data[4 * 11 + 5] = encode_height(75.0f, 0.0f, 100.0f);
    h.height_data[6 * 11 + 5] = encode_height(75.0f, 0.0f, 100.0f);
    h.height_data[5 * 11 + 4] = encode_height(75.0f, 0.0f, 100.0f);
    h.height_data[5 * 11 + 6] = encode_height(75.0f, 0.0f, 100.0f);

    // World (60, 70) -> u = (60-10)/100 = 0.5, v = (70-20)/100 = 0.5
    // grid = 0.5 * 10 = 5.0 -> grid point (5,5)
    EXPECT_NEAR(h.get_height_world(60.0f, 70.0f), 75.0f, 0.2f);
}

TEST(Heightmap, NormalOnFlatSurfacePointsUp) {
    auto h = make_flat(8, 10.0f, 0.0f);
    float nx, ny, nz;
    h.get_normal_world(5.0f, 5.0f, nx, ny, nz);
    // Flat surface normal should be (0, 1, 0)
    EXPECT_NEAR(nx, 0.0f, 0.01f);
    EXPECT_NEAR(ny, 1.0f, 0.01f);
    EXPECT_NEAR(nz, 0.0f, 0.01f);
}

TEST(Heightmap, NormalIsUnitLength) {
    // Create a sloped surface
    Heightmap h;
    h.resolution = 4;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 3.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(16);
    // Slope in x direction: height increases with x
    for (uint32_t z = 0; z < 4; z++) {
        for (uint32_t x = 0; x < 4; x++) {
            h.height_data[z * 4 + x] = encode_height(x * 10.0f, 0.0f, 100.0f);
        }
    }

    float nx, ny, nz;
    h.get_normal_world(1.5f, 1.5f, nx, ny, nz);
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    EXPECT_NEAR(len, 1.0f, 0.001f);
}

TEST(Heightmap, NegativeHeights) {
    Heightmap h;
    h.resolution = 2;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 1.0f;
    h.min_height = -100.0f;
    h.max_height = 100.0f;
    h.height_data.resize(4);

    // Set all to -50
    uint16_t raw = encode_height(-50.0f, -100.0f, 100.0f);
    h.height_data.assign(4, raw);

    EXPECT_NEAR(h.get_height_world(0.5f, 0.5f), -50.0f, 0.1f);
}

TEST(Heightmap, Uint16EncodingRoundTrip) {
    Heightmap h;
    h.resolution = 1;
    h.min_height = -500.0f;
    h.max_height = 500.0f;
    h.height_data.resize(1);

    // Test several values across the range
    float test_values[] = {-500.0f, -250.0f, 0.0f, 250.0f, 499.99f};
    for (float val : test_values) {
        h.height_data[0] = encode_height(val, h.min_height, h.max_height);
        float result = h.get_height_local(0, 0);
        EXPECT_NEAR(result, val, 0.02f) << "Round-trip failed for " << val;
    }
}

// --- Edge case tests ---

TEST(Heightmap, SinglePointHeightmap) {
    // 1x1 heightmap: single point, no interpolation possible
    Heightmap h;
    h.resolution = 1;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 10.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(1);
    h.height_data[0] = encode_height(55.0f, 0.0f, 100.0f);

    EXPECT_NEAR(h.get_height_local(0, 0), 55.0f, 0.1f);
    // Out of bounds returns 0
    EXPECT_FLOAT_EQ(h.get_height_local(1, 0), 0.0f);
}

TEST(Heightmap, LargeDimensionConstruction) {
    // Verify large heightmap can be constructed without issue
    Heightmap h;
    h.resolution = 2048;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 10000.0f;
    h.min_height = -500.0f;
    h.max_height = 500.0f;
    h.height_data.resize(2048u * 2048u, 32768u); // midpoint = height 0

    EXPECT_EQ(h.height_data.size(), 2048u * 2048u);
    EXPECT_NEAR(h.get_height_local(1024, 1024), 0.0f, 0.02f);
}

TEST(Heightmap, HeightAtExactGridCorners) {
    // 4x4 heightmap, verify all four world-space corners return correct heights
    Heightmap h;
    h.resolution = 4;
    h.world_origin_x = 5.0f;
    h.world_origin_z = 10.0f;
    h.world_size = 30.0f;
    h.min_height = 0.0f;
    h.max_height = 200.0f;
    h.height_data.resize(16, encode_height(100.0f, 0.0f, 200.0f));

    // Set distinct corner heights
    h.height_data[0 * 4 + 0] = encode_height(10.0f, 0.0f, 200.0f);  // top-left
    h.height_data[0 * 4 + 3] = encode_height(30.0f, 0.0f, 200.0f);  // top-right
    h.height_data[3 * 4 + 0] = encode_height(50.0f, 0.0f, 200.0f);  // bottom-left
    h.height_data[3 * 4 + 3] = encode_height(70.0f, 0.0f, 200.0f);  // bottom-right

    // World corners: origin=(5,10), far=(35,40)
    EXPECT_NEAR(h.get_height_world(5.0f, 10.0f), 10.0f, 0.2f);
    EXPECT_NEAR(h.get_height_world(35.0f, 10.0f), 30.0f, 0.2f);
    EXPECT_NEAR(h.get_height_world(5.0f, 40.0f), 50.0f, 0.2f);
    EXPECT_NEAR(h.get_height_world(35.0f, 40.0f), 70.0f, 0.2f);
}

TEST(Heightmap, InterpolationAlongEdge) {
    // 3x3 grid, interpolate along the z=0 edge (first row only)
    Heightmap h;
    h.resolution = 3;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 2.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(9, encode_height(0.0f, 0.0f, 100.0f));

    // First row: 0, 50, 100
    h.height_data[0] = encode_height(0.0f, 0.0f, 100.0f);
    h.height_data[1] = encode_height(50.0f, 0.0f, 100.0f);
    h.height_data[2] = encode_height(100.0f, 0.0f, 100.0f);

    // Along z=0 edge, midpoint between grid(0,0) and grid(1,0) at world x=0.5
    EXPECT_NEAR(h.get_height_world(0.5f, 0.0f), 25.0f, 0.2f);
    // Midpoint between grid(1,0) and grid(2,0) at world x=1.5
    EXPECT_NEAR(h.get_height_world(1.5f, 0.0f), 75.0f, 0.2f);
}

TEST(Heightmap, DiagonalInterpolationAcrossCell) {
    // 2x2 grid with distinct corners
    Heightmap h;
    h.resolution = 2;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 1.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(4);

    // h00=0, h10=20, h01=40, h11=80
    h.height_data[0] = encode_height(0.0f, 0.0f, 100.0f);
    h.height_data[1] = encode_height(20.0f, 0.0f, 100.0f);
    h.height_data[2] = encode_height(40.0f, 0.0f, 100.0f);
    h.height_data[3] = encode_height(80.0f, 0.0f, 100.0f);

    // Diagonal from (0,0) to (1,1) at t=0.25 -> world (0.25, 0.25)
    // Bilinear: h0 = 0*(1-0.25) + 20*0.25 = 5, h1 = 40*(1-0.25) + 80*0.25 = 50
    // result = 5*(1-0.25) + 50*0.25 = 3.75 + 12.5 = 16.25
    EXPECT_NEAR(h.get_height_world(0.25f, 0.25f), 16.25f, 0.3f);

    // Diagonal at t=0.75 -> world (0.75, 0.75)
    // h0 = 0*0.25 + 20*0.75 = 15, h1 = 40*0.25 + 80*0.75 = 70
    // result = 15*0.25 + 70*0.75 = 3.75 + 52.5 = 56.25
    EXPECT_NEAR(h.get_height_world(0.75f, 0.75f), 56.25f, 0.3f);
}

TEST(Heightmap, FlatTerrainMinEqualsMax) {
    // min_height == max_height: all heights collapse to that value
    Heightmap h;
    h.resolution = 4;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 10.0f;
    h.min_height = 42.0f;
    h.max_height = 42.0f;
    // When range is 0, any raw value maps to min_height (0 * 0 + 42 = 42)
    h.height_data.assign(16, 0);

    EXPECT_NEAR(h.get_height_local(0, 0), 42.0f, 0.01f);
    EXPECT_NEAR(h.get_height_local(2, 3), 42.0f, 0.01f);
    EXPECT_NEAR(h.get_height_world(5.0f, 5.0f), 42.0f, 0.01f);
}

TEST(Heightmap, NegativeMinHeight) {
    Heightmap h;
    h.resolution = 2;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 1.0f;
    h.min_height = -200.0f;
    h.max_height = 100.0f;
    h.height_data.resize(4);

    // Encode -150 in [-200, 100] range
    h.height_data.assign(4, encode_height(-150.0f, -200.0f, 100.0f));
    EXPECT_NEAR(h.get_height_world(0.5f, 0.5f), -150.0f, 0.1f);

    // Encode 0.0 in [-200, 100] range
    h.height_data.assign(4, encode_height(0.0f, -200.0f, 100.0f));
    EXPECT_NEAR(h.get_height_world(0.5f, 0.5f), 0.0f, 0.1f);
}

TEST(Heightmap, NormalOnSlope) {
    // Create a heightmap that slopes upward in the +x direction
    Heightmap h;
    h.resolution = 4;
    h.world_origin_x = 0.0f;
    h.world_origin_z = 0.0f;
    h.world_size = 3.0f;
    h.min_height = 0.0f;
    h.max_height = 100.0f;
    h.height_data.resize(16);

    // Height increases linearly with x: column 0=0, 1=20, 2=40, 3=60
    for (uint32_t z = 0; z < 4; z++) {
        for (uint32_t x = 0; x < 4; x++) {
            h.height_data[z * 4 + x] = encode_height(x * 20.0f, 0.0f, 100.0f);
        }
    }

    float nx, ny, nz;
    h.get_normal_world(1.5f, 1.5f, nx, ny, nz);

    // Normal should point away from the slope: negative x component (tilts away from rising slope)
    EXPECT_LT(nx, 0.0f) << "Normal x should be negative on a +x upward slope";
    // Normal should still point generally upward
    EXPECT_GT(ny, 0.0f) << "Normal y should be positive (pointing up)";
    // No slope in z direction, so nz should be near zero
    EXPECT_NEAR(nz, 0.0f, 0.01f);
    // Should be unit length
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    EXPECT_NEAR(len, 1.0f, 0.001f);
}

TEST(Heightmap, NormalAtEdgeOfHeightmap) {
    // Normal at the very edge should still work (clamping prevents out-of-bounds)
    auto h = make_flat(8, 10.0f, 25.0f);

    float nx, ny, nz;
    // Bottom-left corner
    h.get_normal_world(0.0f, 0.0f, nx, ny, nz);
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    EXPECT_NEAR(len, 1.0f, 0.001f);
    // Flat terrain: normal should be roughly (0,1,0) even at edge
    EXPECT_NEAR(nx, 0.0f, 0.05f);
    EXPECT_NEAR(ny, 1.0f, 0.05f);
    EXPECT_NEAR(nz, 0.0f, 0.05f);

    // Top-right corner
    h.get_normal_world(10.0f, 10.0f, nx, ny, nz);
    len = std::sqrt(nx * nx + ny * ny + nz * nz);
    EXPECT_NEAR(len, 1.0f, 0.001f);
    EXPECT_NEAR(nx, 0.0f, 0.05f);
    EXPECT_NEAR(ny, 1.0f, 0.05f);
    EXPECT_NEAR(nz, 0.0f, 0.05f);
}

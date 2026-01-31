#pragma once
/**
 * Engine heightmap data structure for terrain rendering.
 * Contains only the data and methods needed by the renderer.
 * Game code converts from its own heightmap format to this.
 */

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

namespace mmo::engine {

struct Heightmap {
    uint32_t resolution = 0;

    float world_origin_x = 0.0f;
    float world_origin_z = 0.0f;
    float world_size = 0.0f;

    // Min/max height range used for 16-bit normalization
    float min_height = -500.0f;
    float max_height = 500.0f;

    // Height data as 16-bit normalized values
    // Real height = (uint16_value / 65535.0) * (max_height - min_height) + min_height
    std::vector<uint16_t> height_data;

    float get_height_local(uint32_t local_x, uint32_t local_z) const {
        if (local_x >= resolution || local_z >= resolution) return 0.0f;
        uint16_t raw = height_data[local_z * resolution + local_x];
        float normalized = raw / 65535.0f;
        return normalized * (max_height - min_height) + min_height;
    }

    float get_height_world(float world_x, float world_z) const {
        float u = (world_x - world_origin_x) / world_size;
        float v = (world_z - world_origin_z) / world_size;
        u = std::fmax(0.0f, std::fmin(1.0f, u));
        v = std::fmax(0.0f, std::fmin(1.0f, v));

        float tx = u * (resolution - 1);
        float tz = v * (resolution - 1);
        uint32_t x0 = static_cast<uint32_t>(tx);
        uint32_t z0 = static_cast<uint32_t>(tz);
        uint32_t x1 = std::min(x0 + 1, resolution - 1);
        uint32_t z1 = std::min(z0 + 1, resolution - 1);
        float fx = tx - x0;
        float fz = tz - z0;

        float h00 = get_height_local(x0, z0);
        float h10 = get_height_local(x1, z0);
        float h01 = get_height_local(x0, z1);
        float h11 = get_height_local(x1, z1);
        float h0 = h00 * (1.0f - fx) + h10 * fx;
        float h1 = h01 * (1.0f - fx) + h11 * fx;
        return h0 * (1.0f - fz) + h1 * fz;
    }

    void get_normal_world(float world_x, float world_z, float& nx, float& ny, float& nz) const {
        float eps = world_size / (resolution - 1);
        float hL = get_height_world(world_x - eps, world_z);
        float hR = get_height_world(world_x + eps, world_z);
        float hD = get_height_world(world_x, world_z - eps);
        float hU = get_height_world(world_x, world_z + eps);

        nx = hL - hR;
        ny = 2.0f * eps;
        nz = hD - hU;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }
        else { nx = 0.0f; ny = 1.0f; nz = 0.0f; }
    }
};

} // namespace mmo::engine

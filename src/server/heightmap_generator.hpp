#pragma once

#include "protocol/heightmap.hpp"
#include <cmath>
#include <algorithm>

namespace mmo::server {


inline void heightmap_init(mmo::protocol::HeightmapChunk& chunk, int32_t cx, int32_t cz,
                           uint32_t res = mmo::protocol::heightmap_config::CHUNK_RESOLUTION) {
    chunk.chunk_x = cx;
    chunk.chunk_z = cz;
    chunk.resolution = res;
    chunk.world_origin_x = cx * mmo::protocol::heightmap_config::CHUNK_WORLD_SIZE;
    chunk.world_origin_z = cz * mmo::protocol::heightmap_config::CHUNK_WORLD_SIZE;
    chunk.world_size = mmo::protocol::heightmap_config::CHUNK_WORLD_SIZE;
    chunk.height_data.resize(res * res, 0);
}

inline void heightmap_set(mmo::protocol::HeightmapChunk& chunk, uint32_t local_x, uint32_t local_z, float height) {
    if (local_x >= chunk.resolution || local_z >= chunk.resolution) return;
    float clamped = std::fmax(mmo::protocol::heightmap_config::MIN_HEIGHT,
                              std::fmin(mmo::protocol::heightmap_config::MAX_HEIGHT, height));
    float normalized = (clamped - mmo::protocol::heightmap_config::MIN_HEIGHT) /
                      (mmo::protocol::heightmap_config::MAX_HEIGHT - mmo::protocol::heightmap_config::MIN_HEIGHT);
    chunk.height_data[local_z * chunk.resolution + local_x] = static_cast<uint16_t>(normalized * 65535.0f);
}

inline float heightmap_get_local(const mmo::protocol::HeightmapChunk& chunk, uint32_t local_x, uint32_t local_z) {
    if (local_x >= chunk.resolution || local_z >= chunk.resolution) return 0.0f;
    uint16_t raw = chunk.height_data[local_z * chunk.resolution + local_x];
    float normalized = raw / 65535.0f;
    return normalized * (mmo::protocol::heightmap_config::MAX_HEIGHT - mmo::protocol::heightmap_config::MIN_HEIGHT)
           + mmo::protocol::heightmap_config::MIN_HEIGHT;
}

inline float heightmap_get_world(const mmo::protocol::HeightmapChunk& chunk, float world_x, float world_z) {
    float u = (world_x - chunk.world_origin_x) / chunk.world_size;
    float v = (world_z - chunk.world_origin_z) / chunk.world_size;
    u = std::fmax(0.0f, std::fmin(1.0f, u));
    v = std::fmax(0.0f, std::fmin(1.0f, v));

    float tx = u * (chunk.resolution - 1);
    float tz = v * (chunk.resolution - 1);
    uint32_t x0 = static_cast<uint32_t>(tx);
    uint32_t z0 = static_cast<uint32_t>(tz);
    uint32_t x1 = std::min(x0 + 1, chunk.resolution - 1);
    uint32_t z1 = std::min(z0 + 1, chunk.resolution - 1);
    float fx = tx - x0;
    float fz = tz - z0;

    float h00 = heightmap_get_local(chunk, x0, z0);
    float h10 = heightmap_get_local(chunk, x1, z0);
    float h01 = heightmap_get_local(chunk, x0, z1);
    float h11 = heightmap_get_local(chunk, x1, z1);

    float h0 = h00 * (1.0f - fx) + h10 * fx;
    float h1 = h01 * (1.0f - fx) + h11 * fx;
    return h0 * (1.0f - fz) + h1 * fz;
}

namespace heightmap_generator {

namespace detail {

inline float terrain_height(float x, float z, float world_width, float world_height) {
    float world_center_x = world_width / 2.0f;
    float world_center_z = world_height / 2.0f;

    float dx = x - world_center_x;
    float dz = z - world_center_z;
    float dist = std::sqrt(dx * dx + dz * dz);

    float playable_radius = 600.0f;
    float transition_radius = 400.0f;
    float flatness = 1.0f;

    if (dist < playable_radius) {
        flatness = 0.1f;
    } else if (dist < playable_radius + transition_radius) {
        float t = (dist - playable_radius) / transition_radius;
        flatness = 0.1f + t * 0.9f;
    }

    float height = 0.0f;

    float freq1 = 0.0008f;
    height += std::sin(x * freq1 * 1.1f) * std::cos(z * freq1 * 0.9f) * 80.0f;
    height += std::sin(x * freq1 * 0.7f + 1.3f) * std::sin(z * freq1 * 1.2f + 0.7f) * 60.0f;

    float freq2 = 0.003f;
    height += std::sin(x * freq2 * 1.3f + 2.1f) * std::cos(z * freq2 * 0.8f + 1.4f) * 25.0f;
    height += std::cos(x * freq2 * 0.9f) * std::sin(z * freq2 * 1.1f + 0.5f) * 20.0f;

    float freq3 = 0.01f;
    height += std::sin(x * freq3 * 1.7f + 0.3f) * std::cos(z * freq3 * 1.4f + 2.1f) * 8.0f;
    height += std::cos(x * freq3 * 1.2f + 1.8f) * std::sin(z * freq3 * 0.9f) * 6.0f;

    height *= flatness;

    if (dist > 2000.0f) {
        float rise_factor = (dist - 2000.0f) / 2000.0f;
        rise_factor = std::min(rise_factor, 1.0f);
        height += rise_factor * rise_factor * 150.0f;
    }

    return height;
}

} // namespace detail

inline void generate_procedural(mmo::protocol::HeightmapChunk& chunk, float world_width, float world_height) {
    for (uint32_t z = 0; z < chunk.resolution; ++z) {
        for (uint32_t x = 0; x < chunk.resolution; ++x) {
            float u = static_cast<float>(x) / (chunk.resolution - 1);
            float v = static_cast<float>(z) / (chunk.resolution - 1);
            float world_x = chunk.world_origin_x + u * chunk.world_size;
            float world_z = chunk.world_origin_z + v * chunk.world_size;

            float height = detail::terrain_height(world_x, world_z, world_width, world_height);
            heightmap_set(chunk, x, z, height);
        }
    }
}

} // namespace heightmap_generator

} // namespace mmo::server

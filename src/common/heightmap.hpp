#pragma once
/**
 * Heightmap Data Structures - Shared Between Client and Server
 * 
 * Designed for streaming chunks in the future.
 * Server generates/loads heightmaps and sends to clients.
 * Clients upload to GPU as texture for shader sampling.
 */

#include <cstdint>
#include <vector>
#include <cstring>
#include <cmath>
#include "terrain.hpp"

namespace mmo {

/**
 * Heightmap configuration constants
 */
namespace heightmap_config {
    // Resolution of each chunk (vertices per edge)
    // 257 = 256 cells + 1 for edge vertices (power of 2 + 1 for seamless tiling)
    constexpr uint32_t CHUNK_RESOLUTION = 257;
    
    // World size each chunk covers (in world units/meters)
    constexpr float CHUNK_WORLD_SIZE = 8000.0f;
    
    // Height range for normalization (for 16-bit storage)
    constexpr float MIN_HEIGHT = -500.0f;
    constexpr float MAX_HEIGHT = 500.0f;
    
    // Meters per texel
    constexpr float TEXEL_SIZE = CHUNK_WORLD_SIZE / (CHUNK_RESOLUTION - 1);
}

/**
 * A single heightmap chunk that can be streamed
 */
struct HeightmapChunk {
    // Chunk grid position (for multi-chunk worlds)
    int32_t chunk_x = 0;
    int32_t chunk_z = 0;
    
    // Resolution (width and height in samples)
    uint32_t resolution = heightmap_config::CHUNK_RESOLUTION;
    
    // World-space bounds this chunk covers
    float world_origin_x = 0.0f;
    float world_origin_z = 0.0f;
    float world_size = heightmap_config::CHUNK_WORLD_SIZE;
    
    // Height data stored as 16-bit normalized values for compact transfer
    // Real height = (uint16_value / 65535.0) * (MAX_HEIGHT - MIN_HEIGHT) + MIN_HEIGHT
    std::vector<uint16_t> height_data;
    
    /**
     * Initialize chunk with given parameters
     */
    void init(int32_t cx, int32_t cz, uint32_t res = heightmap_config::CHUNK_RESOLUTION) {
        chunk_x = cx;
        chunk_z = cz;
        resolution = res;
        world_origin_x = cx * heightmap_config::CHUNK_WORLD_SIZE;
        world_origin_z = cz * heightmap_config::CHUNK_WORLD_SIZE;
        world_size = heightmap_config::CHUNK_WORLD_SIZE;
        height_data.resize(resolution * resolution, 0);
    }
    
    /**
     * Set height at local coordinates (0 to resolution-1)
     */
    void set_height(uint32_t local_x, uint32_t local_z, float height) {
        if (local_x >= resolution || local_z >= resolution) return;
        
        // Clamp and normalize to 16-bit
        float clamped = std::fmax(heightmap_config::MIN_HEIGHT, 
                                  std::fmin(heightmap_config::MAX_HEIGHT, height));
        float normalized = (clamped - heightmap_config::MIN_HEIGHT) / 
                          (heightmap_config::MAX_HEIGHT - heightmap_config::MIN_HEIGHT);
        height_data[local_z * resolution + local_x] = static_cast<uint16_t>(normalized * 65535.0f);
    }
    
    /**
     * Get height at local coordinates (0 to resolution-1)
     */
    float get_height_local(uint32_t local_x, uint32_t local_z) const {
        if (local_x >= resolution || local_z >= resolution) return 0.0f;
        
        uint16_t raw = height_data[local_z * resolution + local_x];
        float normalized = raw / 65535.0f;
        return normalized * (heightmap_config::MAX_HEIGHT - heightmap_config::MIN_HEIGHT) 
               + heightmap_config::MIN_HEIGHT;
    }
    
    /**
     * Get height at world coordinates with bilinear interpolation
     */
    float get_height_world(float world_x, float world_z) const {
        // Convert world to local UV (0-1)
        float u = (world_x - world_origin_x) / world_size;
        float v = (world_z - world_origin_z) / world_size;
        
        // Clamp to valid range
        u = std::fmax(0.0f, std::fmin(1.0f, u));
        v = std::fmax(0.0f, std::fmin(1.0f, v));
        
        // Convert to texel coordinates
        float tx = u * (resolution - 1);
        float tz = v * (resolution - 1);
        
        // Get integer and fractional parts
        uint32_t x0 = static_cast<uint32_t>(tx);
        uint32_t z0 = static_cast<uint32_t>(tz);
        uint32_t x1 = std::min(x0 + 1, resolution - 1);
        uint32_t z1 = std::min(z0 + 1, resolution - 1);
        float fx = tx - x0;
        float fz = tz - z0;
        
        // Bilinear interpolation
        float h00 = get_height_local(x0, z0);
        float h10 = get_height_local(x1, z0);
        float h01 = get_height_local(x0, z1);
        float h11 = get_height_local(x1, z1);
        
        float h0 = h00 * (1.0f - fx) + h10 * fx;
        float h1 = h01 * (1.0f - fx) + h11 * fx;
        
        return h0 * (1.0f - fz) + h1 * fz;
    }
    
    /**
     * Get terrain normal at world coordinates using central differences
     */
    void get_normal_world(float world_x, float world_z, float& nx, float& ny, float& nz) const {
        float eps = heightmap_config::TEXEL_SIZE;
        float hL = get_height_world(world_x - eps, world_z);
        float hR = get_height_world(world_x + eps, world_z);
        float hD = get_height_world(world_x, world_z - eps);
        float hU = get_height_world(world_x, world_z + eps);
        
        nx = hL - hR;
        ny = 2.0f * eps;
        nz = hD - hU;
        
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 0.0001f) {
            nx /= len;
            ny /= len;
            nz /= len;
        } else {
            nx = 0.0f;
            ny = 1.0f;
            nz = 0.0f;
        }
    }
    
    /**
     * Serialized size for network transfer
     * Header: chunk_x(4) + chunk_z(4) + resolution(4) + origin_x(4) + origin_z(4) + world_size(4) = 24 bytes
     * Data: resolution * resolution * 2 bytes
     */
    size_t serialized_size() const {
        return 24 + height_data.size() * sizeof(uint16_t);
    }
    
    /**
     * Serialize to byte buffer for network transfer
     */
    void serialize(std::vector<uint8_t>& buffer) const {
        size_t start = buffer.size();
        buffer.resize(start + serialized_size());
        uint8_t* ptr = buffer.data() + start;
        
        std::memcpy(ptr, &chunk_x, 4); ptr += 4;
        std::memcpy(ptr, &chunk_z, 4); ptr += 4;
        std::memcpy(ptr, &resolution, 4); ptr += 4;
        std::memcpy(ptr, &world_origin_x, 4); ptr += 4;
        std::memcpy(ptr, &world_origin_z, 4); ptr += 4;
        std::memcpy(ptr, &world_size, 4); ptr += 4;
        std::memcpy(ptr, height_data.data(), height_data.size() * sizeof(uint16_t));
    }
    
    /**
     * Deserialize from byte buffer
     */
    bool deserialize(const uint8_t* data, size_t size) {
        if (size < 24) return false;
        
        const uint8_t* ptr = data;
        std::memcpy(&chunk_x, ptr, 4); ptr += 4;
        std::memcpy(&chunk_z, ptr, 4); ptr += 4;
        std::memcpy(&resolution, ptr, 4); ptr += 4;
        std::memcpy(&world_origin_x, ptr, 4); ptr += 4;
        std::memcpy(&world_origin_z, ptr, 4); ptr += 4;
        std::memcpy(&world_size, ptr, 4); ptr += 4;
        
        // Reject unreasonable resolutions to prevent excessive memory allocation
        if (resolution == 0 || resolution > 4096) return false;

        size_t data_size = resolution * resolution;
        if (size < 24 + data_size * sizeof(uint16_t)) return false;

        height_data.resize(data_size);
        std::memcpy(height_data.data(), ptr, data_size * sizeof(uint16_t));
        
        return true;
    }
};

/**
 * Procedural heightmap generator (for development)
 * Uses the same formula as the original terrain for consistency
 */
namespace heightmap_generator {
    
    /**
     * Generate a chunk using procedural noise
     */
    inline void generate_procedural(HeightmapChunk& chunk, float world_width, float world_height) {
        for (uint32_t z = 0; z < chunk.resolution; ++z) {
            for (uint32_t x = 0; x < chunk.resolution; ++x) {
                float u = static_cast<float>(x) / (chunk.resolution - 1);
                float v = static_cast<float>(z) / (chunk.resolution - 1);
                float world_x = chunk.world_origin_x + u * chunk.world_size;
                float world_z = chunk.world_origin_z + v * chunk.world_size;

                float height = terrain::get_height(world_x, world_z, world_width, world_height);
                chunk.set_height(x, z, height);
            }
        }
    }
}

} // namespace mmo

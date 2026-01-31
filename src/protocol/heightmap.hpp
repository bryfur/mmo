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

namespace mmo::protocol {

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

} // namespace mmo::protocol

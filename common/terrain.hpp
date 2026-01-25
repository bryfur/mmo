#pragma once
/**
 * Terrain Height Calculation - Shared Between Client and Server
 * 
 * This ensures both client rendering and server physics/collision
 * use identical terrain geometry. The procedural formula is deterministic,
 * so any (x, z) coordinate produces the same height on all machines.
 */

#include <cmath>
#include <algorithm>

namespace mmo::terrain {

/**
 * Get terrain height at any world position.
 * Uses multi-octave procedural generation for natural-looking terrain.
 * 
 * @param x World X coordinate
 * @param z World Z coordinate  
 * @param world_width Total world width (for center calculation)
 * @param world_height Total world height/depth (for center calculation)
 * @return Height (Y) at the given position
 */
inline float get_height(float x, float z, float world_width, float world_height) {
    float world_center_x = world_width / 2.0f;
    float world_center_z = world_height / 2.0f;
    
    float dx = x - world_center_x;
    float dz = z - world_center_z;
    float dist = std::sqrt(dx * dx + dz * dz);
    
    // Keep playable area relatively flat
    float playable_radius = 600.0f;
    float transition_radius = 400.0f;
    float flatness = 1.0f;
    
    if (dist < playable_radius) {
        flatness = 0.1f;
    } else if (dist < playable_radius + transition_radius) {
        float t = (dist - playable_radius) / transition_radius;
        flatness = 0.1f + t * 0.9f;
    }
    
    // Multi-octave noise for natural terrain
    float height = 0.0f;
    
    // Large rolling hills
    float freq1 = 0.0008f;
    height += std::sin(x * freq1 * 1.1f) * std::cos(z * freq1 * 0.9f) * 80.0f;
    height += std::sin(x * freq1 * 0.7f + 1.3f) * std::sin(z * freq1 * 1.2f + 0.7f) * 60.0f;
    
    // Medium undulations
    float freq2 = 0.003f;
    height += std::sin(x * freq2 * 1.3f + 2.1f) * std::cos(z * freq2 * 0.8f + 1.4f) * 25.0f;
    height += std::cos(x * freq2 * 0.9f) * std::sin(z * freq2 * 1.1f + 0.5f) * 20.0f;
    
    // Small bumps
    float freq3 = 0.01f;
    height += std::sin(x * freq3 * 1.7f + 0.3f) * std::cos(z * freq3 * 1.4f + 2.1f) * 8.0f;
    height += std::cos(x * freq3 * 1.2f + 1.8f) * std::sin(z * freq3 * 0.9f) * 6.0f;
    
    height *= flatness;
    
    // Terrain rises toward mountains at world edges
    if (dist > 2000.0f) {
        float rise_factor = (dist - 2000.0f) / 2000.0f;
        rise_factor = std::min(rise_factor, 1.0f);
        height += rise_factor * rise_factor * 150.0f;
    }
    
    return height;
}

/**
 * Get terrain normal at any world position.
 * Computed via central differences of the height function.
 * 
 * @param x World X coordinate
 * @param z World Z coordinate
 * @param world_width Total world width
 * @param world_height Total world height/depth
 * @return Normalized surface normal vector (as array: {nx, ny, nz})
 */
inline void get_normal(float x, float z, float world_width, float world_height,
                       float& nx, float& ny, float& nz) {
    float eps = 5.0f;
    float hL = get_height(x - eps, z, world_width, world_height);
    float hR = get_height(x + eps, z, world_width, world_height);
    float hD = get_height(x, z - eps, world_width, world_height);
    float hU = get_height(x, z + eps, world_width, world_height);
    
    // Cross product of tangent vectors
    nx = hL - hR;
    ny = 2.0f * eps;
    nz = hD - hU;
    
    // Normalize
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
 * Check if terrain at a position is walkable (not too steep).
 * 
 * @param x World X coordinate
 * @param z World Z coordinate
 * @param world_width Total world width
 * @param world_height Total world height/depth
 * @param max_slope_angle Maximum walkable slope in degrees (default 45)
 * @return True if terrain is walkable
 */
inline bool is_walkable(float x, float z, float world_width, float world_height,
                        float max_slope_angle = 45.0f) {
    float nx, ny, nz;
    get_normal(x, z, world_width, world_height, nx, ny, nz);
    
    // ny is the up component of the normal; compare against slope threshold
    float min_up = std::cos(max_slope_angle * 3.14159265f / 180.0f);
    return ny >= min_up;
}

} // namespace mmo::terrain

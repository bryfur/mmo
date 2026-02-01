#pragma once

#include "protocol/protocol.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace mmo::server {

struct GridCell {
    int x;
    int y;

    bool operator==(const GridCell& other) const {
        return x == other.x && y == other.y;
    }
};

struct GridCellHash {
    size_t operator()(const GridCell& cell) const {
        return std::hash<int>()(cell.x) ^ (std::hash<int>()(cell.y) << 1);
    }
};

// Not thread-safe: all access must occur on the game loop thread
class SpatialGrid {
public:
    explicit SpatialGrid(float cell_size = 500.0f);

    // Update entity position in grid
    void update_entity(uint32_t entity_id, float x, float y, protocol::EntityType type);

    // Remove entity from grid
    void remove_entity(uint32_t entity_id);

    // Query entities within radius of a point (single radius for all types)
    std::vector<uint32_t> query_radius(float center_x, float center_y, float radius) const;

    // Query entities with type-specific radii (smart filtering)
    std::vector<uint32_t> query_with_type_radii(
        float center_x, float center_y,
        float building_radius,
        float environment_radius,
        float player_radius,
        float npc_radius,
        float town_npc_radius
    ) const;

    // Clear all entities
    void clear();

    // Get cell size
    float cell_size() const { return cell_size_; }

private:
    GridCell get_cell(float x, float y) const;
    std::vector<GridCell> get_cells_in_radius(float center_x, float center_y, float radius) const;

    struct EntityInfo {
        GridCell cell;
        protocol::EntityType type;
    };

    float cell_size_;

    // Map: GridCell -> set of entity IDs in that cell
    std::unordered_map<GridCell, std::unordered_set<uint32_t>, GridCellHash> grid_;

    // Track which cell and type each entity is in
    std::unordered_map<uint32_t, EntityInfo> entity_info_;
};

} // namespace mmo::server

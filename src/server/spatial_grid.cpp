#include "spatial_grid.hpp"
#include <cmath>
#include <algorithm>

namespace mmo::server {

SpatialGrid::SpatialGrid(float cell_size)
    : cell_size_(cell_size) {
}

GridCell SpatialGrid::get_cell(float x, float y) const {
    return GridCell{
        static_cast<int>(std::floor(x / cell_size_)),
        static_cast<int>(std::floor(y / cell_size_))
    };
}

std::vector<GridCell> SpatialGrid::get_cells_in_radius(float center_x, float center_y, float radius) const {
    std::vector<GridCell> cells;

    // Calculate bounding box in grid coordinates
    int min_cell_x = static_cast<int>(std::floor((center_x - radius) / cell_size_));
    int max_cell_x = static_cast<int>(std::floor((center_x + radius) / cell_size_));
    int min_cell_y = static_cast<int>(std::floor((center_y - radius) / cell_size_));
    int max_cell_y = static_cast<int>(std::floor((center_y + radius) / cell_size_));

    // Add all cells in bounding box
    for (int cx = min_cell_x; cx <= max_cell_x; ++cx) {
        for (int cy = min_cell_y; cy <= max_cell_y; ++cy) {
            cells.push_back(GridCell{cx, cy});
        }
    }

    return cells;
}

void SpatialGrid::update_entity(uint32_t entity_id, float x, float y, protocol::EntityType type) {
    GridCell new_cell = get_cell(x, y);

    // Check if entity is already in a cell
    auto it = entity_info_.find(entity_id);
    if (it != entity_info_.end()) {
        GridCell old_cell = it->second.cell;

        // If cell hasn't changed, just update type
        if (old_cell == new_cell) {
            it->second.type = type;
            return;
        }

        // Remove from old cell
        auto old_cell_it = grid_.find(old_cell);
        if (old_cell_it != grid_.end()) {
            old_cell_it->second.erase(entity_id);
            // Remove cell from grid if empty
            if (old_cell_it->second.empty()) {
                grid_.erase(old_cell_it);
            }
        }
    }

    // Add to new cell
    grid_[new_cell].insert(entity_id);
    entity_info_[entity_id] = {new_cell, type};
}

void SpatialGrid::remove_entity(uint32_t entity_id) {
    auto it = entity_info_.find(entity_id);
    if (it == entity_info_.end()) {
        return;  // Entity not in grid
    }

    GridCell cell = it->second.cell;

    // Remove from cell
    auto cell_it = grid_.find(cell);
    if (cell_it != grid_.end()) {
        cell_it->second.erase(entity_id);
        // Remove cell from grid if empty
        if (cell_it->second.empty()) {
            grid_.erase(cell_it);
        }
    }

    // Remove from tracking map
    entity_info_.erase(it);
}

std::vector<uint32_t> SpatialGrid::query_radius(float center_x, float center_y, float radius) const {
    std::vector<uint32_t> result;

    // Get all cells that might contain entities in range
    auto cells = get_cells_in_radius(center_x, center_y, radius);

    // Collect all entities from those cells
    for (const auto& cell : cells) {
        auto it = grid_.find(cell);
        if (it != grid_.end()) {
            for (uint32_t entity_id : it->second) {
                result.push_back(entity_id);
            }
        }
    }

    return result;
}

std::vector<uint32_t> SpatialGrid::query_with_type_radii(
    float center_x, float center_y,
    float building_radius,
    float environment_radius,
    float player_radius,
    float npc_radius,
    float town_npc_radius
) const {
    std::vector<uint32_t> result;

    // Use max radius to get all cells that might contain any visible entities
    float max_radius = std::max({building_radius, environment_radius, player_radius, npc_radius, town_npc_radius});
    auto cells = get_cells_in_radius(center_x, center_y, max_radius);

    // Collect entities from those cells, filtering by type-specific radius
    for (const auto& cell : cells) {
        auto it = grid_.find(cell);
        if (it != grid_.end()) {
            for (uint32_t entity_id : it->second) {
                // Look up entity info to get type
                auto info_it = entity_info_.find(entity_id);
                if (info_it == entity_info_.end()) continue;

                // We need to get position to check distance, but we don't store it
                // For now, we'll add all entities in the cells and let the caller filter by distance
                // This is still an optimization because we only check nearby cells
                result.push_back(entity_id);
            }
        }
    }

    return result;
}

void SpatialGrid::clear() {
    grid_.clear();
    entity_info_.clear();
}

} // namespace mmo::server

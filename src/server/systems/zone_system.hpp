#pragma once

#include "server/ecs/game_components.hpp"
#include "server/game_config.hpp"
#include <entt/entt.hpp>
#include <string>
#include <random>

namespace mmo::server::systems {

namespace ecs = mmo::server::ecs;

class ZoneSystem {
public:
    explicit ZoneSystem(const GameConfig& config);

    /// Spawn initial monsters across all zones based on density
    void spawn_initial_monsters(entt::registry& registry,
                                 std::function<uint32_t()> next_id_fn,
                                 std::function<float(float, float)> height_fn);

    /// Respawn a monster when it dies (pick a zone-appropriate location and type)
    void respawn_monster(entt::registry& registry, entt::entity monster,
                         std::function<float(float, float)> height_fn);

    /// Get the zone name at a world position
    std::string get_zone_name(float x, float z) const;

    /// Get monster count target for all zones
    int total_monster_target() const { return total_target_; }

private:
    struct ZoneSpawnInfo {
        const ZoneConfig* zone = nullptr;
        int target_count = 0;
    };

    const GameConfig* config_ = nullptr;
    std::vector<ZoneSpawnInfo> zone_spawns_;
    int total_target_ = 0;
    std::mt19937 rng_;
};

} // namespace mmo::server::systems

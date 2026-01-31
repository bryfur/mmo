#pragma once

#include "protocol/protocol.hpp"
#include "game_types.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <mutex>
#include <random>

namespace mmo::server {

class World {
public:
    World();

    uint32_t add_player(const std::string& name, PlayerClass player_class);
    void remove_player(uint32_t player_id);
    void update_player_input(uint32_t player_id, const mmo::protocol::PlayerInput& input);

    void update(float dt);

    std::vector<mmo::protocol::NetEntityState> get_all_entities() const;
    entt::entity find_entity_by_network_id(uint32_t id) const;
    
    size_t player_count() const;
    size_t npc_count() const;
    
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    
private:
    void spawn_npcs();
    uint32_t generate_color(PlayerClass player_class);
    uint32_t next_network_id();
    
    mutable std::mutex mutex_;
    entt::registry registry_;
    uint32_t next_id_ = 1;
    std::mt19937 rng_;
};

} // namespace mmo::server

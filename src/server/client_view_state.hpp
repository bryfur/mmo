#pragma once

#include "protocol/protocol.hpp"
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <cstdint>

namespace mmo::server {

struct EntitySnapshot {
    mmo::protocol::NetEntityState state;
    std::chrono::steady_clock::time_point last_sent;
};

class ClientViewState {
public:
    explicit ClientViewState(uint32_t client_id);

    // Track which entities client knows about
    bool knows_entity(uint32_t entity_id) const;
    void add_known_entity(uint32_t entity_id, const mmo::protocol::NetEntityState& state);
    void remove_known_entity(uint32_t entity_id);

    // Get last state sent to client
    const mmo::protocol::NetEntityState* get_last_state(uint32_t entity_id) const;
    void update_last_state(uint32_t entity_id, const mmo::protocol::NetEntityState& state);

    // Get all known entity IDs
    const std::unordered_set<uint32_t>& known_entities() const { return known_entities_; }

    // Rate limiting
    bool can_send_update(uint32_t entity_id, float min_interval_sec) const;
    void mark_sent(uint32_t entity_id);

private:
    uint32_t client_id_;
    std::unordered_set<uint32_t> known_entities_;
    std::unordered_map<uint32_t, EntitySnapshot> last_sent_states_;
};

} // namespace mmo::server

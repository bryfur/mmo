#include "client_view_state.hpp"

namespace mmo::server {

ClientViewState::ClientViewState(uint32_t client_id)
    : client_id_(client_id) {
}

bool ClientViewState::knows_entity(uint32_t entity_id) const {
    return known_entities_.find(entity_id) != known_entities_.end();
}

void ClientViewState::add_known_entity(uint32_t entity_id, const mmo::protocol::NetEntityState& state) {
    known_entities_.insert(entity_id);
    EntitySnapshot snapshot;
    snapshot.state = state;
    snapshot.last_sent = std::chrono::steady_clock::now();
    last_sent_states_[entity_id] = snapshot;
}

void ClientViewState::remove_known_entity(uint32_t entity_id) {
    known_entities_.erase(entity_id);
    last_sent_states_.erase(entity_id);
}

const mmo::protocol::NetEntityState* ClientViewState::get_last_state(uint32_t entity_id) const {
    auto it = last_sent_states_.find(entity_id);
    if (it != last_sent_states_.end()) {
        return &it->second.state;
    }
    return nullptr;
}

void ClientViewState::update_last_state(uint32_t entity_id, const mmo::protocol::NetEntityState& state) {
    auto it = last_sent_states_.find(entity_id);
    if (it != last_sent_states_.end()) {
        it->second.state = state;
    } else {
        // Add if not present
        EntitySnapshot snapshot;
        snapshot.state = state;
        snapshot.last_sent = std::chrono::steady_clock::now();
        last_sent_states_[entity_id] = snapshot;
    }
}

bool ClientViewState::can_send_update(uint32_t entity_id, float min_interval_sec) const {
    auto it = last_sent_states_.find(entity_id);
    if (it == last_sent_states_.end()) {
        return true;  // Never sent, can send
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<float>(now - it->second.last_sent).count();
    return elapsed >= min_interval_sec;
}

void ClientViewState::mark_sent(uint32_t entity_id) {
    auto it = last_sent_states_.find(entity_id);
    if (it != last_sent_states_.end()) {
        it->second.last_sent = std::chrono::steady_clock::now();
    }
}

} // namespace mmo::server

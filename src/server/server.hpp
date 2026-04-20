#pragma once

#include "protocol/protocol.hpp"
#include "game_config.hpp"
#include "world.hpp"
#include "session.hpp"
#include "client_view_state.hpp"
#include <asio.hpp>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace mmo::server {

class Server {
public:
    using tcp = asio::ip::tcp;
    
    Server(asio::io_context& io_context, uint16_t port, const GameConfig& config);
    ~Server();
    
    void start();
    void stop();
    
    void on_client_connect(std::shared_ptr<Session> session, const std::string& name);
    void on_class_select(std::shared_ptr<Session> session, uint8_t class_index);
    void on_player_disconnect(uint32_t player_id);
    void on_player_input(uint32_t player_id, const mmo::protocol::PlayerInput& input);
    void on_npc_interact(std::shared_ptr<Session> session, uint32_t player_id, uint32_t npc_id);
    void on_quest_accept(uint32_t player_id, const std::string& quest_id);
    void on_quest_turnin(uint32_t player_id, const std::string& quest_id);
    void on_skill_use(uint32_t player_id, const std::string& skill_id, float dir_x, float dir_z);
    void on_talent_unlock(uint32_t player_id, const std::string& talent_id);
    void on_item_equip(uint32_t player_id, const std::string& item_id);
    void on_item_unequip(uint32_t player_id, const std::string& slot);
    void on_item_equip_by_slot(uint32_t player_id, uint8_t slot_index);
    void on_item_unequip_slot(uint32_t player_id, uint8_t equip_slot);
    void on_item_use(uint32_t player_id, uint8_t slot_index);
    void on_chat_send(uint32_t player_id, uint8_t channel, const std::string& message);
    void on_vendor_buy(uint32_t player_id, uint32_t npc_id, uint8_t stock_index, uint8_t quantity);
    void on_vendor_sell(uint32_t player_id, uint32_t npc_id, uint8_t inventory_slot, uint8_t quantity);

    void on_party_invite(uint32_t player_id, const std::string& target_name);
    void on_party_invite_respond(uint32_t player_id, uint32_t inviter_id, bool accept);
    void on_party_leave(uint32_t player_id);
    void on_party_kick(uint32_t player_id, uint32_t target_id);

    void broadcast(const std::vector<uint8_t>& data);
    void broadcast_except(const std::vector<uint8_t>& data, uint32_t exclude_id);
    void broadcast_system_chat(const std::string& message);
    
    World& world() { return world_; }
    
private:
    void accept();
    void game_loop();
    void broadcast_world_state();
    void send_entity_deltas();
    void send_progression_updates();
    void send_heightmap(std::shared_ptr<Session> session);

    // Quest availability
    void send_quest_availability(uint32_t player_id);

    // Send inventory state to a player
    void send_inventory_update(uint32_t player_id);

    // Send talent sync to a player
    void send_talent_sync(uint32_t player_id);

    // Send talent tree definition for a player's class
    void send_talent_tree(uint32_t player_id);

    // Send available skills to a player
    void send_skill_list(uint32_t player_id);

    // Party management
    void send_party_state(uint32_t player_id);
    void send_party_state_to_all(uint32_t party_id);
    struct Party {
        uint32_t id = 0;
        uint32_t leader_id = 0;
        std::vector<uint32_t> member_ids;
    };
    uint32_t find_party_id(uint32_t player_id) const;
    void disband_party_if_small(uint32_t party_id);

    // Delta compression helpers
    float get_update_interval(mmo::protocol::EntityType type) const;
    bool has_changes(const mmo::protocol::NetEntityState& current,
                     const mmo::protocol::NetEntityState& last) const;
    mmo::protocol::EntityDeltaUpdate create_delta(const mmo::protocol::NetEntityState& current,
                                                   const mmo::protocol::NetEntityState& last) const;

    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    const GameConfig& config_;
    World world_;

    std::unordered_map<uint32_t, std::shared_ptr<Session>> sessions_;
    std::mutex sessions_mutex_;

    // Per-client state tracking for delta compression
    std::unordered_map<uint32_t, std::unique_ptr<ClientViewState>> client_views_;
    std::mutex client_views_mutex_;

    asio::steady_timer tick_timer_;
    std::atomic<bool> running_{false};

    std::chrono::steady_clock::time_point last_tick_;
    std::chrono::steady_clock::time_point next_tick_time_{};

    // Heartbeat: send Ping every 5s, disconnect if no Pong within 15s
    float ping_timer_ = 0.0f;
    static constexpr float PING_INTERVAL = 5.0f;
    static constexpr float PONG_TIMEOUT = 15.0f;

    // Party membership
    std::unordered_map<uint32_t, Party> parties_;        // party_id -> Party
    std::unordered_map<uint32_t, uint32_t> player_party_;  // player_id -> party_id
    uint32_t next_party_id_ = 1;
    // Pending invites: inviter_id -> invitee_id (one pending per pair)
    std::unordered_map<uint32_t, uint32_t> pending_invites_;

    // Party state broadcast cadence
    float party_broadcast_timer_ = 0.0f;
    static constexpr float PARTY_BROADCAST_INTERVAL = 1.0f;
};

} // namespace mmo::server

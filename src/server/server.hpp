#pragma once

#include "common/protocol.hpp"
#include "game_config.hpp"
#include "world.hpp"
#include "session.hpp"
#include <asio.hpp>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace mmo {

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
    void on_player_input(uint32_t player_id, const PlayerInput& input);
    
    void broadcast(const std::vector<uint8_t>& data);
    void broadcast_except(const std::vector<uint8_t>& data, uint32_t exclude_id);
    
    World& world() { return world_; }
    
private:
    void accept();
    void game_loop();
    void broadcast_world_state();
    void send_heightmap(std::shared_ptr<Session> session);
    
    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    const GameConfig& config_;
    World world_;
    
    std::unordered_map<uint32_t, std::shared_ptr<Session>> sessions_;
    std::mutex sessions_mutex_;
    
    asio::steady_timer tick_timer_;
    std::atomic<bool> running_{false};
    
    std::chrono::steady_clock::time_point last_tick_;
};

} // namespace mmo

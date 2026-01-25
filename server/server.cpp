#include "server.hpp"
#include <iostream>

namespace mmo {

Server::Server(asio::io_context& io_context, uint16_t port)
    : io_context_(io_context)
    , acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    , tick_timer_(io_context)
    , last_tick_(std::chrono::steady_clock::now()) {
}

Server::~Server() {
    stop();
}

void Server::start() {
    running_ = true;
    accept();
    game_loop();
    std::cout << "Server started on port " << acceptor_.local_endpoint().port() << std::endl;
}

void Server::stop() {
    running_ = false;
    tick_timer_.cancel();
    acceptor_.close();
    
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        session->close();
    }
    sessions_.clear();
}

void Server::accept() {
    acceptor_.async_accept(
        [this](asio::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::cout << "New connection from " 
                          << socket.remote_endpoint().address().to_string() << std::endl;
                auto session = std::make_shared<Session>(std::move(socket), *this);
                session->start();
            }
            
            if (running_) {
                accept();
            }
        });
}

void Server::on_player_connect(std::shared_ptr<Session> session, const std::string& name, PlayerClass player_class) {
    uint32_t player_id = world_.add_player(name, player_class);
    session->set_player_id(player_id);
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[player_id] = session;
    }
    
    const char* class_name = "";
    switch (player_class) {
        case PlayerClass::Warrior: class_name = "Warrior"; break;
        case PlayerClass::Mage: class_name = "Mage"; break;
        case PlayerClass::Paladin: class_name = "Paladin"; break;
        case PlayerClass::Archer: class_name = "Archer"; break;
    }
    std::cout << "Player '" << name << "' (" << class_name << ") connected with ID " << player_id << std::endl;
    
    // Send connection accepted
    Packet accept_packet(MessageType::ConnectionAccepted);
    accept_packet.write_uint32(player_id);
    session->send(accept_packet.build());
    
    // Send heightmap data (before world state so client can position entities correctly)
    send_heightmap(session);
    
    // Send world state
    auto entities = world_.get_all_entities();
    Packet world_packet(MessageType::WorldState);
    world_packet.write_uint16(static_cast<uint16_t>(entities.size()));
    for (const auto& entity : entities) {
        world_packet.write_entity_state(entity);
    }
    session->send(world_packet.build());
    
    // Find the new player's state to broadcast
    for (const auto& entity : entities) {
        if (entity.id == player_id) {
            Packet join_packet(MessageType::PlayerJoined);
            join_packet.write_entity_state(entity);
            broadcast_except(join_packet.build(), player_id);
            break;
        }
    }
}

void Server::on_player_disconnect(uint32_t player_id) {
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.erase(player_id);
    }
    
    world_.remove_player(player_id);
    
    std::cout << "Player " << player_id << " disconnected" << std::endl;
    
    Packet leave_packet(MessageType::PlayerLeft);
    leave_packet.write_uint32(player_id);
    broadcast(leave_packet.build());
}

void Server::on_player_input(uint32_t player_id, const PlayerInput& input) {
    world_.update_player_input(player_id, input);
}

void Server::broadcast(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        if (session->is_open()) {
            session->send(data);
        }
    }
}

void Server::broadcast_except(const std::vector<uint8_t>& data, uint32_t exclude_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        if (id != exclude_id && session->is_open()) {
            session->send(data);
        }
    }
}

void Server::game_loop() {
    if (!running_) return;
    
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;
    
    world_.update(dt);
    broadcast_world_state();
    
    tick_timer_.expires_after(std::chrono::milliseconds(static_cast<int>(config::TICK_DURATION * 1000)));
    tick_timer_.async_wait([this](asio::error_code ec) {
        if (!ec && running_) {
            game_loop();
        }
    });
}

void Server::broadcast_world_state() {
    auto entities = world_.get_all_entities();
    if (entities.empty()) return;
    
    Packet state_packet(MessageType::WorldState);
    state_packet.write_uint16(static_cast<uint16_t>(entities.size()));
    for (const auto& entity : entities) {
        state_packet.write_entity_state(entity);
    }
    
    broadcast(state_packet.build());
}

void Server::send_heightmap(std::shared_ptr<Session> session) {
    const auto& heightmap = world_.heightmap();
    
    // Heightmap is too large for standard packet (uint16 payload size limit)
    // Send as raw data with custom header
    std::vector<uint8_t> data;
    
    // Header: message type + 4-byte payload size (for large data)
    data.push_back(static_cast<uint8_t>(MessageType::HeightmapChunk));
    uint32_t payload_size = static_cast<uint32_t>(heightmap.serialized_size());
    data.push_back(static_cast<uint8_t>(payload_size & 0xFF));
    data.push_back(static_cast<uint8_t>((payload_size >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((payload_size >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((payload_size >> 24) & 0xFF));
    
    // Serialize heightmap data
    heightmap.serialize(data);
    
    std::cout << "[Server] Sending heightmap to player " << session->player_id() 
              << " (" << data.size() / 1024 << " KB)" << std::endl;
    
    session->send(data);
}

} // namespace mmo

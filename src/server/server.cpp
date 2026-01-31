#include "server.hpp"
#include "asio/error_code.hpp"
#include "asio/io_context.hpp"
#include "protocol/protocol.hpp"
#include "server/game_config.hpp"
#include "server/game_types.hpp"
#include "server/session.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mmo::server {

using namespace mmo::protocol;

Server::Server(asio::io_context& io_context, uint16_t port, const GameConfig& config)
    : io_context_(io_context)
    , acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    , config_(config)
    , world_(config)
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

void Server::on_client_connect(std::shared_ptr<Session> session, const std::string& name) {
    session->set_player_name(name);
    std::cout << "Client '" << name << "' connected, sending class list" << std::endl;

    // Send connection accepted (player_id=0 means "not spawned yet, pick a class")
    Packet accept_packet(MessageType::ConnectionAccepted);
    accept_packet.write_uint32(0);
    session->send(accept_packet.build());

    // Send world config so client knows dimensions, tick rate, etc.
    {
        NetWorldConfig wc;
        wc.world_width = config_.world().width;
        wc.world_height = config_.world().height;
        wc.tick_rate = config_.server().tick_rate;

        Packet wc_packet(MessageType::WorldConfig);
        uint8_t buf[NetWorldConfig::serialized_size()];
        wc.serialize(buf);
        for (size_t i = 0; i < sizeof(buf); ++i) {
            wc_packet.write_uint8(buf[i]);
        }
        session->send(wc_packet.build());
    }

    // Send heightmap early so client can start loading terrain
    send_heightmap(session);

    // Build and send available classes from config
    Packet class_packet(MessageType::ClassList);
    class_packet.write_uint8(static_cast<uint8_t>(config_.class_count()));
    for (int i = 0; i < config_.class_count(); ++i) {
        class_packet.write_class_info(config_.build_class_info(i));
    }
    session->send(class_packet.build());
}

void Server::on_class_select(std::shared_ptr<Session> session, uint8_t class_index) {
    PlayerClass player_class = static_cast<PlayerClass>(std::min(class_index, static_cast<uint8_t>(config_.class_count() - 1)));
    std::string name = session->player_name();

    uint32_t player_id = world_.add_player(name, player_class);
    session->set_player_id(player_id);

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[player_id] = session;
    }

    const auto& cls = config_.get_class(class_index);
    std::cout << "Player '" << name << "' (" << cls.name << ") spawned with ID " << player_id << std::endl;

    // Send real player ID now
    Packet id_packet(MessageType::ConnectionAccepted);
    id_packet.write_uint32(player_id);
    session->send(id_packet.build());

    // Send world state
    auto entities = world_.get_all_entities();
    Packet world_packet(MessageType::WorldState);
    world_packet.write_uint16(static_cast<uint16_t>(entities.size()));
    for (const auto& entity : entities) {
        world_packet.write_entity_state(entity);
    }
    session->send(world_packet.build());

    // Broadcast new player to others
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
    
    float tick_duration = 1.0f / config_.server().tick_rate;
    tick_timer_.expires_after(std::chrono::milliseconds(static_cast<int>(tick_duration * 1000)));
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
    
    // Serialize heightmap into a standard packet (header now supports uint32_t size)
    std::vector<uint8_t> data;
    data.reserve(PacketHeader::size() + heightmap.serialized_size());

    PacketHeader header;
    header.type = MessageType::HeightmapChunk;
    header.payload_size = static_cast<uint32_t>(heightmap.serialized_size());

    data.resize(PacketHeader::size());
    header.serialize(data.data());
    heightmap.serialize(data);

    std::cout << "[Server] Sending heightmap to player " << session->player_id()
              << " (" << data.size() / 1024 << " KB)" << std::endl;

    session->send(data);
}

} // namespace mmo::server

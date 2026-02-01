#include "server.hpp"
#include "asio/error_code.hpp"
#include "asio/io_context.hpp"
#include "protocol/protocol.hpp"
#include "server/game_config.hpp"
#include "server/game_types.hpp"
#include "server/session.hpp"
#include "server/ecs/game_components.hpp"
#include <entt/entt.hpp>
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

    // Delta system will send EntityEnter for nearby entities on next tick
    // No need to send full WorldState - spatial filtering handles everything

    // Broadcast new player to others (they'll get EntityEnter for this new player)
    auto player_state = world_.get_entity_state(player_id);
    Packet join_packet(MessageType::PlayerJoined);
    join_packet.write_entity_state(player_state);
    broadcast_except(join_packet.build(), player_id);
}

void Server::on_player_disconnect(uint32_t player_id) {
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.erase(player_id);
    }

    {
        std::lock_guard<std::mutex> lock(client_views_mutex_);
        client_views_.erase(player_id);
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

    // Use delta compression instead of full world state broadcasts
    send_entity_deltas();
    // broadcast_world_state();  // Old method - kept for reference/rollback

    float tick_duration = 1.0f / config_.server().tick_rate;
    tick_timer_.expires_after(std::chrono::milliseconds(static_cast<int>(tick_duration * 1000)));
    tick_timer_.async_wait([this](asio::error_code ec) {
        if (!ec && running_) {
            game_loop();
        }
    });
}

void Server::broadcast_world_state() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Per-client spatial filtering
    for (auto& [client_id, session] : sessions_) {
        if (!session->is_open()) continue;

        // Get client's player entity and position
        auto player_entity = world_.find_entity_by_network_id(client_id);
        if (player_entity == entt::null) continue;

        const auto& transform = world_.registry().get<ecs::Transform>(player_entity);
        const auto& network_config = config_.network();

        // Query entities near this client using spatial grid
        // Use max view distance to get all potentially visible entities
        auto nearby_ids = world_.query_entities_near(transform.x, transform.z, network_config.max_view_distance());

        // Get entity states for nearby entities only and filter by per-entity view distance
        std::vector<protocol::NetEntityState> visible_entities;

        for (uint32_t entity_id : nearby_ids) {
            // Get entity state for this specific entity (much faster than querying all!)
            auto entity_state = world_.get_entity_state(entity_id);
            if (entity_state.id == 0) continue;  // Skip if entity not found

            // Calculate distance to client
            float dx = entity_state.x - transform.x;
            float dz = entity_state.z - transform.z;
            float dist_sq = dx*dx + dz*dz;

            // Get view distance for this entity type (directly from type, no lookup!)
            float view_dist = network_config.get_view_distance_for_type(entity_state.type);

            // Check if within view distance
            if (dist_sq <= view_dist * view_dist) {
                visible_entities.push_back(entity_state);
            }
        }

        // Send WorldState with only visible entities to this client
        if (!visible_entities.empty()) {
            Packet packet(MessageType::WorldState);
            packet.write_uint16(static_cast<uint16_t>(visible_entities.size()));
            for (const auto& state : visible_entities) {
                packet.write_entity_state(state);
            }
            session->send(packet.build());
        }
    }
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

void Server::send_entity_deltas() {
    std::lock_guard<std::mutex> sessions_lock(sessions_mutex_);
    std::lock_guard<std::mutex> views_lock(client_views_mutex_);

    for (auto& [client_id, session] : sessions_) {
        if (!session->is_open()) continue;

        // Get or create client view state
        if (client_views_.find(client_id) == client_views_.end()) {
            client_views_[client_id] = std::make_unique<ClientViewState>(client_id);
        }
        auto& view = client_views_[client_id];

        // Get client position
        auto player_entity = world_.find_entity_by_network_id(client_id);
        if (player_entity == entt::null) continue;

        const auto& transform = world_.registry().get<ecs::Transform>(player_entity);
        const auto& network_config = config_.network();

        // Query visible entities (spatial partitioning with tiered view distances)
        auto nearby_ids = world_.query_entities_near(transform.x, transform.z, network_config.max_view_distance());

        // Filter by per-entity view distance
        std::vector<uint32_t> visible_now;

        for (uint32_t entity_id : nearby_ids) {
            auto entity_state = world_.get_entity_state(entity_id);
            if (entity_state.id == 0) continue;

            float dx = entity_state.x - transform.x;
            float dz = entity_state.z - transform.z;
            float dist_sq = dx*dx + dz*dz;

            float view_dist = network_config.get_view_distance_for_type(entity_state.type);

            if (dist_sq <= view_dist * view_dist) {
                visible_now.push_back(entity_id);
            }
        }

        const auto& known = view->known_entities();

        // ENTER: in visible_now but not in known
        for (uint32_t id : visible_now) {
            if (!view->knows_entity(id)) {
                auto state = world_.get_entity_state(id);
                Packet packet(MessageType::EntityEnter);
                packet.write_entity_state(state);
                session->send(packet.build());
                view->add_known_entity(id, state);
            }
        }

        // EXIT: in known but not in visible_now
        std::vector<uint32_t> to_remove;
        for (uint32_t id : known) {
            if (std::find(visible_now.begin(), visible_now.end(), id) == visible_now.end()) {
                Packet packet(MessageType::EntityExit);
                packet.write_uint32(id);
                session->send(packet.build());
                to_remove.push_back(id);
            }
        }
        for (uint32_t id : to_remove) {
            view->remove_known_entity(id);
        }

        // UPDATE: in both (check for changes)
        for (uint32_t id : visible_now) {
            if (!view->knows_entity(id)) continue;  // Just entered, skip update

            auto current_state = world_.get_entity_state(id);
            auto last_state = view->get_last_state(id);
            if (!last_state) continue;

            // Check if anything changed
            if (!has_changes(current_state, *last_state)) {
                continue;  // No update needed
            }

            // Rate limiting
            float min_interval = get_update_interval(current_state.type);
            if (!view->can_send_update(id, min_interval)) {
                continue;  // Too soon since last update
            }

            // Build delta update
            auto delta = create_delta(current_state, *last_state);

            // Only send if there are actual changes flagged
            if (delta.flags != 0) {
                // Serialize delta to buffer
                std::vector<uint8_t> delta_data;
                delta.serialize(delta_data);

                // Build packet with header + delta payload
                std::vector<uint8_t> packet_data;
                packet_data.reserve(PacketHeader::size() + delta_data.size());

                PacketHeader header;
                header.type = MessageType::EntityUpdate;
                header.payload_size = static_cast<uint32_t>(delta_data.size());

                packet_data.resize(PacketHeader::size());
                header.serialize(packet_data.data());
                packet_data.insert(packet_data.end(), delta_data.begin(), delta_data.end());

                session->send(packet_data);

                view->update_last_state(id, current_state);
                view->mark_sent(id);
            }
        }
    }
}

float Server::get_update_interval(protocol::EntityType type) const {
    // Rate limiting: max update frequency per entity type
    switch(type) {
        case protocol::EntityType::Building:    return 999.0f;  // Never (static)
        case protocol::EntityType::Environment: return 999.0f;  // Never (static)
        case protocol::EntityType::Player:      return 0.0167f;   // 20 Hz max
        case protocol::EntityType::NPC:         return 0.0167f;   // 60 Hz max
        case protocol::EntityType::TownNPC:     return 0.0167f;    // 5 Hz max
        default: return 0.05f;
    }
}

bool Server::has_changes(const protocol::NetEntityState& current,
                         const protocol::NetEntityState& last) const {
    return current.x != last.x ||
           current.y != last.y ||
           current.z != last.z ||
           current.vx != last.vx ||
           current.vy != last.vy ||
           current.health != last.health ||
           current.is_attacking != last.is_attacking ||
           current.attack_dir_x != last.attack_dir_x ||
           current.attack_dir_y != last.attack_dir_y ||
           current.rotation != last.rotation;
}

protocol::EntityDeltaUpdate Server::create_delta(const protocol::NetEntityState& current,
                                                  const protocol::NetEntityState& last) const {
    protocol::EntityDeltaUpdate delta;
    delta.id = current.id;
    delta.flags = 0;

    if (current.x != last.x || current.y != last.y || current.z != last.z) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_POSITION;
        delta.x = current.x;
        delta.y = current.y;
        delta.z = current.z;
    }

    if (current.vx != last.vx || current.vy != last.vy) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_VELOCITY;
        delta.vx = current.vx;
        delta.vy = current.vy;
    }

    if (current.health != last.health) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_HEALTH;
        delta.health = current.health;
    }

    if (current.is_attacking != last.is_attacking) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_ATTACKING;
        delta.is_attacking = current.is_attacking ? 1 : 0;
    }

    if (current.attack_dir_x != last.attack_dir_x ||
        current.attack_dir_y != last.attack_dir_y) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_ATTACK_DIR;
        delta.attack_dir_x = current.attack_dir_x;
        delta.attack_dir_y = current.attack_dir_y;
    }

    if (current.rotation != last.rotation) {
        delta.flags |= protocol::EntityDeltaUpdate::FLAG_ROTATION;
        delta.rotation = current.rotation;
    }

    return delta;
}

} // namespace mmo::server

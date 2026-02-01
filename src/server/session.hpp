#pragma once

#include "protocol/protocol.hpp"
#include <asio.hpp>
#include <memory>
#include <functional>
#include <array>

namespace mmo::server {

class Server;

class Session : public std::enable_shared_from_this<Session> {
public:
    using tcp = asio::ip::tcp;
    
    Session(tcp::socket socket, Server& server);
    ~Session();
    
    void start();
    void send(const std::vector<uint8_t>& data);
    void close();
    
    uint32_t player_id() const { return player_id_; }
    void set_player_id(uint32_t id) { player_id_ = id; }
    const std::string& player_name() const { return player_name_; }
    void set_player_name(const std::string& name) { player_name_ = name; }

    bool is_open() const { return socket_.is_open(); }
    
private:
    void read_header();
    void read_payload();
    void handle_packet();
    void do_write();
    
    tcp::socket socket_;
    Server& server_;
    uint32_t player_id_ = 0;
    std::string player_name_;
    
    // Read buffer
    std::array<uint8_t, mmo::protocol::PacketHeader::serialized_size()> header_buffer_;
    std::vector<uint8_t> payload_buffer_;
    mmo::protocol::PacketHeader current_header_;
    
    // Write queue
    std::vector<std::vector<uint8_t>> write_queue_;
    bool writing_ = false;
    std::mutex write_mutex_;
};

} // namespace mmo::server

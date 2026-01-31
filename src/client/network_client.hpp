#pragma once

#include "protocol/protocol.hpp"
#include <asio.hpp>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

namespace mmo::client {

class NetworkClient {
public:
    using tcp = asio::ip::tcp;
    using MessageCallback = std::function<void(mmo::protocol::MessageType, const std::vector<uint8_t>&)>;
    
    NetworkClient();
    ~NetworkClient();
    
    bool connect(const std::string& host, uint16_t port, const std::string& player_name);
    void disconnect();
    bool is_connected() const { return connected_; }

    void send_class_select(uint8_t class_index);
    void send_input(const mmo::protocol::PlayerInput& input);
    
    void set_message_callback(MessageCallback callback) { message_callback_ = callback; }
    
    // Process received messages on main thread
    void poll_messages();
    
    uint32_t local_player_id() const { return local_player_id_; }
    
private:
    void io_thread_func();
    void read_header();
    void read_payload();
    void handle_message();
    void do_write();
    
    asio::io_context io_context_;
    tcp::socket socket_;
    std::unique_ptr<asio::io_context::work> work_;
    std::thread io_thread_;
    
    std::atomic<bool> connected_{false};
    uint32_t local_player_id_ = 0;
    
    // Read buffer
    std::array<uint8_t, mmo::protocol::PacketHeader::size()> header_buffer_;
    std::vector<uint8_t> payload_buffer_;
    mmo::protocol::PacketHeader current_header_;
    
    // Write queue
    std::queue<std::vector<uint8_t>> write_queue_;
    std::mutex write_mutex_;
    bool writing_ = false;
    
    // Message queue for main thread
    struct ReceivedMessage {
        mmo::protocol::MessageType type;
        std::vector<uint8_t> payload;
    };
    std::queue<ReceivedMessage> message_queue_;
    std::mutex message_mutex_;
    
    MessageCallback message_callback_;
};

} // namespace mmo::client

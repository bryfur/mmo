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

    // Debug stats
    struct NetworkStats {
        float bytes_sent_per_sec = 0.0f;
        float bytes_recv_per_sec = 0.0f;
        float packets_sent_per_sec = 0.0f;
        float packets_recv_per_sec = 0.0f;
        uint32_t message_queue_size = 0;
    };

    void set_collect_stats(bool enabled) { collect_stats_ = enabled; }
    const NetworkStats& network_stats() const { return network_stats_; }

private:
    void update_stats();
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
    std::array<uint8_t, mmo::protocol::PacketHeader::serialized_size()> header_buffer_;
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

    // Debug stats accumulators (written from IO thread, read from main thread)
    std::atomic<uint64_t> bytes_sent_total_{0};
    std::atomic<uint64_t> bytes_recv_total_{0};
    std::atomic<uint32_t> packets_sent_total_{0};
    std::atomic<uint32_t> packets_recv_total_{0};

    // Snapshot for per-second rate calculation
    uint64_t prev_bytes_sent_{0};
    uint64_t prev_bytes_recv_{0};
    uint32_t prev_packets_sent_{0};
    uint32_t prev_packets_recv_{0};
    uint64_t prev_stats_time_ms_{0};
    bool collect_stats_ = false;
    NetworkStats network_stats_;
};

} // namespace mmo::client

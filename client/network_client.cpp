#include "network_client.hpp"
#include <iostream>

namespace mmo {

NetworkClient::NetworkClient()
    : socket_(io_context_) {
}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& host, uint16_t port, const std::string& player_name, PlayerClass player_class) {
    try {
        tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        
        asio::connect(socket_, endpoints);
        connected_ = true;
        
        // Start IO thread
        work_ = std::make_unique<asio::io_context::work>(io_context_);
        io_thread_ = std::thread(&NetworkClient::io_thread_func, this);
        
        // Start reading
        read_header();
        
        // Send connect message with player name and class
        Packet connect_packet(MessageType::Connect);
        connect_packet.write_string(player_name, 32);
        connect_packet.write_uint8(static_cast<uint8_t>(player_class));
        
        auto data = connect_packet.build();
        {
            std::lock_guard<std::mutex> lock(write_mutex_);
            write_queue_.push(data);
            if (!writing_) {
                writing_ = true;
                do_write();
            }
        }
        
        std::cout << "Connected to server " << host << ":" << port << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        return false;
    }
}

void NetworkClient::disconnect() {
    if (!connected_) return;
    
    connected_ = false;
    
    // Send disconnect message
    Packet disconnect_packet(MessageType::Disconnect);
    auto data = disconnect_packet.build();
    
    asio::error_code ec;
    asio::write(socket_, asio::buffer(data), ec);
    
    socket_.close(ec);
    work_.reset();
    io_context_.stop();
    
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    
    std::cout << "Disconnected from server" << std::endl;
}

void NetworkClient::send_input(const PlayerInput& input) {
    if (!connected_) return;
    
    Packet input_packet(MessageType::PlayerInput);
    // Write full input including attack direction
    std::vector<uint8_t> input_data;
    input.serialize(input_data);
    for (uint8_t b : input_data) {
        input_packet.write_uint8(b);
    }
    
    auto data = input_packet.build();
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_queue_.push(data);
        if (!writing_) {
            writing_ = true;
            do_write();
        }
    }
}

void NetworkClient::poll_messages() {
    std::queue<ReceivedMessage> messages;
    {
        std::lock_guard<std::mutex> lock(message_mutex_);
        std::swap(messages, message_queue_);
    }
    
    while (!messages.empty()) {
        auto& msg = messages.front();
        if (message_callback_) {
            message_callback_(msg.type, msg.payload);
        }
        messages.pop();
    }
}

void NetworkClient::io_thread_func() {
    try {
        io_context_.run();
    } catch (const std::exception& e) {
        std::cerr << "IO thread error: " << e.what() << std::endl;
    }
}

void NetworkClient::read_header() {
    asio::async_read(socket_,
        asio::buffer(header_buffer_),
        [this](asio::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                current_header_.deserialize(header_buffer_.data());
                if (current_header_.payload_size > 0) {
                    payload_buffer_.resize(current_header_.payload_size);
                    read_payload();
                } else {
                    handle_message();
                    read_header();
                }
            } else if (connected_) {
                std::cerr << "Read error: " << ec.message() << std::endl;
                connected_ = false;
            }
        });
}

void NetworkClient::read_payload() {
    asio::async_read(socket_,
        asio::buffer(payload_buffer_),
        [this](asio::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                handle_message();
                read_header();
            } else if (connected_) {
                std::cerr << "Payload read error: " << ec.message() << std::endl;
                connected_ = false;
            }
        });
}

void NetworkClient::handle_message() {
    // Handle connection accepted immediately to get player ID
    if (current_header_.type == MessageType::ConnectionAccepted && payload_buffer_.size() >= 4) {
        std::memcpy(&local_player_id_, payload_buffer_.data(), sizeof(local_player_id_));
        std::cout << "Assigned player ID: " << local_player_id_ << std::endl;
    }
    
    // Queue message for main thread
    std::lock_guard<std::mutex> lock(message_mutex_);
    message_queue_.push({current_header_.type, payload_buffer_});
}

void NetworkClient::do_write() {
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }
    
    auto& front = write_queue_.front();
    asio::async_write(socket_,
        asio::buffer(front),
        [this](asio::error_code ec, std::size_t /*length*/) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (!ec) {
                write_queue_.pop();
                if (!write_queue_.empty()) {
                    do_write();
                } else {
                    writing_ = false;
                }
            } else {
                std::cerr << "Write error: " << ec.message() << std::endl;
                while (!write_queue_.empty()) write_queue_.pop();
                writing_ = false;
            }
        });
}

} // namespace mmo

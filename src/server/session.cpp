#include "session.hpp"
#include "asio/buffer.hpp"
#include "asio/error_code.hpp"
#include "asio/impl/read.hpp"
#include "asio/impl/write.hpp"
#include "protocol/protocol.hpp"
#include "server.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

namespace mmo::server {

using namespace mmo::protocol;

Session::Session(tcp::socket socket, Server& server)
    : socket_(std::move(socket))
    , server_(server) {
}

Session::~Session() {
    if (player_id_ != 0) {
        server_.on_player_disconnect(player_id_);
    }
}

void Session::start() {
    read_header();
}

void Session::send(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push_back(data);
    if (!writing_) {
        writing_ = true;
        do_write();
    }
}

void Session::close() {
    asio::error_code ec;
    socket_.close(ec);
}

void Session::read_header() {
    auto self = shared_from_this();
    asio::async_read(socket_,
        asio::buffer(header_buffer_),
        [this, self](asio::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                current_header_.deserialize(header_buffer_);
                if (current_header_.payload_size > 0) {
                    payload_buffer_.resize(current_header_.payload_size);
                    read_payload();
                } else {
                    handle_packet();
                    read_header();
                }
            } else {
                std::cout << "Session read error: " << ec.message() << std::endl;
                close();
            }
        });
}

void Session::read_payload() {
    auto self = shared_from_this();
    asio::async_read(socket_,
        asio::buffer(payload_buffer_),
        [this, self](asio::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                handle_packet();
                read_header();
            } else {
                std::cout << "Session payload read error: " << ec.message() << std::endl;
                close();
            }
        });
}

void Session::handle_packet() {
    switch (current_header_.type) {
        case MessageType::Connect: {
            ConnectMsg msg;
            if (current_header_.payload_size >= ConnectMsg::serialized_size()) {
                msg.deserialize(payload_buffer_);
            }
            std::string name(msg.name, strnlen(msg.name, sizeof(msg.name)));
            if (name.empty()) name = "Player";
            server_.on_client_connect(shared_from_this(), name);
            break;
        }

        case MessageType::ClassSelect: {
            if (current_header_.payload_size >= ClassSelectMsg::serialized_size()) {
                ClassSelectMsg msg;
                msg.deserialize(payload_buffer_);
                server_.on_class_select(shared_from_this(), msg.class_index);
            }
            break;
        }

        case MessageType::Disconnect: {
            close();
            break;
        }

        case MessageType::PlayerInput: {
            if (current_header_.payload_size >= 1 && player_id_ != 0) {
                PlayerInput input;
                input.deserialize(payload_buffer_);
                server_.on_player_input(player_id_, input);
            }
            break;
        }

        default:
            std::cout << "Unknown message type: " << static_cast<int>(current_header_.type) << std::endl;
            break;
    }
}

void Session::do_write() {
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }
    
    auto self = shared_from_this();
    auto& front = write_queue_.front();
    asio::async_write(socket_,
        asio::buffer(front),
        [this, self](asio::error_code ec, std::size_t /*length*/) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (!ec) {
                write_queue_.erase(write_queue_.begin());
                if (!write_queue_.empty()) {
                    do_write();
                } else {
                    writing_ = false;
                }
            } else {
                std::cout << "Session write error: " << ec.message() << std::endl;
                write_queue_.clear();
                writing_ = false;
                close();
            }
        });
}

} // namespace mmo::server

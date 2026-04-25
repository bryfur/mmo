#include "session.hpp"
#include "asio/buffer.hpp"
#include "asio/error_code.hpp"
#include "asio/impl/read.hpp"
#include "asio/impl/write.hpp"
#include "protocol/protocol.hpp"
#include "server.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mmo::server {

using namespace mmo::protocol;

Session::Session(tcp::socket socket, Server& server) : socket_(std::move(socket)), server_(server) {}

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
    asio::async_read(socket_, asio::buffer(header_buffer_), [this, self](asio::error_code ec, std::size_t /*length*/) {
        if (!ec) {
            current_header_.deserialize(header_buffer_);
            constexpr uint32_t MAX_PAYLOAD_SIZE = 4 * 1024 * 1024; // 4 MB
            if (current_header_.payload_size > MAX_PAYLOAD_SIZE) {
                std::cerr << "Session: payload too large (" << current_header_.payload_size << " bytes), disconnecting"
                          << '\n';
                close();
                return;
            }
            if (current_header_.payload_size > 0) {
                payload_buffer_.resize(current_header_.payload_size);
                read_payload();
            } else {
                dispatch_packet();
                read_header();
            }
        } else {
            std::cout << "Session read error: " << ec.message() << '\n';
            close();
        }
    });
}

void Session::read_payload() {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(payload_buffer_), [this, self](asio::error_code ec, std::size_t /*length*/) {
        if (!ec) {
            dispatch_packet();
            read_header();
        } else {
            std::cout << "Session payload read error: " << ec.message() << '\n';
            close();
        }
    });
}

void Session::dispatch_packet() {
    handle_packet(current_header_, payload_buffer_);
}

void Session::handle_packet(const PacketHeader& header, const std::vector<uint8_t>& payload_buffer) {
    switch (header.type) {
        case MessageType::Connect: {
            ConnectMsg msg;
            if (header.payload_size >= ConnectMsg::serialized_size()) {
                msg.deserialize(payload_buffer);
            }
            if (msg.protocol_version != PROTOCOL_VERSION) {
                std::cout << "Protocol version mismatch: client=" << msg.protocol_version
                          << " server=" << PROTOCOL_VERSION << ", disconnecting" << '\n';
                close();
                break;
            }
            std::string name(msg.name, strnlen(msg.name, sizeof(msg.name)));
            if (name.empty()) {
                name = "Player";
            }
            server_.on_client_connect(shared_from_this(), name);
            break;
        }

        case MessageType::ClassSelect: {
            if (header.payload_size >= ClassSelectMsg::serialized_size()) {
                ClassSelectMsg msg;
                msg.deserialize(payload_buffer);
                server_.on_class_select(shared_from_this(), msg.class_index);
            }
            break;
        }

        case MessageType::Disconnect: {
            close();
            break;
        }

        case MessageType::PlayerInput: {
            if (header.payload_size >= 1 && player_id_ != 0) {
                PlayerInput input;
                input.deserialize(payload_buffer);
                server_.on_player_input(player_id_, input);
            }
            break;
        }

        case MessageType::NPCInteract: {
            if (header.payload_size >= NPCInteractMsg::serialized_size() && player_id_ != 0) {
                NPCInteractMsg msg;
                msg.deserialize(payload_buffer);
                server_.on_npc_interact(shared_from_this(), player_id_, msg.npc_id);
            }
            break;
        }

        case MessageType::QuestAccept: {
            if (header.payload_size >= QuestAcceptMsg::serialized_size() && player_id_ != 0) {
                QuestAcceptMsg msg;
                msg.deserialize(payload_buffer);
                std::string quest_id(msg.quest_id, strnlen(msg.quest_id, sizeof(msg.quest_id)));
                server_.on_quest_accept(player_id_, quest_id);
            }
            break;
        }

        case MessageType::QuestTurnIn: {
            if (header.payload_size >= QuestTurnInMsg::serialized_size() && player_id_ != 0) {
                QuestTurnInMsg msg;
                msg.deserialize(payload_buffer);
                std::string quest_id(msg.quest_id, strnlen(msg.quest_id, sizeof(msg.quest_id)));
                server_.on_quest_turnin(player_id_, quest_id);
            }
            break;
        }

        case MessageType::SkillUse: {
            if (header.payload_size >= SkillUseMsg::serialized_size() && player_id_ != 0) {
                SkillUseMsg msg;
                msg.deserialize(payload_buffer);
                std::string skill_id(msg.skill_id, strnlen(msg.skill_id, sizeof(msg.skill_id)));
                server_.on_skill_use(player_id_, skill_id, msg.dir_x, msg.dir_z);
            }
            break;
        }

        case MessageType::TalentUnlock: {
            if (header.payload_size >= TalentUnlockMsg::serialized_size() && player_id_ != 0) {
                TalentUnlockMsg msg;
                msg.deserialize(payload_buffer);
                std::string talent_id(msg.talent_id, strnlen(msg.talent_id, sizeof(msg.talent_id)));
                server_.on_talent_unlock(player_id_, talent_id);
            }
            break;
        }

        case MessageType::ItemEquip: {
            if (header.payload_size >= ItemEquipMsg::serialized_size() && player_id_ != 0) {
                ItemEquipMsg msg;
                msg.deserialize(payload_buffer);
                // Look up item_id from inventory slot index
                server_.on_item_equip_by_slot(player_id_, msg.slot_index);
            }
            break;
        }

        case MessageType::ItemUnequip: {
            if (header.payload_size >= ItemUnequipMsg::serialized_size() && player_id_ != 0) {
                ItemUnequipMsg msg;
                msg.deserialize(payload_buffer);
                // equip_slot: 0 = weapon, 1 = armor
                server_.on_item_unequip_slot(player_id_, msg.equip_slot);
            }
            break;
        }

        case MessageType::ItemUse: {
            if (header.payload_size >= ItemUseMsg::serialized_size() && player_id_ != 0) {
                ItemUseMsg msg;
                msg.deserialize(payload_buffer);
                server_.on_item_use(player_id_, msg.slot_index);
            }
            break;
        }

        case MessageType::Pong: {
            mark_pong();
            break;
        }

        case MessageType::ChatSend: {
            if (header.payload_size >= ChatSendMsg::serialized_size() && player_id_ != 0) {
                ChatSendMsg msg;
                msg.deserialize(payload_buffer);
                std::string text(msg.message, strnlen(msg.message, sizeof(msg.message)));
                server_.on_chat_send(player_id_, msg.channel, text);
            }
            break;
        }

        case MessageType::VendorBuy: {
            if (header.payload_size >= VendorBuyMsg::serialized_size() && player_id_ != 0) {
                VendorBuyMsg msg;
                msg.deserialize(payload_buffer);
                server_.on_vendor_buy(player_id_, msg.npc_id, msg.stock_index, msg.quantity);
            }
            break;
        }

        case MessageType::VendorSell: {
            if (header.payload_size >= VendorSellMsg::serialized_size() && player_id_ != 0) {
                VendorSellMsg msg;
                msg.deserialize(payload_buffer);
                server_.on_vendor_sell(player_id_, msg.npc_id, msg.inventory_slot, msg.quantity);
            }
            break;
        }

        case MessageType::PartyInvite: {
            if (header.payload_size >= PartyInviteMsg::serialized_size() && player_id_ != 0) {
                PartyInviteMsg msg;
                msg.deserialize(payload_buffer);
                std::string target(msg.target_name, strnlen(msg.target_name, sizeof(msg.target_name)));
                server_.on_party_invite(player_id_, target);
            }
            break;
        }

        case MessageType::PartyInviteRespond: {
            if (header.payload_size >= PartyInviteRespondMsg::serialized_size() && player_id_ != 0) {
                PartyInviteRespondMsg msg;
                msg.deserialize(payload_buffer);
                server_.on_party_invite_respond(player_id_, msg.inviter_id, msg.accept != 0);
            }
            break;
        }

        case MessageType::PartyLeave: {
            if (player_id_ != 0) {
                server_.on_party_leave(player_id_);
            }
            break;
        }

        case MessageType::PartyKick: {
            if (header.payload_size >= PartyKickMsg::serialized_size() && player_id_ != 0) {
                PartyKickMsg msg;
                msg.deserialize(payload_buffer);
                server_.on_party_kick(player_id_, msg.target_id);
            }
            break;
        }

        case MessageType::CraftRequest: {
            if (header.payload_size >= CraftRequestMsg::serialized_size() && player_id_ != 0) {
                CraftRequestMsg msg;
                msg.deserialize(payload_buffer);
                std::string rid(msg.recipe_id, strnlen(msg.recipe_id, sizeof(msg.recipe_id)));
                server_.on_craft_request(player_id_, rid);
            }
            break;
        }

        default:
            std::cout << "Unknown message type: " << static_cast<int>(header.type) << '\n';
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
    asio::async_write(socket_, asio::buffer(front), [this, self](asio::error_code ec, std::size_t /*length*/) {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (!ec) {
            write_queue_.pop_front();
            if (!write_queue_.empty()) {
                do_write();
            } else {
                writing_ = false;
            }
        } else {
            std::cout << "Session write error: " << ec.message() << '\n';
            write_queue_.clear();
            writing_ = false;
            close();
        }
    });
}

} // namespace mmo::server

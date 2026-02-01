#pragma once

#include "serializable.hpp"
#include "message_type.hpp"
#include <cstdint>
#include <vector>

namespace mmo::protocol {

struct PacketHeader : Serializable<PacketHeader> {
    MessageType type;
    uint32_t payload_size;

    static constexpr size_t serialized_size() { return sizeof(MessageType) + sizeof(uint32_t); }

    void serialize_impl(BufferWriter& w) const {
        w.write(static_cast<uint8_t>(type));
        w.write(payload_size);
    }

    void deserialize_impl(BufferReader& r) {
        type = static_cast<MessageType>(r.read<uint8_t>());
        payload_size = r.read<uint32_t>();
    }
};

// Build a ready-to-send packet: header (5 bytes) + payload
inline std::vector<uint8_t> build_packet(MessageType type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> data;
    data.reserve(PacketHeader::serialized_size() + payload.size());
    BufferWriter w(data);
    PacketHeader hdr;
    hdr.type = type;
    hdr.payload_size = static_cast<uint32_t>(payload.size());
    hdr.serialize(w);
    w.write_bytes(payload);
    return data;
}

// Build a ready-to-send packet from a Serializable message (no intermediate buffer)
template<typename T>
std::vector<uint8_t> build_packet(MessageType type, const T& msg) {
    std::vector<uint8_t> data;
    data.reserve(PacketHeader::serialized_size() + msg.serialized_size());
    BufferWriter w(data);
    PacketHeader hdr;
    hdr.type = type;
    hdr.payload_size = static_cast<uint32_t>(msg.serialized_size());
    hdr.serialize(w);
    msg.serialize(w);
    return data;
}

// Build a ready-to-send packet from an array of Serializable items (length-prefixed)
template<typename T>
std::vector<uint8_t> build_packet(MessageType type, const std::vector<T>& items) {
    uint32_t payload_size = static_cast<uint32_t>(sizeof(uint16_t) + items.size() * T::serialized_size());
    std::vector<uint8_t> data;
    data.reserve(PacketHeader::serialized_size() + payload_size);
    BufferWriter w(data);
    PacketHeader hdr;
    hdr.type = type;
    hdr.payload_size = payload_size;
    hdr.serialize(w);
    w.write_array(items);
    return data;
}

// Default port for CLI usage (not game logic)
constexpr uint16_t DEFAULT_PORT = 7777;

} // namespace mmo::protocol

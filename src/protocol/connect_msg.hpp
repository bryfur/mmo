#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

// Protocol version - bump when wire format changes.
// Server rejects clients whose version does not match.
static constexpr uint16_t PROTOCOL_VERSION = 1;

// Client -> Server: initial connection with player name
struct ConnectMsg : Serializable<ConnectMsg> {
    uint16_t protocol_version = PROTOCOL_VERSION;
    char name[32] = {};

    // Expected wire size: 2 (version) + 32 (name) = 34 bytes
    static constexpr size_t serialized_size() { return sizeof(uint16_t) + 32; }

    void serialize_impl(BufferWriter& w) const {
        w.write(protocol_version);
        w.write_bytes(name, 32);
    }

    void deserialize_impl(BufferReader& r) {
        protocol_version = r.read<uint16_t>();
        r.read_bytes(name, 32);
    }
};

} // namespace mmo::protocol

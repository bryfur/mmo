#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

// Client → Server: initial connection with player name
struct ConnectMsg : Serializable<ConnectMsg> {
    char name[32] = {};

    static constexpr size_t serialized_size() { return 32; }

    void serialize_impl(BufferWriter& w) const {
        w.write_bytes(name, 32);
    }

    void deserialize_impl(BufferReader& r) {
        r.read_bytes(name, 32);
    }
};

} // namespace mmo::protocol

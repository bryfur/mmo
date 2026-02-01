#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

// Server → Client: connection accepted with player ID (0 = not spawned yet)
struct ConnectionAcceptedMsg : Serializable<ConnectionAcceptedMsg> {
    uint32_t player_id = 0;

    static constexpr size_t serialized_size() { return 4; }

    void serialize_impl(BufferWriter& w) const {
        w.write(player_id);
    }

    void deserialize_impl(BufferReader& r) {
        player_id = r.read<uint32_t>();
    }
};

} // namespace mmo::protocol

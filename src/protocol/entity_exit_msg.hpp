#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

// Server → Client: entity left view range
struct EntityExitMsg : Serializable<EntityExitMsg> {
    uint32_t entity_id = 0;

    static constexpr size_t serialized_size() { return 4; }

    void serialize_impl(BufferWriter& w) const {
        w.write(entity_id);
    }

    void deserialize_impl(BufferReader& r) {
        entity_id = r.read<uint32_t>();
    }
};

} // namespace mmo::protocol

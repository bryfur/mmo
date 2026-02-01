#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

// World configuration sent from server to client on connect
struct NetWorldConfig : Serializable<NetWorldConfig> {
    float world_width = 8000.0f;
    float world_height = 8000.0f;
    float tick_rate = 60.0f;

    static constexpr size_t serialized_size() {
        return sizeof(float) * 3;
    }

    void serialize_impl(BufferWriter& w) const {
        w.write(world_width);
        w.write(world_height);
        w.write(tick_rate);
    }

    void deserialize_impl(BufferReader& r) {
        world_width = r.read<float>();
        world_height = r.read<float>();
        tick_rate = r.read<float>();
    }
};

} // namespace mmo::protocol

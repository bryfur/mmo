#pragma once

#include "serializable.hpp"

namespace mmo::protocol {

// Client → Server: class selection
struct ClassSelectMsg : Serializable<ClassSelectMsg> {
    uint8_t class_index = 0;

    static constexpr size_t serialized_size() { return 1; }

    void serialize_impl(BufferWriter& w) const {
        w.write(class_index);
    }

    void deserialize_impl(BufferReader& r) {
        class_index = r.read<uint8_t>();
    }
};

} // namespace mmo::protocol

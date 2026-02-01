#pragma once

#include "protocol/buffer_reader.hpp"
#include "protocol/buffer_writer.hpp"
#include <cstdint>
#include <span>

namespace mmo::protocol {

// CRTP base for all serializable protocol types.
// Derived must implement:
//   size_t serialized_size() const  (or static constexpr)
//   void serialize_impl(BufferWriter& w) const
//   void deserialize_impl(BufferReader& r)
template<typename Derived>
struct Serializable {
    // Serialize into a fixed span (bounds-checked, no allocation)
    void serialize(std::span<uint8_t> buf) const {
        BufferWriter w(buf);
        static_cast<const Derived*>(this)->serialize_impl(w);
    }

    // Serialize into an existing writer
    void serialize(BufferWriter& w) const {
        static_cast<const Derived*>(this)->serialize_impl(w);
    }

    // Deserialize from a span
    void deserialize(std::span<const uint8_t> data) {
        BufferReader r(data);
        static_cast<Derived*>(this)->deserialize_impl(r);
    }

    // Deserialize from an existing reader
    void deserialize(BufferReader& r) {
        static_cast<Derived*>(this)->deserialize_impl(r);
    }

    // Convenience: get size via the derived class
    size_t size() const {
        return static_cast<const Derived*>(this)->serialized_size();
    }
};

} // namespace mmo::protocol

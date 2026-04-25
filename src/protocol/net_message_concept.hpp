#pragma once

// NetMessage<T>: compile-time contract for protocol message types. Every
// struct sent over the wire must:
//   - derive from Serializable<Self> (inherits serialize/deserialize entry
//     points)
//   - expose serialized_size() returning a size_t (static or const method)
//   - implement serialize_impl(BufferWriter&) const
//   - implement deserialize_impl(BufferReader&)
//
// Write `static_assert(NetMessage<MyMsg>);` after each protocol struct, or
// centralise asserts in contracts.hpp. The compiler rejects anything that
// forgets a hook (a common bug: adding a field to serialize but forgetting
// to read it back in deserialize).

#include "protocol/serializable.hpp"

#include <concepts>
#include <cstddef>

namespace mmo::protocol {

template <typename T>
concept NetMessage = requires(T t, const T ct, BufferWriter& w, BufferReader& r) {
    requires std::derived_from<T, Serializable<T>>;
    { ct.serialized_size() } -> std::convertible_to<std::size_t>;
    { ct.serialize_impl(w) } -> std::same_as<void>;
    { t.deserialize_impl(r) } -> std::same_as<void>;
};

} // namespace mmo::protocol

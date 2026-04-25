#pragma once

// Formula<T>: compile-time contract for pure math classes. Where a Rule
// answers "can this happen?", a Formula answers "given these inputs, what's
// the value?" - typically a number.
//
// A Formula has:
//   - `struct Inputs { ... }` nested aggregate for the input snapshot
//   - `using Output = ...` typedef for the return type (float, int, a
//     nested POD, etc.)
//   - `static Output compute(const Inputs&) noexcept`
//
// Example: MovementSpeed takes a SpeedInputs{base, talent_mult, ...} and
// returns a float via compute().

#include <concepts>

namespace mmo::server::math {

template <typename T>
concept Formula = requires(const typename T::Inputs& in) {
    typename T::Inputs;
    typename T::Output;
    { T::compute(in) } -> std::same_as<typename T::Output>;
};

} // namespace mmo::server::math

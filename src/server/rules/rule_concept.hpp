#pragma once

// Rule<T>: compile-time contract that every gameplay "validator" class must
// satisfy. A Rule is a pure yes/no decision — given a snapshot of state,
// can an action happen? Rules own no ECS, no config, no RNG.
//
// Any new rule class must provide:
//   1. nested `struct Inputs { ... }` (aggregate-initializable, defaults)
//   2. nested `enum class Result : std::uint8_t` with `Ok` as the first case
//   3. `static [[nodiscard]] Result check(const Inputs&) noexcept`
//
// Write `static_assert(Rule<MyRule>);` after each rule class. Violations
// produce a compile error pointing at the offending class, so the pattern
// is enforced by the compiler rather than by code review.
//
// Example:
//
//   class SkillGate {
//   public:
//       enum class Result : std::uint8_t { Ok, NoSuchSkill, ... };
//       struct Inputs { bool caster_alive = true; ... };
//       [[nodiscard]] static constexpr Result check(const Inputs&) noexcept;
//   };
//   static_assert(Rule<SkillGate>);

#include <concepts>
#include <type_traits>

namespace mmo::server::rules {

template <typename T>
concept Rule = requires(const typename T::Inputs& in) {
    typename T::Inputs;
    typename T::Result;
    requires std::is_enum_v<typename T::Result>;
    { T::Result::Ok } -> std::same_as<typename T::Result>;
    { T::check(in) } -> std::same_as<typename T::Result>;
};

} // namespace mmo::server::rules

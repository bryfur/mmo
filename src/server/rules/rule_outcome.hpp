#pragma once

// RuleOutcome<ResultEnum>: convenience wrapper around a rule Result.
// Lets call sites write
//     if (auto r = MyRule::check(in); r) { ... }
// instead of comparing against Result::Ok manually. Implicitly convertible
// back to the underlying enum so existing switch() usage still works.

#include <type_traits>

namespace mmo::server::rules {

template<typename ResultEnum>
    requires std::is_enum_v<ResultEnum>
struct RuleOutcome {
    ResultEnum value;

    constexpr RuleOutcome(ResultEnum v) noexcept : value(v) {}
    constexpr operator ResultEnum() const noexcept { return value; }
    [[nodiscard]] constexpr bool ok() const noexcept { return value == ResultEnum::Ok; }
    constexpr explicit operator bool() const noexcept { return ok(); }
};

} // namespace mmo::server::rules

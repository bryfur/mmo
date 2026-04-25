#pragma once

#include "engine/core/assert.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace mmo::engine::core {

template<typename E> struct Err {
    E value;
};

template<typename E> Err(E) -> Err<E>;

namespace detail {
struct OkTag {};
struct ErrTag {};
} // namespace detail

template<typename T, typename E = std::string> class Result {
public:
    using value_type = T;
    using error_type = E;

    Result(const T& v) : data_(std::in_place_index<0>, v) {}
    Result(T&& v) : data_(std::in_place_index<0>, std::move(v)) {}

    Result(const Err<E>& e) : data_(std::in_place_index<1>, e.value) {}
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved): moves the wrapped value out of `e`.
    Result(Err<E>&& e) : data_(std::in_place_index<1>, std::move(e.value)) {}

    template<typename U>
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved): moves the wrapped value out of `e`.
    Result(Err<U>&& e)
        requires(std::is_convertible_v<U, E>)
        : data_(std::in_place_index<1>, E(std::move(e.value))) {}

    bool is_ok() const noexcept { return data_.index() == 0; }
    bool is_err() const noexcept { return data_.index() == 1; }

    explicit operator bool() const noexcept { return is_ok(); }

    T& value() & {
        ENGINE_VERIFY(is_ok(), "Result::value() called on error");
        return std::get<0>(data_);
    }
    const T& value() const& {
        ENGINE_VERIFY(is_ok(), "Result::value() called on error");
        return std::get<0>(data_);
    }
    T&& value() && {
        ENGINE_VERIFY(is_ok(), "Result::value() called on error");
        return std::move(std::get<0>(data_));
    }

    E& error() & {
        ENGINE_VERIFY(is_err(), "Result::error() called on ok");
        return std::get<1>(data_);
    }
    const E& error() const& {
        ENGINE_VERIFY(is_err(), "Result::error() called on ok");
        return std::get<1>(data_);
    }
    E&& error() && {
        ENGINE_VERIFY(is_err(), "Result::error() called on ok");
        return std::move(std::get<1>(data_));
    }

    template<typename U> T value_or(U&& fallback) const& {
        return is_ok() ? std::get<0>(data_) : static_cast<T>(std::forward<U>(fallback));
    }

    template<typename U> T value_or(U&& fallback) && {
        return is_ok() ? std::move(std::get<0>(data_)) : static_cast<T>(std::forward<U>(fallback));
    }

    template<typename Fn> auto map(Fn&& fn) const& -> Result<std::invoke_result_t<Fn, const T&>, E> {
        using U = std::invoke_result_t<Fn, const T&>;
        if (is_ok()) {
            return Result<U, E>(std::forward<Fn>(fn)(std::get<0>(data_)));
        }
        return Result<U, E>(Err<E>{std::get<1>(data_)});
    }

    template<typename Fn> auto map(Fn&& fn) && -> Result<std::invoke_result_t<Fn, T&&>, E> {
        using U = std::invoke_result_t<Fn, T&&>;
        if (is_ok()) {
            return Result<U, E>(std::forward<Fn>(fn)(std::move(std::get<0>(data_))));
        }
        return Result<U, E>(Err<E>{std::move(std::get<1>(data_))});
    }

    template<typename Fn> auto and_then(Fn&& fn) const& -> std::invoke_result_t<Fn, const T&> {
        using R = std::invoke_result_t<Fn, const T&>;
        if (is_ok()) {
            return std::forward<Fn>(fn)(std::get<0>(data_));
        }
        return R(Err<E>{std::get<1>(data_)});
    }

    template<typename Fn> auto and_then(Fn&& fn) && -> std::invoke_result_t<Fn, T&&> {
        using R = std::invoke_result_t<Fn, T&&>;
        if (is_ok()) {
            return std::forward<Fn>(fn)(std::move(std::get<0>(data_)));
        }
        return R(Err<E>{std::move(std::get<1>(data_))});
    }

private:
    std::variant<T, E> data_;
};

template<typename T> inline Result<std::decay_t<T>> Ok(T&& v) {
    return Result<std::decay_t<T>>(std::forward<T>(v));
}

template<typename E> inline Err<std::decay_t<E>> make_err(E&& e) {
    return Err<std::decay_t<E>>{std::forward<E>(e)};
}

} // namespace mmo::engine::core

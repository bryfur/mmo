#pragma once

#include "engine/core/logger.hpp"

#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <utility>


namespace mmo::engine::core::detail {

[[noreturn]] inline void abort_with(LogLevel level, const char* file, int line, const char* expr,
                                    std::string_view msg) {
    std::string full = std::format("{}:{}: assertion `{}` failed: {}", file, line, expr ? expr : "<unreachable>", msg);
    log_plain(level, "assert", full);
    std::abort();
}

template<typename... Args>
[[noreturn]] inline void abort_format(LogLevel level, const char* file, int line, const char* expr,
                                      std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    abort_with(level, file, line, expr, msg);
}

[[noreturn]] inline void abort_plain(LogLevel level, const char* file, int line, const char* expr) {
    abort_with(level, file, line, expr, "");
}

} // namespace mmo::engine::core::detail


#define ENGINE_VERIFY(cond, ...)                                                                                       \
    do {                                                                                                               \
        if (!(cond)) [[unlikely]] {                                                                                    \
            __VA_OPT__(::mmo::engine::core::detail::abort_format(::mmo::engine::core::LogLevel::Fatal, __FILE__,       \
                                                                 __LINE__, #cond, __VA_ARGS__);)                       \
            ::mmo::engine::core::detail::abort_plain(::mmo::engine::core::LogLevel::Fatal, __FILE__, __LINE__, #cond); \
        }                                                                                                              \
    } while (0)

#ifdef NDEBUG
#define ENGINE_ASSERT(cond, ...) ((void)0)
#else
#define ENGINE_ASSERT(cond, ...) ENGINE_VERIFY(cond __VA_OPT__(, ) __VA_ARGS__)
#endif

#define ENGINE_UNREACHABLE(...)                                                                                        \
    do {                                                                                                               \
        __VA_OPT__(::mmo::engine::core::detail::abort_format(::mmo::engine::core::LogLevel::Fatal, __FILE__, __LINE__, \
                                                             "unreachable", __VA_ARGS__);)                             \
        ::mmo::engine::core::detail::abort_plain(::mmo::engine::core::LogLevel::Fatal, __FILE__, __LINE__,             \
                                                 "unreachable");                                                       \
    } while (0)

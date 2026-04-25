#pragma once

#include <iostream>
#include <sstream>
#include <string_view>

// Minimal server-side logger. Writes structured single-line records to stdout
// (info/debug) or stderr (warn/error). No allocation if level is filtered out.
//
// Usage:
//     LOG_INFO("World") << "Loaded " << count << " entities";
//     LOG_WARN("Net")   << "client " << id << " sent oversized packet";
//
// The category is a short string-tag; bracket it in output to make grepping
// painless. Set the global level by editing LogLevel::current at startup
// (defaults to Info; bump to Debug for verbose tracing).

namespace mmo::server::log {

enum class Level : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

inline Level current_level = Level::Info;

class Stream {
public:
    Stream(Level level, std::string_view category)
        : level_(level), enabled_(level >= current_level) {
        if (!enabled_) return;
        const char* tag =
            level == Level::Debug ? "[DEBUG]" :
            level == Level::Info  ? "[INFO ]" :
            level == Level::Warn  ? "[WARN ]" :
                                    "[ERROR]";
        buf_ << tag << " [" << category << "] ";
    }
    ~Stream() {
        if (!enabled_) return;
        buf_ << '\n';
        auto& sink = (level_ >= Level::Warn) ? std::cerr : std::cout;
        sink << buf_.str();
    }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    template <typename T>
    Stream& operator<<(T&& v) {
        if (enabled_) buf_ << std::forward<T>(v);
        return *this;
    }

private:
    std::ostringstream buf_;
    Level level_;
    bool enabled_;
};

} // namespace mmo::server::log

#define LOG_DEBUG(cat) ::mmo::server::log::Stream(::mmo::server::log::Level::Debug, cat)
#define LOG_INFO(cat)  ::mmo::server::log::Stream(::mmo::server::log::Level::Info,  cat)
#define LOG_WARN(cat)  ::mmo::server::log::Stream(::mmo::server::log::Level::Warn,  cat)
#define LOG_ERROR(cat) ::mmo::server::log::Stream(::mmo::server::log::Level::Error, cat)

#include "engine/core/logger.hpp"

#include <algorithm>
#include <SDL3/SDL_log.h>
#include <string>

namespace mmo::engine::core {

const char* to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        case LogLevel::Off:
            return "OFF";
    }
    return "?";
}

namespace {

SDL_LogPriority to_sdl_priority(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return SDL_LOG_PRIORITY_TRACE;
        case LogLevel::Debug:
            return SDL_LOG_PRIORITY_DEBUG;
        case LogLevel::Info:
            return SDL_LOG_PRIORITY_INFO;
        case LogLevel::Warn:
            return SDL_LOG_PRIORITY_WARN;
        case LogLevel::Error:
            return SDL_LOG_PRIORITY_ERROR;
        case LogLevel::Fatal:
            return SDL_LOG_PRIORITY_CRITICAL;
        default:
            return SDL_LOG_PRIORITY_INFO;
    }
}

} // namespace

void StdoutSink::write(LogLevel level, const char* category, std::string_view msg) {
    const std::string cat_prefix = category && *category ? std::string("[") + category + "] " : std::string();
    std::string body;
    body.reserve(cat_prefix.size() + msg.size());
    body.append(cat_prefix);
    body.append(msg.data(), msg.size());
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, to_sdl_priority(level), "%s", body.c_str());
}

Logger::Logger() {
    sinks_.push_back(Entry{next_id_++, std::make_shared<StdoutSink>()});
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_min_level(LogLevel level) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

LogLevel Logger::min_level() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return min_level_;
}

bool Logger::should_log(LogLevel level) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return level >= min_level_ && min_level_ != LogLevel::Off;
}

Logger::SinkHandle Logger::add_sink(std::shared_ptr<LogSink> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    SinkHandle id = next_id_++;
    sinks_.push_back(Entry{id, std::move(sink)});
    return id;
}

void Logger::remove_sink(SinkHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.erase(std::remove_if(sinks_.begin(), sinks_.end(), [handle](const Entry& e) { return e.id == handle; }),
                 sinks_.end());
}

void Logger::clear_sinks() {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

void Logger::dispatch(LogLevel level, const char* category, std::string_view msg) {
    // Snapshot under lock so sink writes happen unlocked (avoids deadlock if a sink calls back).
    std::vector<std::shared_ptr<LogSink>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot.reserve(sinks_.size());
        for (auto& e : sinks_) snapshot.push_back(e.sink);
    }
    for (auto& s : snapshot) {
        s->write(level, category ? category : "general", msg);
    }
}

} // namespace mmo::engine::core

#include "client/game.hpp"
#include "protocol/protocol.hpp"
#include "SDL3/SDL_log.h"
#include "SDL3/SDL_stdinc.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>

// Custom SDL log function with timestamps
static void SDLCALL log_with_timestamp(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)userdata;
    (void)category;

    // Get current time with milliseconds
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    struct tm tm_now{};
    localtime_r(&time_t_now, &tm_now);

    const char* priority_str = "";
    switch (priority) {
        case SDL_LOG_PRIORITY_VERBOSE:
            priority_str = "VERBOSE";
            break;
        case SDL_LOG_PRIORITY_DEBUG:
            priority_str = "DEBUG";
            break;
        case SDL_LOG_PRIORITY_INFO:
            priority_str = "INFO";
            break;
        case SDL_LOG_PRIORITY_WARN:
            priority_str = "WARN";
            break;
        case SDL_LOG_PRIORITY_ERROR:
            priority_str = "ERROR";
            break;
        case SDL_LOG_PRIORITY_CRITICAL:
            priority_str = "CRITICAL";
            break;
        default:
            priority_str = "???";
            break;
    }

    fprintf(stderr, "[%02d:%02d:%02d.%03d] [%s] %s\n", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
            static_cast<int>(ms.count()), priority_str, message);
}

static void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]" << '\n';
    std::cout << "Options:" << '\n';
    std::cout << "  -h, --host <host>    Server host (default: localhost)" << '\n';
    std::cout << "  -p, --port <port>    Server port (default: " << mmo::protocol::DEFAULT_PORT << ")" << '\n';
    std::cout << "  --help               Show this help message" << '\n';
}

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    uint16_t port = mmo::protocol::DEFAULT_PORT;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if ((arg == "-h" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }

    std::cout << "=== MMO Client ===" << '\n';
    std::cout << "Server: " << host << ":" << port << '\n';
    std::cout << '\n';
    std::cout << "Controls:" << '\n';
    std::cout << "  Arrow Keys - Navigate menu / Move" << '\n';
    std::cout << "  SPACE - Select class / Attack" << '\n';
    std::cout << "  ESC - Quit" << '\n';
    std::cout << '\n';

    // Set up timestamped logging
    SDL_SetLogOutputFunction(log_with_timestamp, nullptr);

    mmo::client::Game game;

    if (!game.init(host, port)) {
        std::cerr << "Failed to initialize game" << '\n';
        return 1;
    }

    game.run();
    game.shutdown();

    return 0;
}

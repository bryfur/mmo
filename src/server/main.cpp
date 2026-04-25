#include "asio/io_context.hpp"
#include "asio/signal_set.hpp"
#include "server/game_config.hpp"
#include "server/server.hpp"
#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Load game configuration from JSON files
    mmo::server::GameConfig config;
    std::string data_dir = "data";

    // Check for data dir relative to executable or current directory
    if (!config.load(data_dir)) {
        // Try relative to executable path
        data_dir = "../data";
        if (!config.load(data_dir)) {
            std::cerr << "Failed to load game config from data/ directory" << '\n';
            return 1;
        }
    }

    uint16_t port = config.server().default_port;

    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    asio::io_context io_context;

    mmo::server::Server server(io_context, port, config);

    // Graceful shutdown via asio: signal handler runs on the io_context
    // thread, so server.stop() (which flushes persistence) executes
    // before io_context.run() returns. Replaces std::signal-based
    // approach which was not safe to call game/DB code from.
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const asio::error_code& ec, int sig) {
        if (ec) {
            return;
        }
        std::cout << "\nReceived signal " << sig << ", shutting down..." << '\n';
        server.stop();
        io_context.stop();
    });

    server.start();

    std::cout << "MMO Server running on port " << port << '\n';
    std::cout << "Press Ctrl+C to stop" << '\n';

    io_context.run();

    return 0;
}

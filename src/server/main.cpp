#include "server/server.hpp"
#include "server/game_config.hpp"
#include <iostream>
#include <csignal>

std::function<void()> shutdown_handler;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (shutdown_handler) {
        shutdown_handler();
    }
}

int main(int argc, char* argv[]) {
    // Load game configuration from JSON files
    mmo::GameConfig config;
    std::string data_dir = "data";

    // Check for data dir relative to executable or current directory
    if (!config.load(data_dir)) {
        // Try relative to executable path
        data_dir = "../data";
        if (!config.load(data_dir)) {
            std::cerr << "Failed to load game config from data/ directory" << std::endl;
            return 1;
        }
    }

    uint16_t port = config.server().default_port;

    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    try {
        asio::io_context io_context;

        mmo::Server server(io_context, port, config);

        shutdown_handler = [&]() {
            server.stop();
            io_context.stop();
        };

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        server.start();

        std::cout << "MMO Server running on port " << port << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;

        io_context.run();

    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

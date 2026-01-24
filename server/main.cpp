#include "server/server.hpp"
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
    uint16_t port = mmo::DEFAULT_PORT;
    
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    try {
        asio::io_context io_context;
        
        mmo::Server server(io_context, port);
        
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

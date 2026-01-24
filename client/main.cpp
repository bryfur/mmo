#include "client/game.hpp"
#include "common/protocol.hpp"
#include <iostream>
#include <string>

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>    Server host (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>    Server port (default: " << mmo::DEFAULT_PORT << ")" << std::endl;
    std::cout << "  --help               Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    uint16_t port = mmo::DEFAULT_PORT;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-h" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }
    
    std::cout << "=== MMO Client ===" << std::endl;
    std::cout << "Server: " << host << ":" << port << std::endl;
    std::cout << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Arrow Keys - Navigate menu / Move" << std::endl;
    std::cout << "  SPACE - Select class / Attack" << std::endl;
    std::cout << "  ESC - Quit" << std::endl;
    std::cout << std::endl;
    
    mmo::Game game;
    
    if (!game.init(host, port)) {
        std::cerr << "Failed to initialize game" << std::endl;
        return 1;
    }
    
    game.run();
    game.shutdown();
    
    return 0;
}

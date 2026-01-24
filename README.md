# MMO Game

A basic multiplayer online game with a C++ server and client.

## Features

- **Server**: Boost.Asio TCP server handling multiple client connections
- **Client**: SDL3 + OpenGL 2D rendering with WASD/Arrow key movement
- **Networking**: Custom binary protocol for efficient player state synchronization
- **Multiplayer**: See other players move in real-time

## Dependencies

### Server
- Boost (system, asio)
- pthread

### Client
- SDL3
- OpenGL
- Boost (system, asio)

## Building

### Install Dependencies (Ubuntu/Debian)

```bash
# Boost
sudo apt install libboost-all-dev

# OpenGL
sudo apt install libgl1-mesa-dev

# SDL3 (may need to build from source if not available)
# Option 1: From package manager (if available)
sudo apt install libsdl3-dev

# Option 2: Build from source
git clone https://github.com/libsdl-org/SDL.git
cd SDL
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

### Build the Project

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

This will produce two executables:
- `mmo_server` - The game server
- `mmo_client` - The game client

## Running

### Start the Server

```bash
./mmo_server [port]
```

Default port is `7777`.

### Start Clients

```bash
./mmo_client [options]
```

Options:
- `-h, --host <host>` - Server host (default: localhost)
- `-p, --port <port>` - Server port (default: 7777)
- `-n, --name <name>` - Player name (default: random)
- `--help` - Show help message

### Example: Local Testing

Terminal 1 (Server):
```bash
./mmo_server
```

Terminal 2 (Client 1):
```bash
./mmo_client -n Player1
```

Terminal 3 (Client 2):
```bash
./mmo_client -n Player2
```

## Controls

- **WASD** or **Arrow Keys** - Move
- **ESC** - Quit

## Architecture

```
mmo/
├── common/           # Shared code between server and client
│   ├── protocol.hpp  # Network protocol definitions
│   └── protocol.cpp
├── server/           # Server implementation
│   ├── main.cpp      # Server entry point
│   ├── server.hpp/cpp    # TCP server with Boost.Asio
│   ├── session.hpp/cpp   # Client connection handler
│   └── game_world.hpp/cpp # Game state management
├── client/           # Client implementation
│   ├── main.cpp      # Client entry point
│   ├── game.hpp/cpp      # Main game loop
│   ├── network_client.hpp/cpp # Network communication
│   ├── renderer.hpp/cpp  # OpenGL 2D rendering
│   └── input_handler.hpp/cpp # Input handling
└── CMakeLists.txt    # Build configuration
```

## Network Protocol

Messages use a simple binary format:
- **Header**: 3 bytes (1 byte type + 2 bytes payload size)
- **Payload**: Variable length data

Message types:
- `Connect` (1) - Client requests connection with player name
- `Disconnect` (2) - Client disconnects
- `PlayerInput` (3) - Client sends input state
- `ConnectionAccepted` (10) - Server accepts connection, sends player ID
- `PlayerJoined` (12) - Server notifies of new player
- `PlayerLeft` (13) - Server notifies of disconnected player
- `WorldState` (14) - Server broadcasts all player positions

## Future Improvements

- [ ] Client-side prediction for smoother movement
- [ ] UDP for position updates (keep TCP for reliable messages)
- [ ] Text rendering for player names
- [ ] 3D rendering with OpenGL 3.3+
- [ ] Chat system
- [ ] Collision detection
- [ ] Game objects and items
- [ ] Authentication

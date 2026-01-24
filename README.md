# MMO Game

A multiplayer online game built with C++20 featuring a client-server architecture, 3D rendering, and physics simulation.

## Features

- 3D OpenGL rendering with shadows, terrain, grass, and effects
- Entity Component System architecture using EnTT
- Physics simulation with Jolt Physics
- glTF model loading for characters, buildings, and environment
- AI and combat systems
- Client-side interpolation for smooth networked movement
- Text rendering with SDL3_ttf

## Dependencies

Dependencies are managed via CMake FetchContent (automatically downloaded):
- EnTT v3.14.0 (ECS)
- Jolt Physics v5.2.0
- tinygltf v2.9.7

System dependencies (must be installed):
- SDL3
- SDL3_ttf
- OpenGL
- GLEW
- GLM
- Standalone Asio

## Building

### Install System Dependencies (Ubuntu/Debian)

```bash
sudo apt install libasio-dev libglew-dev libglm-dev libgl1-mesa-dev
```

SDL3 and SDL3_ttf may need to be built from source if not available in your package manager.

### Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Produces:
- `mmo_server` - Game server
- `mmo_client` - Game client

## Running

```bash
# Start server (default port 7777)
./build/mmo_server [port]

# Start client
./build/mmo_client [-h host] [-p port] [-n name]
```

Or use the convenience script:
```bash
./start.sh
```

## Controls

- WASD / Arrow Keys - Move
- ESC - Quit

## Project Structure

```
mmo/
├── client/           # Client application
│   ├── render/       # Rendering subsystems (terrain, shadows, effects, UI)
│   └── systems/      # Client-side ECS systems (camera, interpolation)
├── server/           # Server application
│   └── systems/      # Server-side ECS systems (AI, combat, movement, physics)
├── common/           # Shared code
│   └── ecs/          # Component definitions
├── assets/           # Game assets
│   ├── models/       # glTF models
│   └── textures/     # Texture files
└── tools/            # Python utilities
```

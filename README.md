# MMO Game

A multiplayer online game built with C++20 featuring a client-server architecture, 3D rendering, and physics simulation.

## Features

- 3D rendering with SDL3 GPU API (terrain, grass, effects, UI, skybox)
- Entity Component System architecture using EnTT
- Physics simulation with Jolt Physics
- glTF model loading for characters, buildings, and environment
- HLSL shaders compiled to SPIR-V at build time via SDL_shadercross
- AI and combat systems
- Client-side interpolation for smooth networked movement
- Text rendering with SDL3_ttf
- Gamepad/controller support
- JSON-driven game data configuration (classes, monsters, world, etc.)

## Dependencies

Dependencies are managed via CMake FetchContent (automatically downloaded):
- EnTT v3.14.0 (ECS)
- Jolt Physics v5.2.0
- tinygltf v2.9.7
- Standalone Asio 1.30.2
- GLM 1.0.3
- nlohmann/json v3.11.3
- SDL_shadercross (HLSL to SPIR-V shader compilation)

System dependencies (used if available, otherwise automatically built from source):
- SDL3 >= 3.4.0
- SDL3_ttf >= 3.2.2
- SDL3_image >= 3.4.0

## Building

### Install System Dependencies (Ubuntu/Debian)

```bash
# Optional - if not installed, these will be built from source automatically
sudo apt install libsdl3-dev libsdl3-ttf-dev libsdl3-image-dev
```

### Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Produces:
- `build/src/server/mmo_server` - Game server
- `build/src/client/mmo_client` - Game client

## Running

```bash
# Start server (default port 7777)
./build/src/server/mmo_server [port]

# Start client
./build/src/client/mmo_client [-h host] [-p port]
```

Or use the convenience script:
```bash
./start.sh
```

## Controls

### Keyboard & Mouse
- WASD / Arrow Keys - Move (camera-relative)
- Right Mouse Button + Drag - Orbit camera
- Mouse Wheel - Zoom in/out
- Left Mouse Button / Space - Attack
- Shift - Sprint
- ESC - Menu

### Gamepad
- Left Stick - Move
- Right Stick - Orbit camera
- Right Trigger - Attack
- Left Trigger - Sprint

## Project Structure

```
mmo/
├── src/
│   ├── client/           # Client application
│   │   └── ecs/          # Client-side ECS components
│   ├── engine/           # Core engine (rendering, GPU, input)
│   │   ├── gpu/          # SDL3 GPU abstraction layer
│   │   ├── render/       # Rendering subsystems (terrain, grass, effects, UI)
│   │   ├── scene/        # Scene management
│   │   ├── shaders/      # HLSL shader sources
│   │   └── systems/      # Engine systems (camera)
│   ├── server/           # Server application
│   │   ├── ecs/          # Server-side ECS components
│   │   └── systems/      # Server-side ECS systems (AI, combat, movement, physics)
│   └── protocol/         # Shared networking protocol
├── assets/               # Game assets
│   ├── models/           # glTF models
│   └── textures/         # Texture files
├── data/                 # JSON game configuration
│   ├── classes.json      # Character class definitions
│   ├── monsters.json     # Monster definitions
│   ├── environment.json  # Environment settings
│   ├── server.json       # Server configuration
│   ├── town.json         # Town layout
│   └── world.json        # World settings
└── tools/                # Python utilities
```

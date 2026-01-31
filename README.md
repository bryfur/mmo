# MMO Game

A multiplayer online game built with C++20 featuring a client-server architecture, 3D rendering, and physics simulation.

## Features

- 3D rendering with SDL3 GPU API (terrain, grass, effects, shadows, SSAO)
- Entity Component System architecture using EnTT
- Physics simulation with Jolt Physics
- glTF model loading for characters, buildings, and environment
- HLSL shaders compiled to SPIR-V at build time via SDL_shadercross
- AI and combat systems
- Client-side interpolation for smooth networked movement
- Text rendering with SDL3_ttf

## Dependencies

Dependencies are managed via CMake FetchContent (automatically downloaded):
- EnTT v3.14.0 (ECS)
- Jolt Physics v5.2.0
- tinygltf v2.9.7
- Standalone Asio 1.30.2
- SDL_shadercross (HLSL to SPIR-V shader compilation)

System dependencies (must be installed):
- SDL3
- SDL3_ttf
- SDL3_image
- GLM

Or pass `-DVENDORED_SDL=ON` to build SDL3, SDL3_ttf, and SDL3_image from source.

## Building

### Install System Dependencies (Ubuntu/Debian)

```bash
sudo apt install libsdl3-dev libsdl3-ttf-dev libsdl3-image-dev libglm-dev
```

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

- WASD / Arrow Keys - Move (camera-relative)
- Right Mouse Button + Drag - Orbit camera
- Mouse Wheel - Zoom in/out
- Left Mouse Button / Space - Attack
- Shift - Sprint
- ESC - Quit

## Project Structure

```
mmo/
├── src/
│   ├── client/           # Client application
│   │   ├── gpu/          # SDL3 GPU abstraction layer
│   │   ├── render/       # Rendering subsystems (terrain, effects, UI, shadows)
│   │   ├── scene/        # Scene management
│   │   ├── shaders/      # HLSL shader sources
│   │   └── systems/      # Client-side ECS systems (camera, interpolation)
│   ├── server/           # Server application
│   │   └── systems/      # Server-side ECS systems (AI, combat, movement, physics)
│   └── common/           # Shared code (protocol, ECS components)
├── assets/               # Game assets
│   ├── models/           # glTF models
│   └── textures/         # Texture files
└── tools/                # Python utilities
```

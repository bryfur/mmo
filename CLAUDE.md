# CLAUDE.md - AI Assistant Guide for MMO Project

## Project Overview

A 3D multiplayer online game built in C++20 with SDL3 GPU rendering, EnTT ECS architecture, Jolt Physics, and Asio networking. The client renders a 3D world with terrain, models, grass, shadows, and ambient occlusion. The server runs authoritative game simulation at 60 Hz.

## Quick Reference

```bash
# Build (incremental only - NEVER use --clean-first)
cmake --build build

# Configure (first time or after CMakeLists changes)
cmake -B build

# Run server + client(s)
./start.sh          # 1 client
./start.sh 3        # 3 clients

# Binaries
build/src/server/mmo_server
build/src/client/mmo_client
```

## Critical Rules

- **NEVER** run clean rebuilds (`--clean-first`). Build takes a long time from scratch. Always use incremental: `cmake --build build`
- **No test suite exists.** Testing is manual via `./start.sh`
- Shader changes require rebuild (HLSL → SPIR-V compiled at build time)

## Repository Structure

```
src/
  client/         Game client (networking, input, game state machine, menus)
  server/         Game server (world sim, sessions, physics, AI, combat)
  engine/         Rendering engine and core systems
    gpu/          SDL3 GPU abstraction (device, shaders, pipelines, buffers, textures)
    render/       Renderers (world, terrain, grass, effects, UI, text, shadows, AO)
    scene/        Scene management, camera, frustum culling
    shaders/      HLSL vertex/fragment shaders (29 files)
  protocol/       Shared network protocol and serialization
assets/
  models/         GLB 3D models (characters, buildings, environment)
  textures/       Texture files
data/             JSON game config (classes, monsters, world, server, town, environment)
docs/             Architecture documentation
.github/workflows/ CI/CD
```

## Architecture

### ECS (EnTT)
- Components are plain structs in `src/client/ecs/components.hpp` and `src/server/ecs/game_components.hpp`
- Systems are free functions: `void update_physics(entt::registry& registry, float dt)`
- Server and client have separate registries and component sets

### Networking
- TCP via Asio, binary serialization with `MessageType` + payload
- Protocol defined in `src/protocol/protocol.hpp`
- Client runs network I/O on a separate thread with a message queue to main thread
- Server broadcasts `WorldState` every tick (60 Hz)
- Connection flow: Connect → ClassSelect → Spawning → Playing

### Rendering Pipeline
- SDL3 GPU API (abstracts Vulkan/Metal/D3D12)
- Shaders: HLSL compiled to SPIR-V at build time via shadercross
- Render order: terrain → world objects → effects → UI
- Features: cascaded shadows, SSAO/GTAO, procedural skybox, instanced grass

### Physics
- Jolt Physics for rigid bodies and collision
- Collision layers: NON_MOVING, MOVING, TRIGGER
- Server-authoritative physics simulation

## Coding Conventions

### Naming (enforced by clang-tidy)
- Classes/Structs: `CamelCase`
- Namespaces: `lower_case` (`mmo::engine::gpu`)
- Methods/Functions: `lower_case_underscores`
- Variables: `lower_case_underscores`
- Private members: trailing `_` suffix (`socket_`, `registry_`)
- Constants/Enums: `UPPER_CASE`

### Namespaces
- `mmo::client` / `mmo::server` / `mmo::engine` / `mmo::protocol`
- Engine sub-namespaces: `mmo::engine::gpu`, `mmo::engine::render`, `mmo::engine::scene`, `mmo::engine::systems`
- Server systems: `mmo::server::systems`

### Formatting
- clang-format: LLVM-based, 4-space indent, 120 char line limit
- K&R brace style (same-line opening braces)

### Coordinates
- Y-up, X-Z ground plane, rotation in radians around Y-axis

### Shader Conventions
- Files: `{name}.vert.hlsl` / `{name}.frag.hlsl`
- Entry points: `VSMain` (vertex), `PSMain` (fragment)
- Binding sets: 0 (VS textures/buffers), 1 (VS uniforms), 2 (FS textures/buffers), 3 (FS uniforms)
- All uniform structs padded to 16-byte alignment

## Build System

- CMake 3.20+ with FetchContent for all dependencies
- Dependencies are statically linked and auto-fetched: SDL3, EnTT, Jolt, Asio, tinygltf, GLM, nlohmann/json, SDL3_ttf, SDL3_image, SDL_shadercross
- C++20 standard required
- CI: GitHub Actions on ubuntu-24.04 with ccache

## Key Files

| File | Purpose |
|------|---------|
| `src/client/game.cpp` | Client game loop and state machine |
| `src/server/world.cpp` | Server world simulation and entity management |
| `src/server/systems/physics_system.cpp` | Jolt Physics integration |
| `src/protocol/protocol.hpp` | All network message types and serialization |
| `src/engine/scene/scene_renderer.cpp` | Main rendering orchestrator |
| `src/engine/gpu/gpu_device.cpp` | SDL3 GPU context management |
| `data/classes.json` | Player class definitions (Warrior, Mage, Paladin, Archer) |

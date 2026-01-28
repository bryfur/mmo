# SDL3 GPU Shader Directory

This directory contains shaders for the SDL3 GPU API.

## Directory Structure

```
shaders/
├── src/              # HLSL source shaders
│   ├── model.vert.hlsl
│   ├── model.frag.hlsl
│   └── ...
├── CMakeLists.txt    # Build-time shader compilation
└── README.md
```

## Shader Compilation

Shaders are written in HLSL and compiled to SPIR-V at build time by CMake using `shadercross`.

**Build-time:** HLSL → SPIR-V (via shadercross CLI)
**Runtime:** SPIR-V → Backend (via SDL_shadercross library)
- Vulkan uses SPIR-V directly
- Metal/D3D12 get transpiled from SPIR-V at load time

Compiled `.spv` files are output to `${CMAKE_BINARY_DIR}/shaders/`.

### Adding a New Shader

1. Create your HLSL file in `shaders/src/` with the naming convention:
   - Vertex shaders: `name.vert.hlsl` (entry point: `VSMain`)
   - Fragment shaders: `name.frag.hlsl` (entry point: `PSMain`)

2. Rebuild the project - CMake will automatically compile the new shader.

3. Load the shader in code:
   ```cpp
   auto* vs = shader_manager_->get("shaders/name.vert.spv", ShaderStage::Vertex, "VSMain", resources);
   auto* fs = shader_manager_->get("shaders/name.frag.spv", ShaderStage::Fragment, "PSMain", resources);
   ```

## Shader Conventions

### Entry Points
- Vertex shaders: `VSMain`
- Fragment shaders: `PSMain`

### Resource Binding
SDL3 GPU uses register-based binding:
- `b0-b3`: Constant/uniform buffers
- `t0-t7`: Textures (sampled)
- `s0-s7`: Samplers

### Vertex Input Layouts
See `client/gpu/gpu_types.hpp` for vertex format definitions.

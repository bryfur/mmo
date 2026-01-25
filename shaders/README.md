# SDL3 GPU Shader Directory

This directory contains shaders for the SDL3 GPU API migration.

## Directory Structure

```
shaders/
├── src/              # HLSL source shaders
│   ├── model.vert.hlsl
│   ├── model.frag.hlsl
│   └── ...
├── compiled/         # Compiled shader binaries (auto-generated)
│   ├── metal/        # Metal shaders (.metallib)
│   ├── spirv/        # SPIR-V shaders (.spv)
│   └── dxil/         # DXIL shaders (.dxil)
└── CMakeLists.txt    # Shader compilation rules
```

## Shader Compilation

Shaders are written in HLSL and compiled to multiple formats using SDL_shadercross:

- **Metal** (.metallib) - for macOS/iOS
- **SPIR-V** (.spv) - for Vulkan (Linux, Windows)
- **DXIL** (.dxil) - for D3D12 (Windows)

### Manual Compilation

```bash
# Using the compile script
python tools/compile_shaders.py

# Or using SDL_shadercross directly
shadercross -s HLSL -d SPIRV -i shaders/src/model.vert.hlsl -o shaders/compiled/spirv/model.vert.spv
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

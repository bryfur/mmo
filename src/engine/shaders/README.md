# HLSL Shader Sources

This directory contains the HLSL source shaders that are compiled to SPIR-V at
build time using the `shadercross` CLI from SDL_shadercross. The compiled `.spv`
files are output to the build directory and loaded at runtime.

## Naming Convention

- `{name}.vert.hlsl` - Vertex shaders (entry point: `VSMain`)
- `{name}.frag.hlsl` - Fragment/pixel shaders (entry point: `PSMain`)

## Available Shaders

### 3D Model Rendering
- `model.vert.hlsl` / `model.frag.hlsl` - Static model rendering with directional lighting, rim lighting, and exponential fog
- `skinned_model.vert.hlsl` / `skinned_model.frag.hlsl` - Animated models with skeletal animation (up to 64 bones)

### Environment
- `terrain.vert.hlsl` / `terrain.frag.hlsl` - Terrain rendering with texture sampling, vertex color variation, and height-based lighting
- `skybox.vert.hlsl` / `skybox.frag.hlsl` - Procedural skybox with sun disc/corona/glare, gradient sky, and horizon glow (fullscreen triangle technique)
- `grass.vert.hlsl` / `grass.frag.hlsl` - Instanced grass rendering with wind animation, per-instance rotation/scale/color, and alpha testing

### UI and Text
- `ui.vert.hlsl` / `ui.frag.hlsl` - 2D UI elements with optional texture support
- `text.vert.hlsl` / `text.frag.hlsl` - Text rendering with font atlas
- `billboard.vert.hlsl` / `billboard.frag.hlsl` - Camera-facing billboards (health bars, nameplates)

### Effects
- `effect.vert.hlsl` / `effect.frag.hlsl` - Instanced particle effects with billboarding and per-particle rotation

### Debug
- `grid.vert.hlsl` / `grid.frag.hlsl` - Debug grid overlay (vertex color passthrough)

## SDL3 GPU Resource Binding Order

Shader resource bindings must follow the SDL3 GPU binding order. Since these
shaders target SPIR-V, they use `[[vk::binding(slot, set)]]` annotations:

**Vertex shaders:**
- Set 0: Sampled textures, then storage textures, then storage buffers
- Set 1: Uniform buffers

**Fragment shaders:**
- Set 2: Sampled textures, then storage textures, then storage buffers
- Set 3: Uniform buffers

All vertex semantics use `TEXCOORD0`, `TEXCOORD1`, etc. as required by SDL3
GPU's D3D12 backend assumption.

All uniform structs are padded to 16-byte alignment for GPU compatibility.

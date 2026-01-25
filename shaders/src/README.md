# HLSL Shader Sources

This directory contains the HLSL source shaders that will be compiled to 
multiple backend formats (SPIR-V, Metal, DXIL) using SDL_shadercross.

## Naming Convention

- `{name}.vert.hlsl` - Vertex shaders (entry point: `VSMain`)
- `{name}.frag.hlsl` - Fragment/pixel shaders (entry point: `PSMain`)

## Planned Shaders (Task 2)

These shaders will be created in Task 2 (Shader System):

- `model.vert.hlsl` / `model.frag.hlsl` - Static model rendering
- `skinned_model.vert.hlsl` / `skinned_model.frag.hlsl` - Animated models
- `terrain.vert.hlsl` / `terrain.frag.hlsl` - Terrain rendering
- `skybox.vert.hlsl` / `skybox.frag.hlsl` - Skybox rendering
- `ui.vert.hlsl` / `ui.frag.hlsl` - 2D UI elements
- `text.vert.hlsl` / `text.frag.hlsl` - Text rendering
- `billboard.vert.hlsl` / `billboard.frag.hlsl` - Billboard sprites
- `effect.vert.hlsl` / `effect.frag.hlsl` - Visual effects
- `shadow.vert.hlsl` / `shadow.frag.hlsl` - Shadow map generation
- `grass.vert.hlsl` / `grass.frag.hlsl` - Instanced grass rendering

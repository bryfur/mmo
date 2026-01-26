# HLSL Shader Sources

This directory contains the HLSL source shaders that are compiled at runtime
using SDL_shadercross to the appropriate backend format (SPIR-V for Vulkan,
Metal for macOS/iOS, DXIL for Direct3D 12).

## Naming Convention

- `{name}.vert.hlsl` - Vertex shaders (entry point: `VSMain`)
- `{name}.frag.hlsl` - Fragment/pixel shaders (entry point: `PSMain`)

## Available Shaders

### 3D Model Rendering
- `model.vert.hlsl` / `model.frag.hlsl` - Static model rendering with lighting, shadows, SSAO, and fog
- `skinned_model.vert.hlsl` / `skinned_model.frag.hlsl` - Animated models with skeletal animation support

### Environment
- `terrain.vert.hlsl` / `terrain.frag.hlsl` - Terrain rendering with texture splatting
- `skybox.vert.hlsl` / `skybox.frag.hlsl` - Procedural skybox with sun, clouds, and mountains
- `grass.vert.hlsl` / `grass.frag.hlsl` - Instanced grass rendering with wind animation

### UI and Text
- `ui.vert.hlsl` / `ui.frag.hlsl` - 2D UI elements (rectangles, buttons)
- `text.vert.hlsl` / `text.frag.hlsl` - Text rendering with font atlas
- `billboard.vert.hlsl` / `billboard.frag.hlsl` - Camera-facing billboards (health bars, nameplates)

### Effects
- `effect.vert.hlsl` / `effect.frag.hlsl` - Particle effects with billboarding

### Shadow Mapping
- `shadow.vert.hlsl` / `shadow.frag.hlsl` - Shadow map depth pass
- `skinned_shadow.vert.hlsl` - Shadow map for animated models (uses shadow.frag.hlsl)

### Post-Processing
- `ssao.vert.hlsl` / `ssao.frag.hlsl` - Screen Space Ambient Occlusion calculation
- `ssao_blur.frag.hlsl` - SSAO blur pass (uses ssao.vert.hlsl)
- `ssao_gbuffer.vert.hlsl` / `ssao_gbuffer.frag.hlsl` - G-buffer generation for SSAO

## Shader Entry Points

All vertex shaders use `VSMain` as the entry point.
All fragment shaders use `PSMain` as the entry point.

## Coordinate System Notes

- SDL3 GPU uses a left-handed coordinate system
- Texture coordinates are Y-down (top-left is 0,0)
- Shadow map sampling requires Y-flip for correct results

## Uniform Buffer Conventions

- Slot 0 (`b0`): Primary uniforms (transforms, camera, lighting)
- Slot 1 (`b1`): Secondary uniforms (bone matrices, additional parameters)
- All structs are padded to 16-byte alignment for GPU compatibility

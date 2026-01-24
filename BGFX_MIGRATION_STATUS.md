# bgfx Migration Status

## Status: ✅ COMPLETE

The migration from OpenGL 3.3 (GLEW) to bgfx has been completed. All rendering subsystems now use bgfx APIs.

## Completed Components

### Build System
- CMakeLists.txt: bgfx.cmake via FetchContent, shader compilation with `-i` and `--varyingdef`

### Shader System  
- All bgfx .sc shader files in `client/shaders/`
- varying.def.sc files (no leading comments - breaks shaderc)

### Rendering Subsystems
1. **RenderContext** - bgfx::init(), SDL3 window handle, ViewId namespace
2. **ModelLoader** - bgfx vertex/index buffers, texture handles
3. **TerrainRenderer** - bgfx mesh rendering
4. **WorldRenderer** - Skybox, grid, mountains/rocks/trees
5. **GrassRenderer** - Billboard instancing with InstanceDataBuffer
6. **ShadowSystem** - Depth framebuffer, shadow pass (ViewId 0)
7. **UIRenderer** - Transient buffers, ViewId::UI (5)
8. **TextRenderer** - Font atlas, glyph rendering
9. **EffectRenderer** - Attack effects, billboards
10. **Main Renderer** - bgfx::frame(), view setup, window resize

### bgfx Utilities
- `bgfx_utils::load_shader()` - loads compiled shader binaries
- `bgfx_utils::load_program()` - creates shader programs  
- `bgfx_utils::load_texture()` - loads textures via SDL_image

## ViewId Architecture

```cpp
namespace ViewId {
    constexpr bgfx::ViewId Shadow = 0;
    constexpr bgfx::ViewId SSAO_GBuffer = 1;  // disabled
    constexpr bgfx::ViewId SSAO_Calc = 2;     // disabled
    constexpr bgfx::ViewId SSAO_Blur = 3;     // disabled
    constexpr bgfx::ViewId Main = 4;
    constexpr bgfx::ViewId UI = 5;
}
```

## Build

```bash
cd build && cmake .. && ninja
```

## Notes
- SSAO currently disabled (can be re-enabled later)
- Shader varying.def.sc files must not have leading comments
- All render methods take bgfx::ViewId as first parameter

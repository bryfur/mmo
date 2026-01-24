# Render Subsystems

This directory contains modular rendering subsystems extracted from the main `renderer.cpp`. These provide cleaner, single-responsibility components for rendering.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Renderer                              │
│              (Main facade - renderer.cpp)                    │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│ RenderContext │   │ TerrainRenderer│   │ WorldRenderer │
│  (SDL/GL ctx) │   │   (terrain)    │   │(skybox,rocks) │
└───────────────┘   └───────────────┘   └───────────────┘
                              │                     │
        ┌─────────────────────┼─────────────────────┤
        │                     │                     │
        ▼                     ▼                     ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│  UIRenderer   │   │EffectRenderer │   │ ShadowSystem  │
│   (2D UI)     │   │  (attacks)    │   │ (shadows/SSAO)│
└───────────────┘   └───────────────┘   └───────────────┘
```

## Subsystems

### RenderContext (`render_context.hpp/cpp`)
Manages SDL window, OpenGL context, and core rendering state.
- Window creation and management
- GL context initialization
- Viewport management
- Common GL state helpers (depth test, culling, blending)

### TerrainRenderer (`terrain_renderer.hpp/cpp`)
Handles procedural terrain generation and rendering.
- Multi-octave noise terrain height calculation
- Terrain mesh generation
- Grass texture loading and rendering
- `get_height(x, z)` for terrain-aware object placement

### WorldRenderer (`world_renderer.hpp/cpp`)
Renders environmental world elements.
- Procedural skybox with sun position
- Distant mountains (3D models)
- Scattered rocks with distance culling
- Trees with forest grove clustering
- Debug grid

### UIRenderer (`ui_renderer.hpp/cpp`)
All 2D screen-space UI rendering.
- Primitive shapes (rectangles, circles, lines)
- Text rendering via TextRenderer
- Composite UI elements (buttons, health bars)
- Target reticle for ranged classes

### EffectRenderer (`effect_renderer.hpp/cpp`)
Visual attack effects for all player classes.
- Warrior: Sword slash arc
- Mage: Fireball projectile beam
- Paladin: Orbiting holy books AOE
- Archer: Arrow projectile with arc

### ShadowSystem (`shadow_system.hpp/cpp`)
Shadow mapping and SSAO post-processing.
- Shadow depth FBO and texture (4096x4096)
- Light space matrix calculation
- Screen-Space Ambient Occlusion (SSAO)
- G-buffer for position/normal data
- SSAO blur pass for soft shadows

## Usage

These subsystems are designed to be used as internal components of the main Renderer class. To integrate:

```cpp
#include "render/render_context.hpp"
#include "render/terrain_renderer.hpp"
#include "render/shadow_system.hpp"
// etc.

class Renderer {
private:
    RenderContext context_;
    TerrainRenderer terrain_;
    ShadowSystem shadows_;
    // ...
};
```

The main Renderer then acts as a facade, delegating to these focused subsystems while maintaining the same public API.

## Benefits

1. **Single Responsibility**: Each subsystem handles one specific aspect
2. **Testability**: Subsystems can be tested in isolation
3. **Maintainability**: Smaller, focused files are easier to understand
4. **Reusability**: Subsystems can be used independently if needed

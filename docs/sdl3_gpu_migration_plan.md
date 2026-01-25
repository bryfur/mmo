# SDL3 GPU API Migration Plan

## Overview

This document outlines the migration from OpenGL 4.1 to SDL3 GPU API. The migration enables cross-platform support for Vulkan, Metal, and D3D12 backends.

**Current State:** OpenGL 4.1 with GLEW, 12 files with direct GL calls, ~20 GLSL shaders  
**Target State:** SDL3 GPU API with pre-compiled shaders for Metal/Vulkan/D3D12

---

## Agent Task Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      PHASE 0: ARCHITECTURE REFACTORING                      │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
                        ┌─────────────────────────┐
                        │ Task 0: Decouple        │
                        │ Rendering from Game     │
                        │ (Architecture Agent)    │
                        └───────────┬─────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE 1: INFRASTRUCTURE                           │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
                        ┌─────────────────────────┐
                        │   Task 1: Scaffolding   │
                        │   (Infrastructure Agent)│
                        └───────────┬─────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE 2: CORE SYSTEMS                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
              ┌─────────────────────┼─────────────────────┐
              ▼                     ▼                     ▼
   ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
   │ Task 2: Shaders  │  │ Task 3: GPU      │  │ Task 4: Buffer   │
   │ (Shader Agent)   │  │ Context          │  │ Abstraction      │
   │                  │  │ (Context Agent)  │  │ (Buffer Agent)   │
   └────────┬─────────┘  └────────┬─────────┘  └────────┬─────────┘
            │                     │                     │
            └─────────────────────┼─────────────────────┘
                                  │
                                  ▼
                        ┌─────────────────────┐
                        │  Task 5: Pipeline   │
                        │  System             │
                        │  (Pipeline Agent)   │
                        └─────────┬───────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE 3: RENDERER PORTS                           │
└─────────────────────────────────────────────────────────────────────────────┘
                                  │
    ┌─────────────┬───────────────┼───────────────┬─────────────┐
    ▼             ▼               ▼               ▼             ▼
┌────────┐  ┌──────────┐  ┌─────────────┐  ┌──────────┐  ┌──────────┐
│Task 6  │  │Task 7    │  │Task 8       │  │Task 9    │  │Task 10   │
│Model   │  │Terrain   │  │World        │  │UI        │  │Text      │
│Loader  │  │Renderer  │  │Renderer     │  │Renderer  │  │Renderer  │
└────┬───┘  └────┬─────┘  └──────┬──────┘  └────┬─────┘  └────┬─────┘
     │           │               │              │             │
     └───────────┴───────────────┼──────────────┴─────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE 4: ADVANCED SYSTEMS                         │
└─────────────────────────────────────────────────────────────────────────────┘
                                 │
              ┌──────────────────┼──────────────────┐
              ▼                  ▼                  ▼
   ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
   │ Task 11: Shadow  │ │ Task 12: Effects │ │ Task 13: Grass   │
   │ System           │ │ Renderer         │ │ Renderer         │
   └────────┬─────────┘ └────────┬─────────┘ └────────┬─────────┘
            │                    │                    │
            └────────────────────┼────────────────────┘
                                 │
                                 ▼
                       ┌─────────────────────┐
                       │  Task 14: Main      │
                       │  Renderer Facade    │
                       │  Integration        │
                       └─────────┬───────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE 5: CLEANUP & TESTING                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
                       ┌─────────────────────┐
                       │  Task 15: Cleanup   │
                       │  & Validation       │
                       └─────────────────────┘
```

---

## Phase 0: Architecture Refactoring (Pre-Migration)

### Task 0: Decouple Rendering from Game Logic

**Agent:** Architecture Agent  
**Dependencies:** None  
**Estimated Time:** 4-6 hours

#### Problem Statement
Currently `game.cpp` has 101 direct `renderer_.` calls, mixing game logic with rendering concerns. This makes the GPU migration harder and violates separation of concerns.

#### Objectives
1. Create a `RenderScene` abstraction that collects what to render
2. Add `Renderable` ECS components for entities
3. Move all draw calls from `Game` to render systems
4. Game only updates state and populates the render scene

#### Files to Create
- `client/scene/render_scene.hpp` - Scene graph / render queue
- `client/scene/render_scene.cpp`
- `client/scene/ui_scene.hpp` - UI element collection
- `client/scene/ui_scene.cpp`
- `client/systems/render_system.hpp` - ECS system that renders entities
- `client/systems/render_system.cpp`

#### Files to Modify
- `client/game.hpp` - Remove direct renderer calls
- `client/game.cpp` - Use RenderScene instead of direct draws
- `common/ecs/components.hpp` - Add renderable components

#### New Components
```cpp
// In common/ecs/components.hpp
namespace mmo::ecs {

// Marks an entity as renderable with a 3D model
struct ModelRenderable {
    std::string model_name;  // Key in ModelManager
    glm::vec4 tint = {1, 1, 1, 1};
    float scale = 1.0f;
};

// For 2D sprites/billboards
struct SpriteRenderable {
    std::string texture_name;
    float width = 1.0f;
    float height = 1.0f;
};

// Health bar display
struct HealthBarRenderable {
    float width = 1.0f;
    float y_offset = 2.0f;  // Height above entity
    bool show_always = false;
};

} // namespace mmo::ecs
```

#### RenderScene Interface
```cpp
// client/scene/render_scene.hpp
namespace mmo {

struct RenderCommand {
    enum class Type { Model, Sprite, Effect, Terrain, Skybox, UI };
    Type type;
    // ... command-specific data
};

class RenderScene {
public:
    void clear();
    
    // 3D world commands
    void add_model(const std::string& model, const glm::mat4& transform, 
                   const glm::vec4& tint = {1,1,1,1});
    void add_skinned_model(const std::string& model, const glm::mat4& transform,
                           const std::array<glm::mat4, 64>& bones);
    void add_effect(const ecs::AttackEffect& effect);
    
    // The renderer consumes these
    const std::vector<RenderCommand>& commands() const { return commands_; }
    
private:
    std::vector<RenderCommand> commands_;
};

class UIScene {
public:
    void clear();
    
    void add_rect(float x, float y, float w, float h, uint32_t color);
    void add_text(const std::string& text, float x, float y, float scale, uint32_t color);
    void add_button(float x, float y, float w, float h, const std::string& label, 
                    uint32_t color, bool selected);
    
    const std::vector<UICommand>& commands() const { return commands_; }
    
private:
    std::vector<UICommand> commands_;
};

} // namespace mmo
```

#### Game.cpp Changes (Before/After)
```cpp
// BEFORE (current - 101 renderer_ calls)
void Game::render_class_select() {
    renderer_.begin_frame();
    renderer_.begin_ui();
    renderer_.draw_filled_rect(0, 0, width, 100, 0xFF332211);
    renderer_.draw_ui_text("SELECT YOUR CLASS", ...);
    // ... 50+ more draw calls
    renderer_.end_ui();
    renderer_.end_frame();
}

// AFTER (scene-based)
void Game::render_class_select() {
    ui_scene_.clear();
    
    // Populate UI scene (what to draw, not how)
    ui_scene_.add_rect(0, 0, width, 100, 0xFF332211);
    ui_scene_.add_text("SELECT YOUR CLASS", center_x - 150, 30, 2.0f, 0xFFFFFFFF);
    // ... rest of UI elements
    
    // Single call to render the scene
    renderer_.render(render_scene_, ui_scene_);
}

// For playing state
void Game::render_playing() {
    render_scene_.clear();
    ui_scene_.clear();
    
    // ECS render system populates the scene
    render_system_.collect_renderables(registry_, render_scene_);
    
    // Populate UI
    build_playing_ui(ui_scene_);
    
    // Render everything
    renderer_.render(render_scene_, ui_scene_);
}
```

#### Render System
```cpp
// client/systems/render_system.cpp
void RenderSystem::collect_renderables(entt::registry& registry, RenderScene& scene) {
    // Collect all model renderables
    auto model_view = registry.view<ecs::Transform, ecs::ModelRenderable>();
    for (auto entity : model_view) {
        auto& transform = model_view.get<ecs::Transform>(entity);
        auto& renderable = model_view.get<ecs::ModelRenderable>(entity);
        
        glm::mat4 model_matrix = compute_model_matrix(transform, renderable.scale);
        scene.add_model(renderable.model_name, model_matrix, renderable.tint);
    }
    
    // Collect effects
    auto effect_view = registry.view<ecs::AttackEffect>();
    for (auto entity : effect_view) {
        scene.add_effect(effect_view.get<ecs::AttackEffect>(entity));
    }
}
```

#### Acceptance Criteria
- [ ] `Game.cpp` has zero direct `renderer_.draw_*` calls
- [ ] All entities use ECS renderable components
- [ ] RenderScene collects all render commands
- [ ] Renderer consumes RenderScene to draw
- [ ] Game only updates state, never pixels
- [ ] No visual changes (looks identical to before)

#### Benefits for GPU Migration
1. **Single point of change** - Renderer internals change, scene interface stays same
2. **Easier batching** - Can sort/batch commands before rendering
3. **Testable** - Can verify scene contents without GPU
4. **Parallelizable** - Game update and render command generation can overlap

---

## Phase 1: Infrastructure

### Task 1: Scaffolding & Build System Setup

**Agent:** Infrastructure Agent  
**Dependencies:** Task 0  
**Estimated Time:** 2-3 hours

#### Objectives
1. Update CMakeLists.txt to remove OpenGL/GLEW dependencies
2. Add SDL_shadercross as a dependency for shader compilation
3. Create shader compilation infrastructure
4. Create new header files with GPU abstraction types

#### Files to Modify
- `CMakeLists.txt`

#### Files to Create
- `client/gpu/gpu_types.hpp` - Common GPU type definitions
- `client/gpu/gpu_device.hpp` - GPU device wrapper header
- `client/gpu/gpu_device.cpp` - GPU device wrapper implementation
- `client/gpu/gpu_buffer.hpp` - Buffer abstraction header
- `client/gpu/gpu_buffer.cpp` - Buffer abstraction implementation  
- `client/gpu/gpu_texture.hpp` - Texture abstraction header
- `client/gpu/gpu_texture.cpp` - Texture abstraction implementation
- `client/gpu/gpu_pipeline.hpp` - Pipeline abstraction header
- `client/gpu/gpu_pipeline.cpp` - Pipeline abstraction implementation
- `client/gpu/gpu_shader.hpp` - Shader loading header
- `client/gpu/gpu_shader.cpp` - Shader loading implementation
- `shaders/` - New shader directory structure
- `shaders/CMakeLists.txt` - Shader compilation rules
- `tools/compile_shaders.py` - Shader compilation helper script

#### CMakeLists.txt Changes
```cmake
# REMOVE these lines:
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# REMOVE from target_link_libraries:
OpenGL::GL
GLEW::GLEW

# ADD SDL_shadercross:
FetchContent_Declare(
    SDL_shadercross
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
    GIT_TAG main
)
FetchContent_MakeAvailable(SDL_shadercross)

# ADD new source files to mmo_client
```

#### gpu_types.hpp Skeleton
```cpp
#pragma once
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace mmo::gpu {

// Forward declarations
class GPUDevice;
class GPUBuffer;
class GPUTexture;
class GPUPipeline;
class GPUShader;

// Vertex formats matching existing structures
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::vec4 color;
};

struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::vec4 color;
    uint8_t joints[4];
    float weights[4];
};

struct Vertex2D {
    glm::vec2 position;
    glm::vec2 texcoord;
    glm::vec4 color;
};

// Uniform buffer structures (must match shader layouts)
struct CameraUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 camera_pos;
    float padding;
};

struct ModelUniforms {
    glm::mat4 model;
    glm::vec4 tint;
};

struct LightUniforms {
    glm::vec3 light_dir;
    float ambient;
    glm::vec3 light_color;
    float padding;
    glm::mat4 light_space_matrix;
};

} // namespace mmo::gpu
```

#### Acceptance Criteria
- [ ] Project compiles without OpenGL/GLEW
- [ ] SDL_shadercross is available
- [ ] All new header files exist with proper includes
- [ ] New `client/gpu/` directory structure created
- [ ] Shader directory structure created

---

## Phase 2: Core Systems

### Task 2: Shader System

**Agent:** Shader Agent  
**Dependencies:** Task 1  
**Estimated Time:** 4-6 hours

#### Objectives
1. Convert all GLSL shaders to HLSL (SDL_shadercross source format)
2. Create shader metadata system for resource binding
3. Implement shader loading from compiled binaries

#### Files to Create
- `shaders/src/model.vert.hlsl`
- `shaders/src/model.frag.hlsl`
- `shaders/src/skinned_model.vert.hlsl`
- `shaders/src/skinned_model.frag.hlsl`
- `shaders/src/terrain.vert.hlsl`
- `shaders/src/terrain.frag.hlsl`
- `shaders/src/skybox.vert.hlsl`
- `shaders/src/skybox.frag.hlsl`
- `shaders/src/ui.vert.hlsl`
- `shaders/src/ui.frag.hlsl`
- `shaders/src/billboard.vert.hlsl`
- `shaders/src/billboard.frag.hlsl`
- `shaders/src/shadow.vert.hlsl`
- `shaders/src/shadow.frag.hlsl`
- `shaders/src/grass.vert.hlsl`
- `shaders/src/grass.frag.hlsl`
- `shaders/src/effect.vert.hlsl`
- `shaders/src/effect.frag.hlsl`
- `shaders/src/text.vert.hlsl`
- `shaders/src/text.frag.hlsl`
- `shaders/src/ssao.vert.hlsl`
- `shaders/src/ssao.frag.hlsl`

#### Files to Modify
- `client/gpu/gpu_shader.cpp` - Implement shader loading

#### Reference: Current GLSL Shaders Location
All current shaders are embedded in `client/shader.hpp` as string literals. Extract and convert each one.

#### Shader Conversion Notes
- SDL3 GPU uses left-handed coordinates (Y-down in texture coords)
- Uniform slots: 4 per stage (vertex/fragment)
- Use `[[vk::binding(N)]]` or HLSL register syntax
- Samplers and textures are separate bindings

#### Acceptance Criteria
- [ ] All shaders converted to HLSL
- [ ] Shader compilation script works
- [ ] Compiled shaders output to `shaders/compiled/`
- [ ] GPUShader class can load and create SDL_GPUShader objects

---

### Task 3: GPU Context & Device

**Agent:** Context Agent  
**Dependencies:** Task 1  
**Estimated Time:** 2-3 hours

#### Objectives
1. Implement GPUDevice wrapper class
2. Replace SDL OpenGL context with SDL GPU device
3. Implement frame lifecycle (command buffer acquire/submit)

#### Files to Modify
- `client/gpu/gpu_device.cpp`
- `client/render/render_context.hpp`
- `client/render/render_context.cpp`

#### GPUDevice Implementation Requirements
```cpp
class GPUDevice {
public:
    bool init(SDL_Window* window);
    void shutdown();
    
    // Frame lifecycle
    SDL_GPUCommandBuffer* begin_frame();
    void end_frame(SDL_GPUCommandBuffer* cmd);
    
    // Swapchain
    SDL_GPUTexture* acquire_swapchain_texture(SDL_GPUCommandBuffer* cmd);
    
    // Resource creation (delegates to SDL)
    SDL_GPUBuffer* create_buffer(const SDL_GPUBufferCreateInfo& info);
    SDL_GPUTexture* create_texture(const SDL_GPUTextureCreateInfo& info);
    SDL_GPUSampler* create_sampler(const SDL_GPUSamplerCreateInfo& info);
    SDL_GPUGraphicsPipeline* create_pipeline(const SDL_GPUGraphicsPipelineCreateInfo& info);
    
    // Accessors
    SDL_GPUDevice* device() const { return device_; }
    SDL_Window* window() const { return window_; }
    int width() const;
    int height() const;
    
private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_Window* window_ = nullptr;
};
```

#### RenderContext Changes
- Remove all `SDL_GL_*` calls
- Remove GLEW initialization
- Replace `gl_context_` with `GPUDevice`
- Update `begin_frame()` / `end_frame()` to use command buffers

#### Acceptance Criteria
- [ ] GPUDevice can initialize with Metal/Vulkan backend
- [ ] Window displays (even if blank)
- [ ] Command buffer lifecycle works
- [ ] Clean shutdown without leaks

---

### Task 4: Buffer Abstraction

**Agent:** Buffer Agent  
**Dependencies:** Task 1, Task 3  
**Estimated Time:** 2-3 hours

#### Objectives
1. Implement GPUBuffer class for vertex/index/uniform buffers
2. Create upload utilities using transfer buffers
3. Support both static and dynamic buffer patterns

#### Files to Modify
- `client/gpu/gpu_buffer.cpp`
- `client/gpu/gpu_texture.cpp`

#### GPUBuffer Implementation Requirements
```cpp
class GPUBuffer {
public:
    enum class Type { Vertex, Index, Uniform, Storage };
    
    static std::unique_ptr<GPUBuffer> create_static(
        GPUDevice& device, 
        Type type,
        const void* data, 
        size_t size
    );
    
    static std::unique_ptr<GPUBuffer> create_dynamic(
        GPUDevice& device,
        Type type,
        size_t size
    );
    
    void update(SDL_GPUCommandBuffer* cmd, const void* data, size_t size, size_t offset = 0);
    
    SDL_GPUBuffer* handle() const { return buffer_; }
    size_t size() const { return size_; }
    
private:
    SDL_GPUBuffer* buffer_ = nullptr;
    SDL_GPUTransferBuffer* transfer_ = nullptr; // For dynamic buffers
    size_t size_ = 0;
    Type type_;
};
```

#### GPUTexture Implementation Requirements
```cpp
class GPUTexture {
public:
    static std::unique_ptr<GPUTexture> create_2d(
        GPUDevice& device,
        int width, int height,
        SDL_GPUTextureFormat format,
        SDL_GPUTextureUsageFlags usage
    );
    
    static std::unique_ptr<GPUTexture> load_from_file(
        GPUDevice& device,
        const std::string& path
    );
    
    static std::unique_ptr<GPUTexture> create_depth(
        GPUDevice& device,
        int width, int height
    );
    
    void upload(SDL_GPUCommandBuffer* cmd, const void* pixels);
    
    SDL_GPUTexture* handle() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    
private:
    SDL_GPUTexture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};
```

#### Acceptance Criteria
- [ ] Can create vertex buffers and upload mesh data
- [ ] Can create index buffers
- [ ] Can load textures from files (using SDL_image)
- [ ] Can create depth textures for shadow mapping
- [ ] Dynamic buffer updates work

---

### Task 5: Pipeline System

**Agent:** Pipeline Agent  
**Dependencies:** Task 2, Task 3, Task 4  
**Estimated Time:** 3-4 hours

#### Objectives
1. Implement GPUPipeline class
2. Create pipeline cache/registry
3. Define vertex input layouts for all vertex types

#### Files to Modify
- `client/gpu/gpu_pipeline.cpp`

#### Files to Create
- `client/gpu/pipeline_registry.hpp`
- `client/gpu/pipeline_registry.cpp`

#### GPUPipeline Implementation Requirements
```cpp
struct PipelineConfig {
    SDL_GPUShader* vertex_shader;
    SDL_GPUShader* fragment_shader;
    SDL_GPUVertexInputState vertex_input;
    SDL_GPUPrimitiveType primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    SDL_GPURasterizerState rasterizer;
    SDL_GPUDepthStencilState depth_stencil;
    SDL_GPUColorTargetBlendState blend_state;
    SDL_GPUTextureFormat color_format;
    SDL_GPUTextureFormat depth_format;
    bool has_depth = true;
};

class GPUPipeline {
public:
    static std::unique_ptr<GPUPipeline> create(GPUDevice& device, const PipelineConfig& config);
    
    void bind(SDL_GPURenderPass* pass);
    
    SDL_GPUGraphicsPipeline* handle() const { return pipeline_; }
    
private:
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
};
```

#### Pipeline Registry
```cpp
class PipelineRegistry {
public:
    void init(GPUDevice& device);
    void shutdown();
    
    GPUPipeline* get_model_pipeline();
    GPUPipeline* get_skinned_model_pipeline();
    GPUPipeline* get_terrain_pipeline();
    GPUPipeline* get_skybox_pipeline();
    GPUPipeline* get_ui_pipeline();
    GPUPipeline* get_billboard_pipeline();
    GPUPipeline* get_shadow_pipeline();
    GPUPipeline* get_grass_pipeline();
    GPUPipeline* get_effect_pipeline();
    GPUPipeline* get_text_pipeline();
    
private:
    std::unordered_map<std::string, std::unique_ptr<GPUPipeline>> pipelines_;
    GPUDevice* device_ = nullptr;
};
```

#### Vertex Input Layouts
Define `SDL_GPUVertexInputState` for:
- `Vertex3D` (position, normal, texcoord, color)
- `SkinnedVertex` (above + joints, weights)
- `Vertex2D` (position, texcoord, color)
- `GrassVertex` (position, normal, texcoord + instance data)

#### Acceptance Criteria
- [ ] All pipeline types can be created
- [ ] Pipeline registry caches pipelines
- [ ] Vertex input states match shader expectations
- [ ] Blend states configured correctly (opaque, alpha blend, additive)

---

## Phase 3: Renderer Ports

### Task 6: Model Loader

**Agent:** Model Agent  
**Dependencies:** Task 4, Task 5  
**Estimated Time:** 3-4 hours

#### Objectives
1. Replace GL VAO/VBO with GPUBuffer
2. Update Mesh struct to use new buffer types
3. Maintain animation/skeleton support

#### Files to Modify
- `client/model_loader.hpp`
- `client/model_loader.cpp`

#### Key Changes
```cpp
// OLD:
struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    // ...
};

// NEW:
struct Mesh {
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer;
    std::unique_ptr<gpu::GPUBuffer> index_buffer;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    // ...
};
```

#### Draw Method Changes
```cpp
// OLD:
void draw() {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, 0);
}

// NEW:
void bind_buffers(SDL_GPURenderPass* pass) {
    SDL_GPUBufferBinding vbuf = { vertex_buffer->handle(), 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbuf, 1);
    SDL_GPUBufferBinding ibuf = { index_buffer->handle(), 0 };
    SDL_BindGPUIndexBuffer(pass, &ibuf, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}
// Draw call happens at renderer level
```

#### Acceptance Criteria
- [ ] Models load and create GPU buffers
- [ ] Skinned models work with bone data
- [ ] Texture loading uses GPUTexture
- [ ] No GL calls remain in model_loader

---

### Task 7: Terrain Renderer

**Agent:** Terrain Agent  
**Dependencies:** Task 5, Task 6  
**Estimated Time:** 2-3 hours

#### Objectives
1. Port terrain mesh generation to GPUBuffer
2. Use terrain pipeline from registry
3. Implement terrain render pass

#### Files to Modify
- `client/render/terrain_renderer.hpp`
- `client/render/terrain_renderer.cpp`

#### Key Changes
- Replace `GLuint vao_, vbo_, ebo_` with `GPUBuffer` instances
- Replace `glUniform*` calls with `SDL_PushGPU*UniformData`
- Replace draw calls with `SDL_DrawGPUIndexedPrimitives`

#### Acceptance Criteria
- [ ] Terrain renders correctly
- [ ] Height-based coloring works
- [ ] No GL calls remain

---

### Task 8: World Renderer

**Agent:** World Agent  
**Dependencies:** Task 5, Task 6  
**Estimated Time:** 3-4 hours

#### Objectives
1. Port skybox rendering (cubemap texture)
2. Port mountain/rock/tree rendering
3. Port grid overlay

#### Files to Modify
- `client/render/world_renderer.hpp`
- `client/render/world_renderer.cpp`

#### Skybox Notes
- Create cubemap texture with `SDL_GPU_TEXTURETYPE_CUBE`
- Skybox pipeline needs depth test disabled or depth write disabled
- Draw full-screen quad or cube

#### Acceptance Criteria
- [ ] Skybox renders
- [ ] Mountains/rocks/trees render with correct transforms
- [ ] Grid overlay works
- [ ] No GL calls remain

---

### Task 9: UI Renderer

**Agent:** UI Agent  
**Dependencies:** Task 4, Task 5  
**Estimated Time:** 2-3 hours

#### Objectives
1. Port 2D quad/shape rendering
2. Implement dynamic vertex buffer for UI
3. Handle screen-space projection

#### Files to Modify
- `client/render/ui_renderer.hpp`
- `client/render/ui_renderer.cpp`

#### Key Changes
- UI uses orthographic projection (2D)
- Dynamic vertex buffer for batched UI elements
- Separate render pass (or at end of main pass with depth test off)
- Alpha blending enabled

#### Acceptance Criteria
- [ ] Rectangles, circles, lines render
- [ ] Buttons and UI text backgrounds work
- [ ] Alpha blending correct
- [ ] No GL calls remain

---

### Task 10: Text Renderer

**Agent:** Text Agent  
**Dependencies:** Task 9  
**Estimated Time:** 2-3 hours

#### Objectives
1. Port text rendering (SDL_ttf integration)
2. Font atlas texture management
3. Text batching

#### Files to Modify
- `client/render/text_renderer.hpp`
- `client/render/text_renderer.cpp`

#### Notes
- SDL_ttf creates SDL_Surface, convert to GPUTexture
- May need glyph caching system
- Text shader samples font texture with alpha

#### Acceptance Criteria
- [ ] Text renders at correct positions
- [ ] Multiple font sizes work
- [ ] Color tinting works
- [ ] No GL calls remain

---

## Phase 4: Advanced Systems

### Task 11: Shadow System

**Agent:** Shadow Agent  
**Dependencies:** Task 5, Task 6  
**Estimated Time:** 3-4 hours

#### Objectives
1. Create depth-only render pass for shadow map
2. Port shadow map texture to GPUTexture
3. Implement shadow sampling in main shaders

#### Files to Modify
- `client/render/shadow_system.hpp`
- `client/render/shadow_system.cpp`

#### Shadow Mapping with SDL3 GPU
```cpp
// Create shadow map texture
SDL_GPUTextureCreateInfo shadow_tex_info = {
    .type = SDL_GPU_TEXTURETYPE_2D,
    .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
    .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
    .width = shadow_size,
    .height = shadow_size,
    .layer_count_or_depth = 1,
    .num_levels = 1,
};

// Shadow render pass
SDL_GPUDepthStencilTargetInfo depth_target = {
    .texture = shadow_texture_,
    .load_op = SDL_GPU_LOADOP_CLEAR,
    .store_op = SDL_GPU_STOREOP_STORE,
    .clear_depth = 1.0f,
};
SDL_GPURenderPass* shadow_pass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth_target);
```

#### Acceptance Criteria
- [ ] Shadow map renders depth correctly
- [ ] Main pass samples shadow map
- [ ] Shadow acne/peter-panning handled
- [ ] No GL calls remain

---

### Task 12: Effect Renderer

**Agent:** Effects Agent  
**Dependencies:** Task 5, Task 6  
**Estimated Time:** 2-3 hours

#### Objectives
1. Port attack effect rendering
2. Billboards and particle effects
3. Additive blending effects

#### Files to Modify
- `client/render/effect_renderer.hpp`
- `client/render/effect_renderer.cpp`

#### Blend States
- Warrior slash: Alpha blend
- Mage beam: Additive blend
- Paladin AOE: Additive blend
- Archer arrow: Alpha blend

#### Acceptance Criteria
- [ ] All attack effects render
- [ ] Blend modes correct
- [ ] Billboard orientation works
- [ ] No GL calls remain

---

### Task 13: Grass Renderer

**Agent:** Grass Agent  
**Dependencies:** Task 5, Task 6  
**Estimated Time:** 2-3 hours

#### Objectives
1. Port instanced grass rendering
2. Instance buffer for grass positions
3. Wind animation in shader

#### Files to Modify
- `client/render/grass_renderer.hpp`
- `client/render/grass_renderer.cpp`

#### Instancing with SDL3 GPU
```cpp
// Vertex buffer binding with instancing
SDL_GPUVertexBufferDescription vbuf_descs[2] = {
    { .slot = 0, .pitch = sizeof(GrassVertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX },
    { .slot = 1, .pitch = sizeof(GrassInstance), .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE },
};

// Draw instanced
SDL_DrawGPUIndexedPrimitives(pass, index_count, instance_count, 0, 0, 0);
```

#### Acceptance Criteria
- [ ] Grass renders with instancing
- [ ] Wind animation works
- [ ] Density and distribution correct
- [ ] No GL calls remain

---

## Phase 5: Integration & Cleanup

### Task 14: Main Renderer Integration

**Agent:** Integration Agent  
**Dependencies:** Tasks 6-13  
**Estimated Time:** 3-4 hours

#### Objectives
1. Update main Renderer class to orchestrate GPU passes
2. Implement proper render pass structure
3. Remove all legacy GL code from renderer.cpp

#### Files to Modify
- `client/renderer.hpp`
- `client/renderer.cpp`

#### New Frame Structure
```cpp
void Renderer::render_frame() {
    auto* cmd = device_.begin_frame();
    
    // 1. Shadow pass
    begin_shadow_pass(cmd);
    draw_shadow_casters(cmd);
    end_shadow_pass(cmd);
    
    // 2. Main 3D pass
    auto* swapchain = device_.acquire_swapchain_texture(cmd);
    begin_main_pass(cmd, swapchain);
    draw_skybox(cmd);
    draw_terrain(cmd);
    draw_world_objects(cmd);
    draw_entities(cmd);
    draw_effects(cmd);
    draw_grass(cmd);
    end_main_pass(cmd);
    
    // 3. UI pass (or same pass, depth test off)
    begin_ui_pass(cmd, swapchain);
    draw_ui(cmd);
    draw_text(cmd);
    end_ui_pass(cmd);
    
    device_.end_frame(cmd);
}
```

#### Acceptance Criteria
- [ ] Full frame renders correctly
- [ ] All subsystems integrated
- [ ] Proper pass ordering
- [ ] No GL calls in entire client

---

### Task 15: Cleanup & Validation

**Agent:** Cleanup Agent  
**Dependencies:** Task 14  
**Estimated Time:** 2-3 hours

#### Objectives
1. Remove all OpenGL includes
2. Delete old shader.hpp shader strings
3. Update CMakeLists.txt to remove GL references
4. Verify no GL calls via grep
5. Test on available platforms

#### Files to Modify
- `CMakeLists.txt` (final cleanup)
- `client/shader.hpp` (remove GL shaders, keep only as utility class or delete)
- `client/shader.cpp` (remove or repurpose)
- All files: remove `#include <GL/glew.h>`

#### Validation Commands
```bash
# Verify no GL calls remain
grep -r "glGen\|glBind\|glCreate\|GL_" client/

# Verify no GL includes
grep -r "#include.*glew\|#include.*GL/" client/

# Build and run
cmake --build build
./build/mmo_client
```

#### Acceptance Criteria
- [ ] Zero GL references in codebase
- [ ] Clean build with no warnings
- [ ] Game runs and renders correctly
- [ ] Tested with Metal backend (macOS)
- [ ] Memory clean (no leaks on shutdown)

---

## Appendix A: File Change Summary

### Files to Delete (Eventually)
- Remove OpenGL content from `client/shader.hpp` (keep file for Shader utility class if repurposed)

### New Files Created
```
client/scene/
├── render_scene.hpp
├── render_scene.cpp
├── ui_scene.hpp
└── ui_scene.cpp

client/systems/
├── render_system.hpp      (new)
└── render_system.cpp      (new)

client/gpu/
├── gpu_types.hpp
├── gpu_device.hpp
├── gpu_device.cpp
├── gpu_buffer.hpp
├── gpu_buffer.cpp
├── gpu_texture.hpp
├── gpu_texture.cpp
├── gpu_pipeline.hpp
├── gpu_pipeline.cpp
├── gpu_shader.hpp
├── gpu_shader.cpp
├── pipeline_registry.hpp
└── pipeline_registry.cpp

shaders/
├── CMakeLists.txt
├── src/
│   ├── model.vert.hlsl
│   ├── model.frag.hlsl
│   ├── skinned_model.vert.hlsl
│   ├── skinned_model.frag.hlsl
│   ├── terrain.vert.hlsl
│   ├── terrain.frag.hlsl
│   ├── skybox.vert.hlsl
│   ├── skybox.frag.hlsl
│   ├── ui.vert.hlsl
│   ├── ui.frag.hlsl
│   ├── billboard.vert.hlsl
│   ├── billboard.frag.hlsl
│   ├── shadow.vert.hlsl
│   ├── shadow.frag.hlsl
│   ├── grass.vert.hlsl
│   ├── grass.frag.hlsl
│   ├── effect.vert.hlsl
│   ├── effect.frag.hlsl
│   ├── text.vert.hlsl
│   ├── text.frag.hlsl
│   ├── ssao.vert.hlsl
│   └── ssao.frag.hlsl
└── compiled/
    └── (auto-generated)

tools/
└── compile_shaders.py
```

### Files Modified
```
CMakeLists.txt
common/ecs/components.hpp          (add renderable components)
client/game.hpp
client/game.cpp                    (major refactor - remove 101 renderer_ calls)
client/renderer.hpp
client/renderer.cpp
client/model_loader.hpp
client/model_loader.cpp
client/render/render_context.hpp
client/render/render_context.cpp
client/render/terrain_renderer.hpp
client/render/terrain_renderer.cpp
client/render/world_renderer.hpp
client/render/world_renderer.cpp
client/render/ui_renderer.hpp
client/render/ui_renderer.cpp
client/render/effect_renderer.hpp
client/render/effect_renderer.cpp
client/render/shadow_system.hpp
client/render/shadow_system.cpp
client/render/grass_renderer.hpp
client/render/grass_renderer.cpp
client/render/text_renderer.hpp
client/render/text_renderer.cpp
```

---

## Appendix B: GPU Abstraction Layer Explained

### Purpose

The `client/gpu/` directory provides a **convenience wrapper** around SDL3 GPU API. It keeps low-level GPU details in one place and gives renderer subsystems a clean, simple interface.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         WITHOUT GPU ABSTRACTION                             │
└─────────────────────────────────────────────────────────────────────────────┘

  TerrainRenderer ──┐
  WorldRenderer ────┤
  UIRenderer ───────┼──► Raw SDL3 GPU calls everywhere
  EffectRenderer ───┤     SDL_CreateGPUBuffer(), SDL_BindGPUVertexBuffers(), etc.
  GrassRenderer ────┤     (Verbose, repetitive, error-prone)
  ShadowSystem ─────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                          WITH GPU ABSTRACTION                               │
└─────────────────────────────────────────────────────────────────────────────┘

  TerrainRenderer ──┐                              ┌─────────────────────┐
  WorldRenderer ────┤                              │   client/gpu/       │
  UIRenderer ───────┼──► Clean, simple API ───────►│                     │──► SDL3 GPU
  EffectRenderer ───┤    GPUBuffer::create()       │   GPUDevice         │
  GrassRenderer ────┤    GPUTexture::load()        │   GPUBuffer         │
  ShadowSystem ─────┘    pipeline->bind()          │   GPUTexture        │
                                                   │   GPUPipeline       │
                                                   │   GPUShader         │
                                                   └─────────────────────┘
```

### Who Uses What

| File | Purpose | Used By |
|------|---------|---------|
| `gpu_device.hpp/cpp` | Manages SDL_GPUDevice, window, command buffers, frame lifecycle | `RenderContext`, main `Renderer` |
| `gpu_buffer.hpp/cpp` | Wraps vertex/index/uniform buffers with easy create/upload | Every renderer subsystem |
| `gpu_texture.hpp/cpp` | Wraps textures, handles loading from files, depth textures | `ModelLoader`, `ShadowSystem`, `UIRenderer` |
| `gpu_shader.hpp/cpp` | Loads compiled shader binaries, creates SDL_GPUShader | `PipelineRegistry` |
| `gpu_pipeline.hpp/cpp` | Wraps graphics pipelines (shader + state) | All renderers bind pipelines |
| `pipeline_registry.hpp/cpp` | Caches all pipelines, creates them once at startup | `Renderer` (single instance) |
| `gpu_types.hpp` | Common structs (vertex formats, uniform layouts) | Everyone |

### Before vs After: Buffer Creation

**Without abstraction (raw SDL3 GPU) - ~25 lines:**
```cpp
void TerrainRenderer::init() {
    // Create vertex buffer
    SDL_GPUBufferCreateInfo buf_info = {};
    buf_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buf_info.size = vertices.size() * sizeof(Vertex3D);
    vertex_buffer_ = SDL_CreateGPUBuffer(device, &buf_info);
    
    // Upload data via transfer buffer
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = buf_info.size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    void* map = SDL_MapGPUTransferBuffer(device, transfer, false);
    memcpy(map, vertices.data(), buf_info.size);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { transfer, 0 };
    SDL_GPUBufferRegion dst = { vertex_buffer_, 0, buf_info.size };
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    
    // Repeat for index buffer... another 25 lines
}
```

**With abstraction - 2 lines:**
```cpp
void TerrainRenderer::init() {
    vertex_buffer_ = GPUBuffer::create_static(device_, GPUBuffer::Type::Vertex, 
                                               vertices.data(), vertices.size() * sizeof(Vertex3D));
    index_buffer_ = GPUBuffer::create_static(device_, GPUBuffer::Type::Index,
                                              indices.data(), indices.size() * sizeof(uint32_t));
}
```

### The Abstraction Classes

```cpp
// gpu_buffer.hpp - Simplifies buffer management
class GPUBuffer {
public:
    enum class Type { Vertex, Index, Uniform, Storage };
    
    // Static buffer (upload once, use forever) - most geometry
    static std::unique_ptr<GPUBuffer> create_static(GPUDevice& device, Type type, 
                                                      const void* data, size_t size);
    
    // Dynamic buffer (update frequently) - UI, particles, uniforms
    static std::unique_ptr<GPUBuffer> create_dynamic(GPUDevice& device, Type type, size_t size);
    
    void update(SDL_GPUCommandBuffer* cmd, const void* data, size_t size);
    
    SDL_GPUBuffer* handle() const;  // For binding to render pass
};

// gpu_texture.hpp - Simplifies texture management
class GPUTexture {
public:
    // Load from file (PNG, JPG via SDL_image)
    static std::unique_ptr<GPUTexture> load_from_file(GPUDevice& device, const std::string& path);
    
    // Create render target / depth buffer
    static std::unique_ptr<GPUTexture> create_render_target(GPUDevice& device, int w, int h, Format fmt);
    static std::unique_ptr<GPUTexture> create_depth(GPUDevice& device, int w, int h);
    
    SDL_GPUTexture* handle() const;
};

// gpu_pipeline.hpp - Simplifies pipeline creation
class GPUPipeline {
public:
    static std::unique_ptr<GPUPipeline> create(GPUDevice& device, const PipelineConfig& config);
    
    void bind(SDL_GPURenderPass* pass);  // Calls SDL_BindGPUGraphicsPipeline
};
```

### Full Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  Game Logic (game.cpp)                                                      │
│  - Updates ECS, handles input                                               │
│  - Populates RenderScene/UIScene                                            │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ uses
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  Scene Layer (client/scene/)                                                │
│  - RenderScene: declarative list of models, effects to draw                 │
│  - UIScene: declarative list of rects, text to draw                         │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ consumed by
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  Renderer Subsystems (client/render/)                                       │
│  - TerrainRenderer, WorldRenderer, UIRenderer, EffectRenderer, etc.         │
│  - Each knows HOW to render its specific domain                             │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ uses
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  GPU Abstraction Layer (client/gpu/)                                        │
│  - GPUDevice: frame lifecycle, resource creation factory                    │
│  - GPUBuffer: vertex/index/uniform buffers with easy upload                 │
│  - GPUTexture: 2D textures, depth buffers, render targets                   │
│  - GPUPipeline: shader + render state combo                                 │
│  - PipelineRegistry: caches all pipelines, creates once at startup          │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ wraps
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  SDL3 GPU API                                                               │
│  - SDL_GPUDevice, SDL_GPUBuffer, SDL_GPUTexture, SDL_GPUPipeline, etc.      │
│  - Talks to Metal (macOS), Vulkan (Linux/Windows), D3D12 (Windows)          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Runtime Flow

```
1. Startup
   └─► Renderer::init()
       └─► GPUDevice::init(window)           // Create SDL_GPUDevice
       └─► PipelineRegistry::init(device)    // Create all pipelines (once)
       └─► TerrainRenderer::init(device)     // Create terrain buffers
       └─► WorldRenderer::init(device)       // Create world buffers
       └─► ... other subsystems init

2. Each Frame
   └─► Game::update()                        // Update ECS, populate scenes
   └─► Renderer::render(scene, ui)
       └─► GPUDevice::begin_frame()          // Acquire command buffer
       └─► ShadowSystem::render(cmd)         // Shadow pass
       └─► TerrainRenderer::draw(cmd, scene) // Bind pipeline, buffers, draw
       └─► WorldRenderer::draw(cmd, scene)   // Skybox, mountains, trees
       └─► EffectRenderer::draw(cmd, scene)  // Attack effects
       └─► GrassRenderer::draw(cmd)          // Instanced grass
       └─► UIRenderer::draw(cmd, ui)         // 2D UI overlay
       └─► GPUDevice::end_frame()            // Submit command buffer, present
```

### Benefits of This Abstraction

| Benefit | Explanation |
|---------|-------------|
| **DRY** | Buffer/texture upload logic written once, not repeated in 12 files |
| **Encapsulation** | Renderer subsystems don't know SDL3 GPU internals |
| **Future-proof** | If SDL4 or new API comes, change only `client/gpu/` |
| **Fewer bugs** | Common patterns (create, upload, bind, draw) correct by construction |
| **Readable** | `GPUBuffer::create_static()` vs 25 lines of raw SDL calls |
| **Testable** | Can mock GPUDevice for unit tests |

---

## Appendix C: Parallel Task Groups

Tasks that can run in parallel (after dependencies met):

**Group A (after Task 1):**
- Task 2 (Shaders)
- Task 3 (GPU Context)
- Task 4 (Buffers)

**Group B (after Tasks 2-5):**
- Task 6 (Model Loader)
- Task 7 (Terrain)
- Task 8 (World)
- Task 9 (UI)
- Task 10 (Text)

**Group C (after Group B):**
- Task 11 (Shadows)
- Task 12 (Effects)
- Task 13 (Grass)

**Sequential:**
- Task 14 (Integration) - after all Group C
- Task 15 (Cleanup) - after Task 14

---

## Appendix D: Risk Mitigation

| Risk | Mitigation |
|------|------------|
| SDL_shadercross not stable | Fall back to offline shader compilation with spirv-cross CLI |
| Performance regression | Profile early, optimize pipeline binds and buffer updates |
| Platform-specific bugs | Test on macOS Metal first (primary dev), then expand |
| Shader compilation errors | Keep old GL code in branch until migration validated |
| Missing features in SDL3 GPU | Check API coverage early, may need workarounds |

---

## Notes for Agents

1. **Always test after each task** - Build and verify rendering works
2. **Maintain backward compatibility during transition** - Use `#ifdef` if needed
3. **Document any API limitations discovered**
4. **Commit frequently with descriptive messages**
5. **If blocked, document the issue and move to next parallel task**

#pragma once

#include "engine/scene/render_scene.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/render/render_context.hpp"
#include "engine/render/terrain_renderer.hpp"
#include "engine/render/world_renderer.hpp"
#include "engine/render/ui_renderer.hpp"
#include "engine/render/effect_renderer.hpp"
#include "engine/render/grass_renderer.hpp"
#include "engine/render/shadow_map.hpp"
#include "engine/render/ambient_occlusion.hpp"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/model_loader.hpp"
#include "engine/graphics_settings.hpp"
#include "engine/scene/camera_state.hpp"
#include "engine/render_stats.hpp"
#include "engine/heightmap.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace mmo::engine::scene {

namespace gpu = mmo::engine::gpu;
namespace render = mmo::engine::render;

/**
 * SceneRenderer consumes RenderScene + UIScene and produces a rendered frame.
 * Owns all GPU resources, sub-renderers, and rendering state.
 * Game builds scenes; SceneRenderer draws them.
 */
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /**
     * Initialize all rendering subsystems.
     * @param context RenderContext owned by the application (must outlive this)
     * @param world_width World width for terrain/world renderers
     * @param world_height World height for terrain/world renderers
     */
    bool init(render::RenderContext& context, float world_width = 8000.0f, float world_height = 8000.0f);

    void shutdown();

    /**
     * Render a complete frame from scene descriptions.
     * @param scene 3D world scene commands
     * @param ui_scene 2D UI commands
     * @param camera Camera state for the frame
     * @param dt Delta time for animations/effects
     */
    void render_frame(const RenderScene& scene, const UIScene& ui_scene,
                      const CameraState& camera, float dt);

    // ========== Configuration ==========

    void set_heightmap(const Heightmap& heightmap);
    void set_anisotropic_filter(int level);
    void set_graphics_settings(const GraphicsSettings& settings);
    void set_vsync_mode(int mode);
    void set_screen_size(int width, int height);

    // ========== Debug Stats ==========

    void set_collect_stats(bool enabled) { collect_stats_ = enabled; }
    const engine::RenderStats& render_stats() const { return render_stats_; }

    // ========== Accessors ==========

    render::TerrainRenderer& terrain() { return terrain_; }
    ModelManager& models() { return *model_manager_; }
    render::GrassRenderer* grass() { return grass_renderer_.get(); }
    float get_terrain_height(float x, float z) { return terrain_.get_height(x, z); }

private:
    // Frame lifecycle
    void begin_frame();
    void end_frame();
    void begin_main_pass();
    void end_main_pass();
    void begin_ui();
    void end_ui();

    // Rendering
    void render_3d_scene(const RenderScene& scene, const CameraState& camera, float dt);
    void render_model_command(const ModelCommand& cmd, const CameraState& camera);
    void render_skinned_model_command(const SkinnedModelCommand& cmd, const CameraState& camera);
    void render_ui_commands(const UIScene& ui_scene, const CameraState& camera);
    void draw_billboard_3d(const Billboard3DCommand& cmd, const CameraState& camera);

    // Shadow rendering
    void render_shadow_passes(const RenderScene& scene, const CameraState& camera);
    void render_shadow_models(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                               const RenderScene& scene, int cascade_index);
    void bind_shadow_data(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, int sampler_slot);

    // Setup
    void init_pipelines();
    void init_billboard_buffers();
    void update_animations(float dt);

    // ========== Sub-renderers ==========
    render::RenderContext* context_ = nullptr;
    gpu::PipelineRegistry pipeline_registry_;
    render::TerrainRenderer terrain_;
    render::WorldRenderer world_;
    render::UIRenderer ui_;
    render::EffectRenderer effects_;
    std::unique_ptr<ModelManager> model_manager_;
    std::unique_ptr<render::GrassRenderer> grass_renderer_;
    render::ShadowMap shadow_map_;
    render::AmbientOcclusion ao_;

    // ========== GPU Resources ==========
    std::unique_ptr<gpu::GPUBuffer> billboard_vertex_buffer_;
    std::unique_ptr<gpu::GPUTexture> depth_texture_;
    SDL_GPUSampler* default_sampler_ = nullptr;

    // ========== Render State ==========
    SDL_GPURenderPass* main_render_pass_ = nullptr;
    SDL_GPUTexture* current_swapchain_ = nullptr;
    bool had_main_pass_this_frame_ = false;
    glm::vec3 light_dir_ = glm::vec3(-0.5f, -0.8f, -0.3f);
    float skybox_time_ = 0.0f;
    GraphicsSettings* graphics_ = nullptr;
    GraphicsSettings default_graphics_;

    // ========== Debug Stats ==========
    bool collect_stats_ = false;
    engine::RenderStats render_stats_;
};

} // namespace mmo::engine::scene

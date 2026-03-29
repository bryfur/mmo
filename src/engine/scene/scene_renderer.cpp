#include "scene_renderer.hpp"
#include "frustum.hpp"
#include "SDL3/SDL_gpu.h"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_pipeline.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/model_loader.hpp"
#include "engine/render/terrain_renderer.hpp"
#include "engine/render/world_renderer.hpp"
#include "engine/render/ui_renderer.hpp"
#include "engine/render/effect_renderer.hpp"
#include "engine/render/grass_renderer.hpp"
#include "engine/render/shadow_map.hpp"
#include "engine/render/ambient_occlusion.hpp"
#include "engine/render/bloom.hpp"
#include "engine/render/volumetric_fog.hpp"
#include "engine/systems/effect_system.hpp"
#include "engine/render_constants.hpp"
#include "engine/gpu/gpu_uniforms.hpp"
#include "engine/heightmap.hpp"
#include "engine/render/render_context.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <cstring>
#include <memory>
#include <type_traits>

#include "engine/graphics_settings.hpp"
#include "engine/scene/camera_state.hpp"
#include "engine/scene/render_scene.hpp"
#include "engine/scene/ui_scene.hpp"
#include "glm/common.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/matrix.hpp"

namespace mmo::engine::scene {

namespace gpu = mmo::engine::gpu;
namespace render = mmo::engine::render;
using namespace mmo::engine::render;

// O(1) handle-based model lookup with per-frame cache
static inline Model* get_model_cached(
    std::unordered_map<ModelHandle, Model*>& cache,
    ModelManager& mgr,
    ModelHandle handle)
{
    if (handle == INVALID_MODEL_HANDLE) return nullptr;
    auto it = cache.find(handle);
    if (it != cache.end()) return it->second;
    Model* m = mgr.get_model(handle);
    cache[handle] = m;
    return m;
}

static inline Model* get_model_cached_by_name(
    std::unordered_map<std::string, Model*>& name_cache,
    ModelManager& mgr,
    const std::string& name)
{
    if (name.empty()) return nullptr;
    auto it = name_cache.find(name);
    if (it != name_cache.end()) return it->second;
    Model* m = mgr.get_model(name);
    name_cache[name] = m;
    return m;
}

static inline Model* resolve_model(
    std::unordered_map<ModelHandle, Model*>& handle_cache,
    std::unordered_map<std::string, Model*>& name_cache,
    ModelManager& mgr,
    ModelHandle handle,
    const std::string& name)
{
    if (handle != INVALID_MODEL_HANDLE)
        return get_model_cached(handle_cache, mgr, handle);
    return get_model_cached_by_name(name_cache, mgr, name);
}

static inline ModelHandle resolve_handle(
    ModelManager& mgr,
    ModelHandle handle,
    const std::string& name)
{
    if (handle != INVALID_MODEL_HANDLE) return handle;
    return mgr.get_handle(name);
}

SceneRenderer::SceneRenderer()
    : pipeline_registry_(std::make_unique<gpu::PipelineRegistry>()),
      terrain_(std::make_unique<render::TerrainRenderer>()),
      world_(std::make_unique<render::WorldRenderer>()),
      ui_(std::make_unique<render::UIRenderer>()),
      effects_(std::make_unique<render::EffectRenderer>()),
      effect_system_(std::make_unique<mmo::engine::systems::EffectSystem>()),
      model_manager_(std::make_unique<ModelManager>()),
      grass_renderer_(std::make_unique<render::GrassRenderer>()),
      shadow_map_(std::make_unique<render::ShadowMap>()),
      ao_(std::make_unique<render::AmbientOcclusion>()),
      bloom_(std::make_unique<render::Bloom>()),
      volumetric_fog_(std::make_unique<render::VolumetricFog>()) {
}

SceneRenderer::~SceneRenderer() {
    shutdown();
}

// ========== Accessors (out-of-line for forward-declared types) ==========

render::RenderContext* SceneRenderer::context() { return context_; }
render::TerrainRenderer& SceneRenderer::terrain() { return *terrain_; }
ModelManager& SceneRenderer::models() { return *model_manager_; }
render::GrassRenderer* SceneRenderer::grass() { return grass_renderer_.get(); }
float SceneRenderer::get_terrain_height(float x, float z) { return terrain_->get_height(x, z); }

bool SceneRenderer::init(RenderContext& context, float world_width, float world_height) {
    context_ = &context;

    if (!pipeline_registry_->init(context_->device())) {
        std::cerr << "Failed to initialize pipeline registry" << std::endl;
        return false;
    }
    pipeline_registry_->set_swapchain_format(context_->swapchain_format());

    model_manager_->set_device(&context_->device());

    if (!terrain_->init(context_->device(), *pipeline_registry_, world_width, world_height)) {
        std::cerr << "Failed to initialize terrain renderer" << std::endl;
        return false;
    }

    if (!world_->init(context_->device(), *pipeline_registry_, world_width, world_height, model_manager_.get())) {
        std::cerr << "Failed to initialize world renderer" << std::endl;
        return false;
    }

    world_->set_terrain_height_func([this](float x, float z) {
        return terrain_->get_height(x, z);
    });

    int w = context_->width();
    int h = context_->height();

    if (!ui_->init(context_->device(), *pipeline_registry_, w, h)) {
        std::cerr << "Failed to initialize UI renderer" << std::endl;
        return false;
    }

    if (!effects_->init(context_->device(), *pipeline_registry_, model_manager_.get())) {
        std::cerr << "Failed to initialize effect renderer" << std::endl;
        return false;
    }
    effects_->set_terrain_height_func([this](float x, float z) {
        return terrain_->get_height(x, z);
    });

    depth_texture_ = gpu::GPUTexture::create_depth(context_->device(), w, h);
    if (!depth_texture_) {
        std::cerr << "Failed to create depth texture" << std::endl;
        return false;
    }

    init_pipelines();
    init_billboard_buffers();

    // Preload all pipelines upfront to avoid hitching during gameplay
    if (!pipeline_registry_->preload_all_pipelines()) {
        std::cerr << "Warning: Some pipelines failed to preload" << std::endl;
    }

    if (grass_renderer_) {
        grass_renderer_->init(context_->device(), *pipeline_registry_, world_width, world_height);
    }

    const GraphicsSettings& gfx = graphics_ ? *graphics_ : default_graphics_;
    static constexpr int resolution_table[] = {512, 1024, 2048, 4096};
    int shadow_res = resolution_table[std::clamp(gfx.shadow_resolution, 0, 3)];
    shadow_map_->set_active_cascades(gfx.shadow_cascades + 1);
    if (!shadow_map_->init(context_->device(), shadow_res)) {
        std::cerr << "Warning: Failed to initialize shadow map (shadows disabled)" << std::endl;
    }

    if (!ao_->init(context_->device(), w, h)) {
        std::cerr << "Warning: Failed to initialize GTAO (AO disabled)" << std::endl;
    }

    if (!bloom_->init(context_->device(), w, h)) {
        std::cerr << "Warning: Failed to initialize Bloom (bloom disabled)" << std::endl;
    }

    if (!volumetric_fog_->init(context_->device(), w, h)) {
        std::cerr << "Warning: Failed to initialize Volumetric Fog (fog disabled)" << std::endl;
    }

    return true;
}

void SceneRenderer::shutdown() {
    if (model_manager_) {
        model_manager_->unload_all();
    }

    if (grass_renderer_) {
        grass_renderer_->shutdown();
    }

    billboard_vertex_buffer_.reset();
    depth_texture_.reset();

    if (default_sampler_ && context_) {
        context_->device().release_sampler(default_sampler_);
        default_sampler_ = nullptr;
    }

    volumetric_fog_->shutdown();
    bloom_->shutdown();
    ao_->shutdown();
    shadow_map_->shutdown();
    effects_->shutdown();
    ui_->shutdown();
    world_->shutdown();
    terrain_->shutdown();
    pipeline_registry_->shutdown();
}

void SceneRenderer::init_pipelines() {
    auto* model_pipeline = pipeline_registry_->get_model_pipeline();
    auto* skinned_pipeline = pipeline_registry_->get_skinned_model_pipeline();
    auto* billboard_pipeline = pipeline_registry_->get_billboard_pipeline();

    if (!model_pipeline || !skinned_pipeline || !billboard_pipeline) {
        std::cerr << "Warning: Some pipelines failed to preload" << std::endl;
    }

    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.max_anisotropy = 16.0f;
    sampler_info.enable_anisotropy = true;
    default_sampler_ = context_->device().create_sampler(sampler_info);

    if (!default_sampler_) {
        std::cerr << "Warning: Failed to create default GPU sampler" << std::endl;
    }
}

void SceneRenderer::init_billboard_buffers() {
    constexpr size_t BILLBOARD_BUFFER_SIZE = 6 * 7 * sizeof(float);
    billboard_vertex_buffer_ = gpu::GPUBuffer::create_dynamic(
        context_->device(),
        gpu::GPUBuffer::Type::Vertex,
        BILLBOARD_BUFFER_SIZE
    );

    if (!billboard_vertex_buffer_) {
        std::cerr << "Warning: Failed to create billboard vertex buffer" << std::endl;
    }
}

// ============================================================================
// Configuration
// ============================================================================

void SceneRenderer::set_screen_size(int width, int height) {
    ui_->set_screen_size(width, height);
    if (ao_->is_ready()) {
        ao_->resize(width, height);
    }
    if (volumetric_fog_->is_ready()) {
        volumetric_fog_->resize(width, height);
    }
}

void SceneRenderer::set_graphics_settings(const GraphicsSettings& settings) {
    // Check if shadow settings changed
    if (graphics_) {
        static constexpr int resolution_table[] = {512, 1024, 2048, 4096};
        int new_res = resolution_table[std::clamp(settings.shadow_resolution, 0, 3)];
        int new_cascades = settings.shadow_cascades + 1;

        if (new_res != shadow_map_->resolution()) {
            shadow_map_->reinit(new_res);
        }
        if (new_cascades != shadow_map_->active_cascades()) {
            shadow_map_->set_active_cascades(new_cascades);
        }
    }

    default_graphics_ = settings;
    graphics_ = &default_graphics_;
}

void SceneRenderer::set_vsync_mode(int mode) {
    if (context_) {
        context_->set_vsync_mode(mode);
    }
}

int SceneRenderer::max_vsync_mode() const {
    return context_ ? context_->max_vsync_mode() : 1;
}

void SceneRenderer::set_anisotropic_filter(int level) {
    float aniso_value = 1.0f;
    if (level > 0) {
        aniso_value = static_cast<float>(1 << level);
    }
    aniso_value = std::min(aniso_value, 16.0f);

    terrain_->set_anisotropic_filter(aniso_value);

    if (default_sampler_ && context_) {
        context_->device().release_sampler(default_sampler_);

        SDL_GPUSamplerCreateInfo sampler_info = {};
        sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.max_anisotropy = aniso_value;
        sampler_info.enable_anisotropy = (level > 0);
        default_sampler_ = context_->device().create_sampler(sampler_info);

        if (!default_sampler_) {
            std::cerr << "Warning: Failed to recreate default GPU sampler (anisotropic level: "
                      << level << ")" << std::endl;
        }
    }
}

void SceneRenderer::set_heightmap(const Heightmap& heightmap) {
    terrain_->set_heightmap(heightmap);

    if (grass_renderer_ && terrain_->heightmap_texture()) {
        render::HeightmapParams hm_params;
        hm_params.world_origin_x = heightmap.world_origin_x;
        hm_params.world_origin_z = heightmap.world_origin_z;
        hm_params.world_size = heightmap.world_size;
        hm_params.min_height = heightmap.min_height;
        hm_params.max_height = heightmap.max_height;
        grass_renderer_->set_heightmap(terrain_->heightmap_texture(), hm_params);
    }

    std::cout << "[Renderer] Heightmap set for terrain rendering" << std::endl;
}

int SceneRenderer::spawn_effect(const mmo::engine::EffectDefinition* definition,
                                  const glm::vec3& position,
                                  const glm::vec3& direction,
                                  float range) {
    return effect_system_->spawn_effect(definition, position, direction, range);
}

// ============================================================================
// Frame Lifecycle
// ============================================================================

void SceneRenderer::begin_frame() {
    context_->begin_frame();
    had_main_pass_this_frame_ = false;
    depth_prepass_ran_ = false;
    ui_->set_screen_size(context_->width(), context_->height());
}

void SceneRenderer::end_frame() {
    current_swapchain_ = nullptr;
    context_->end_frame();
}

void SceneRenderer::begin_main_pass() {
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) {
        std::cerr << "begin_main_pass: No active command buffer" << std::endl;
        return;
    }

    uint32_t sw_width, sw_height;
    current_swapchain_ = context_->acquire_swapchain_texture(cmd, &sw_width, &sw_height);
    if (!current_swapchain_) {
        std::cerr << "begin_main_pass: Failed to acquire swapchain texture" << std::endl;
        return;
    }

    if (depth_texture_ && (depth_texture_->width() != static_cast<int>(sw_width) ||
                           depth_texture_->height() != static_cast<int>(sw_height))) {
        depth_texture_ = gpu::GPUTexture::create_depth(context_->device(), sw_width, sw_height);
        if (!depth_texture_) {
            std::cerr << "begin_main_pass: Failed to resize depth texture" << std::endl;
            return;
        }
    }

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = current_swapchain_;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { 0.35f, 0.45f, 0.6f, 1.0f };

    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = depth_texture_ ? depth_texture_->handle() : nullptr;
    // If depth pre-pass already wrote depth, preserve it (LOAD) instead of clearing
    depth_target.load_op = depth_prepass_ran_ ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    main_render_pass_ = SDL_BeginGPURenderPass(cmd, &color_target, 1,
                                                depth_texture_ ? &depth_target : nullptr);
    if (!main_render_pass_) {
        std::cerr << "begin_main_pass: Failed to begin render pass" << std::endl;
        return;
    }

    had_main_pass_this_frame_ = true;
}

void SceneRenderer::end_main_pass() {
    if (main_render_pass_) {
        SDL_EndGPURenderPass(main_render_pass_);
        main_render_pass_ = nullptr;
    }
}

void SceneRenderer::begin_ui() {
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) {
        std::cerr << "begin_ui: No active command buffer" << std::endl;
        return;
    }

    if (!current_swapchain_) {
        uint32_t sw_width, sw_height;
        current_swapchain_ = context_->acquire_swapchain_texture(cmd, &sw_width, &sw_height);
        if (!current_swapchain_) {
            return;
        }
    }

    ui_->begin(cmd);
}

void SceneRenderer::end_ui() {
    ui_->end();

    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (cmd && current_swapchain_) {
        bool clear_background = !had_main_pass_this_frame_;
        ui_->execute(cmd, current_swapchain_, clear_background);
    }
}

// ============================================================================
// Main Render Frame
// ============================================================================

void SceneRenderer::render_frame(const RenderScene& scene, const UIScene& ui_scene,
                                  const CameraState& camera, float dt) {
    if (collect_stats_) render_stats_ = {};
    const GraphicsSettings& gfx = graphics_ ? *graphics_ : default_graphics_;

    // Cache frame-level state once (avoids repeated ternary/function calls)
    frame_fog_active_ = gfx.fog_enabled;
    frame_draw_dist_sq_ = gfx.get_draw_distance() * gfx.get_draw_distance();
    frame_do_frustum_cull_ = gfx.frustum_culling;

    // Update particle effect system
    auto get_terrain_height = [this](float x, float z) -> float {
        return terrain_->get_height(x, z);
    };
    effect_system_->update(dt, get_terrain_height);

    begin_frame();

    bool has_content = scene.has_3d_content();
    bool use_ao = gfx.ao_mode > 0 && ao_->is_ready();

    if (has_content) {
        // Build instance batches (cull + group) and upload storage buffers
        // before any render passes begin.
        // Extract frustum once here; reused in render_3d_scene for skinned models.
        frame_frustum_.extract_from_matrix(camera.view_projection);
        build_instance_batches(scene, camera, frame_frustum_);
        upload_instance_buffers();

        if (shadow_map_->is_ready() && gfx.shadow_mode > 0) {
            render_shadow_passes(scene, camera);
        }

        // Cache shadow uniforms AFTER render_shadow_passes updates cascade matrices
        if (shadow_map_->is_ready()) {
            frame_shadow_uniforms_ = shadow_map_->get_shadow_uniforms(gfx.shadow_mode);
        }

        SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();

        if (use_ao) {
            // === GTAO PATH: render to offscreen, then AO passes, then composite ===

            // Acquire swapchain first (needed for composite + UI later)
            uint32_t sw_width, sw_height;
            current_swapchain_ = context_->acquire_swapchain_texture(cmd, &sw_width, &sw_height);

            // Resize GTAO + bloom + volumetric fog textures if window size changed
            if (current_swapchain_) {
                ao_->resize(static_cast<int>(sw_width), static_cast<int>(sw_height));
                bloom_->resize(static_cast<int>(sw_width), static_cast<int>(sw_height));
                volumetric_fog_->resize(static_cast<int>(sw_width), static_cast<int>(sw_height));
            }

            // Render scene to offscreen RT
            main_render_pass_ = ao_->begin_offscreen_pass(cmd);
            had_main_pass_this_frame_ = (main_render_pass_ != nullptr);

            if (main_render_pass_ && cmd) {
                render_3d_scene(scene, camera, dt);
                ao_->end_offscreen_pass();
                main_render_pass_ = nullptr;
            }

            // AO computation + blur + bloom + composite to swapchain
            if (current_swapchain_) {
                glm::mat4 inv_proj = glm::inverse(camera.projection);
                if (gfx.ao_mode == 1) {
                    ao_->render_ssao_pass(cmd, *pipeline_registry_, camera.projection, inv_proj);
                } else {
                    ao_->render_gtao_pass(cmd, *pipeline_registry_, camera.projection, inv_proj);
                }
                ao_->render_blur_pass(cmd, *pipeline_registry_);

                // Bloom: downsample + upsample chain on offscreen color
                SDL_GPUTexture* bloom_tex = nullptr;
                if (gfx.bloom_enabled && bloom_->is_ready()) {
                    bloom_->render(cmd, *pipeline_registry_, ao_->offscreen_color()->handle());
                    bloom_tex = bloom_->bloom_texture();
                }

                // Volumetric fog: ray march through fog volume using depth + shadow map
                SDL_GPUTexture* fog_tex = nullptr;
                if ((gfx.volumetric_fog || gfx.god_rays) && volumetric_fog_->is_ready() && shadow_map_->is_ready() &&
                    ao_->offscreen_depth()) {
                    volumetric_fog_->render(cmd, *pipeline_registry_,
                                           ao_->offscreen_depth()->handle(),
                                           *shadow_map_, camera, light_dir_,
                                           gfx.god_rays, gfx.volumetric_fog);
                    fog_tex = volumetric_fog_->fog_texture();
                }

                ao_->render_composite_pass(cmd, *pipeline_registry_, current_swapchain_,
                                          bloom_tex, gfx.bloom_enabled ? gfx.bloom_strength : 0.0f,
                                          fog_tex);
            }
        } else {
            // === NORMAL PATH: render directly to swapchain ===

            // Depth pre-pass: render depth-only before the main color pass
            // to eliminate overdraw (fragments behind existing depth are rejected early)
            if (gfx.depth_prepass) {
                render_depth_prepass(scene, camera);
            }

            begin_main_pass();

            if (main_render_pass_ && context_->current_command_buffer()) {
                render_3d_scene(scene, camera, dt);
            }

            end_main_pass();
        }
    }

    begin_ui();
    render_ui_commands(ui_scene, camera);
    for (const auto& billboard : scene.billboards()) {
        draw_billboard_3d(billboard, camera);
    }
    end_ui();

    // Post-UI callback (e.g. ImGui render pass)
    if (post_ui_callback_) {
        post_ui_callback_(context_->current_command_buffer(), current_swapchain_);
    }

    end_frame();
}

// ============================================================================
// 3D Scene Rendering (shared between normal and AO paths)
// ============================================================================

void SceneRenderer::render_3d_scene(const RenderScene& scene, const CameraState& camera, float dt) {
    const GraphicsSettings& gfx = graphics_ ? *graphics_ : default_graphics_;
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();

    if (scene.should_draw_skybox() && gfx.skybox_enabled) {
        skybox_time_ += dt;
        world_->update(dt);
        world_->render_skybox(main_render_pass_, cmd, camera.view, camera.projection);
    }

    // Prepare shadow bindings for terrain/grass
    SDL_GPUTextureSamplerBinding shadow_bindings[4] = {};
    int shadow_binding_count = 0;
    if (shadow_map_->is_ready()) {
        for (int i = 0; i < render::CSM_MAX_CASCADES; ++i) {
            shadow_bindings[i].texture = shadow_map_->shadow_texture(i);
            shadow_bindings[i].sampler = shadow_map_->shadow_sampler();
        }
        shadow_binding_count = render::CSM_MAX_CASCADES;
    }

    if (scene.should_draw_ground()) {
        SDL_PushGPUFragmentUniformData(cmd, 1, &frame_shadow_uniforms_, sizeof(frame_shadow_uniforms_));
        terrain_->render(main_render_pass_, cmd, camera.view, camera.projection,
                       camera.position, light_dir_,
                       shadow_binding_count > 0 ? shadow_bindings : nullptr,
                       shadow_binding_count);
    }

    if (scene.should_draw_grass() && gfx.grass_enabled && grass_renderer_) {
        SDL_PushGPUFragmentUniformData(cmd, 1, &frame_shadow_uniforms_, sizeof(frame_shadow_uniforms_));
        grass_renderer_->update(dt, skybox_time_);
        grass_renderer_->render(main_render_pass_, cmd, camera.view, camera.projection,
                                camera.position, light_dir_,
                                shadow_binding_count > 0 ? shadow_bindings : nullptr,
                                shadow_binding_count);
    }

    // Use pre-cached frame state
    const Frustum& frustum = frame_frustum_;
    bool do_frustum_cull = frame_do_frustum_cull_;
    float draw_dist_sq = frame_draw_dist_sq_;

    // Render instanced static models (rocks, trees, etc.) - uses same frustum
    render_instanced_models(camera);

    // Render non-instanced static models (attack animations, no-fog, etc.)
    if (!non_instanced_commands_.empty()) {
        gpu::GPUPipeline* model_pipeline = pipeline_registry_->get_model_pipeline();
        if (model_pipeline && main_render_pass_) {
            model_pipeline->bind(main_render_pass_);
            bind_shadow_data(main_render_pass_, context_->current_command_buffer(), 1);
            for (const auto* cmd : non_instanced_commands_) {
                render_model_command_inner(*cmd, camera);
            }
        }
    }

    // Render skinned models individually (they have per-instance bone data)
    // Bind pipeline and shadow data once before the loop
    {
        bool skinned_pipeline_bound = false;
        SDL_GPUTexture* last_skinned_texture = nullptr;
        int last_skinned_has_texture = -1;

        for (const auto& data : scene.skinned_commands()) {
            const glm::mat4& t = data.transform;
            glm::vec3 world_pos(t[3][0], t[3][1], t[3][2]);

            float dx = world_pos.x - camera.position.x;
            float dz = world_pos.z - camera.position.z;
            if (dx * dx + dz * dz > draw_dist_sq) {
                if (collect_stats_) render_stats_.entities_distance_culled++;
                continue;
            }

            if (do_frustum_cull) {
                Model* model = resolve_model(frame_model_cache_, frame_model_name_cache_, *model_manager_, data.model_handle, data.model_name);
                if (model) {
                    glm::vec3 world_center = glm::vec3(t * glm::vec4(model->bounding_center, 1.0f));
                    float max_scale = std::max({
                        glm::length(glm::vec3(t[0])),
                        glm::length(glm::vec3(t[1])),
                        glm::length(glm::vec3(t[2]))
                    });
                    if (!frustum.intersects_sphere(world_center, model->bounding_half_diag * max_scale)) {
                        if (collect_stats_) render_stats_.entities_frustum_culled++;
                        continue;
                    }
                }
            }

            if (collect_stats_) render_stats_.entities_rendered++;

            // Bind pipeline + shadow data once for all skinned models
            if (!skinned_pipeline_bound) {
                gpu::GPUPipeline* skinned_pipeline = pipeline_registry_->get_skinned_model_pipeline();
                if (!skinned_pipeline) break;
                skinned_pipeline->bind(main_render_pass_);
                bind_shadow_data(main_render_pass_, context_->current_command_buffer(), 1);
                skinned_pipeline_bound = true;
            }

            render_skinned_model_command_inner(data, camera, last_skinned_texture, last_skinned_has_texture);
        }
    }

    // Process particle effect spawn commands from the scene
    auto& spawns = scene.particle_effect_spawns();
    for (const auto& spawn_cmd : spawns) {
        if (spawn_cmd.definition) {
            effect_system_->spawn_effect(spawn_cmd.definition, spawn_cmd.position,
                                       spawn_cmd.direction, spawn_cmd.range);
        }
    }

    // Clear the spawn commands now that we've consumed them
    // Note: Using const_cast because we need to clear after reading
    const_cast<RenderScene&>(scene).clear_particle_effect_spawns();

    // Render new particle-based effects
    effects_->draw_particle_effects(
        main_render_pass_,
        context_->current_command_buffer(),
        *effect_system_,
        camera.view,
        camera.projection,
        camera.position
    );

    // Debug line rendering (only if game submitted any)
    render_debug_lines(scene, camera);
}

// ============================================================================
// Model Rendering
// ============================================================================

void SceneRenderer::render_model_command(const ModelCommand& cmd, const CameraState& camera) {
    // Standalone path: binds pipeline + shadows itself
    if (!main_render_pass_) return;
    gpu::GPUPipeline* pipeline = pipeline_registry_->get_model_pipeline();
    if (!pipeline) return;
    pipeline->bind(main_render_pass_);
    bind_shadow_data(main_render_pass_, context_->current_command_buffer(), 1);
    render_model_command_inner(cmd, camera);
}

void SceneRenderer::render_model_command_inner(const ModelCommand& cmd, const CameraState& camera) {
    Model* model = resolve_model(frame_model_cache_, frame_model_name_cache_, *model_manager_, cmd.model_handle, cmd.model_name);
    if (!model || !main_render_pass_) return;

    SDL_GPUCommandBuffer* gpu_cmd = context_->current_command_buffer();
    if (!gpu_cmd) return;

    const glm::mat4& model_mat = cmd.transform;

    gpu::ModelTransformUniforms transform_uniforms = {};
    transform_uniforms.model = model_mat;
    transform_uniforms.view = camera.view;
    transform_uniforms.projection = camera.projection;
    transform_uniforms.cameraPos = camera.position;
    transform_uniforms.normalMatrix = glm::transpose(glm::inverse(model_mat));
    transform_uniforms.useSkinning = 0;

    bool fog_active = !cmd.no_fog && frame_fog_active_;

    gpu::ModelLightingUniforms lighting_uniforms = {};
    lighting_uniforms.lightDir = light_dir_;
    lighting_uniforms.lightColor = lighting::LIGHT_COLOR;
    lighting_uniforms.ambientColor = fog_active ? lighting::AMBIENT_COLOR : lighting::AMBIENT_COLOR_NO_FOG;
    lighting_uniforms.tintColor = cmd.tint;
    lighting_uniforms.fogColor = fog_active ? fog::COLOR : fog::DISTANT_COLOR;
    lighting_uniforms.fogStart = fog_active ? fog::START : fog::DISTANT_START;
    lighting_uniforms.fogEnd = fog_active ? fog::END : fog::DISTANT_END;
    lighting_uniforms.fogEnabled = fog_active ? 1 : 0;
    lighting_uniforms.cameraPos = camera.position;

    SDL_PushGPUVertexUniformData(gpu_cmd, 0, &transform_uniforms, sizeof(transform_uniforms));

    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded) {
            ModelLoader::upload_to_gpu(context_->device(), *model);
        }

        if (!mesh.vertex_buffer || !mesh.index_buffer) continue;
        if (mesh.index_count() == 0) continue;

        lighting_uniforms.hasTexture = (mesh.has_texture && mesh.texture) ? 1 : 0;
        SDL_PushGPUFragmentUniformData(gpu_cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));

        if (mesh.has_texture && mesh.texture && default_sampler_) {
            SDL_GPUTextureSamplerBinding tex_binding = { mesh.texture->handle(), default_sampler_ };
            SDL_BindGPUFragmentSamplers(main_render_pass_, 0, &tex_binding, 1);
        }

        mesh.bind_buffers(main_render_pass_);
        if (collect_stats_) { render_stats_.draw_calls++; render_stats_.triangle_count += mesh.index_count() / 3; }
        SDL_DrawGPUIndexedPrimitives(main_render_pass_,
                                      mesh.index_count(),
                                      1, 0, 0, 0);
    }
}

// ============================================================================
// Instanced Rendering
// ============================================================================

void SceneRenderer::build_instance_batches(const RenderScene& scene, const CameraState& camera, const Frustum& frustum) {
    // Clear vectors but preserve map keys + vector capacity to avoid re-hashing
    // and re-allocating every frame.
    for (auto& [handle, vec] : instance_batches_) vec.clear();
    for (auto& [handle, vec] : shadow_instance_batches_) vec.clear();
    non_instanced_commands_.clear();

    // Model pointer cache is persistent - models are never unloaded during gameplay

    // Use pre-cached frame state
    bool do_frustum_cull = frame_do_frustum_cull_;
    float draw_dist_sq = frame_draw_dist_sq_;

    for (const auto& cmd : scene.model_commands()) {
        const glm::mat4& t = cmd.transform;
        glm::vec3 world_pos(t[3][0], t[3][1], t[3][2]);

        // Distance culling
        float dx = world_pos.x - camera.position.x;
        float dz = world_pos.z - camera.position.z;
        if (dx * dx + dz * dz > draw_dist_sq) {
            if (collect_stats_) render_stats_.entities_distance_culled++;
            continue;
        }

        // Resolve handle for this command (O(1) if handle set, hash lookup fallback)
        ModelHandle h = resolve_handle(*model_manager_, cmd.model_handle, cmd.model_name);

        // Frustum culling using pre-computed bounding sphere
        if (do_frustum_cull) {
            Model* model = get_model_cached(frame_model_cache_, *model_manager_, h);
            if (model) {
                glm::vec3 world_center = glm::vec3(t * glm::vec4(model->bounding_center, 1.0f));
                float max_scale = std::max({
                    glm::length(glm::vec3(t[0])),
                    glm::length(glm::vec3(t[1])),
                    glm::length(glm::vec3(t[2]))
                });
                if (!frustum.intersects_sphere(world_center, model->bounding_half_diag * max_scale)) {
                    if (collect_stats_) render_stats_.entities_frustum_culled++;
                    continue;
                }
            }
        }

        if (collect_stats_) render_stats_.entities_rendered++;

        // Models flagged for individual draws (e.g. per-instance transform variation).
        if (cmd.force_non_instanced) {
            non_instanced_commands_.push_back(&cmd);
            continue;
        }

        gpu::InstanceData inst;
        inst.model = cmd.transform;
        inst.normalMatrix = glm::transpose(glm::inverse(cmd.transform));
        inst.tint = cmd.tint;
        inst.noFog = cmd.no_fog ? 1.0f : 0.0f;
        instance_batches_[h].push_back(inst);

        gpu::ShadowInstanceData shadow_inst;
        shadow_inst.model = cmd.transform;
        shadow_instance_batches_[h].push_back(shadow_inst);
    }
}

void SceneRenderer::upload_instance_buffers() {
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) return;

    // Upload main instance buffer
    size_t total_instances = 0;
    for (const auto& [name, instances] : instance_batches_) {
        total_instances += instances.size();
    }

    if (total_instances > 0) {
        size_t required_size = total_instances * sizeof(gpu::InstanceData);

        if (!instance_storage_buffer_ || instance_storage_capacity_ < required_size) {
            instance_storage_capacity_ = required_size * 2;  // over-allocate
            instance_storage_buffer_ = gpu::GPUBuffer::create_dynamic(
                context_->device(), gpu::GPUBuffer::Type::Storage, instance_storage_capacity_);
        }

        // Pack all batches contiguously into reusable buffer
        packed_instances_.clear();
        packed_instances_.reserve(total_instances);
        for (const auto& [name, instances] : instance_batches_) {
            packed_instances_.insert(packed_instances_.end(), instances.begin(), instances.end());
        }

        instance_storage_buffer_->update(cmd, packed_instances_.data(), packed_instances_.size() * sizeof(gpu::InstanceData));
    }

    // Upload shadow instance buffer
    size_t total_shadow = 0;
    for (const auto& [name, instances] : shadow_instance_batches_) {
        total_shadow += instances.size();
    }

    if (total_shadow > 0) {
        size_t required_size = total_shadow * sizeof(gpu::ShadowInstanceData);

        if (!shadow_instance_storage_buffer_ || shadow_instance_storage_capacity_ < required_size) {
            shadow_instance_storage_capacity_ = required_size * 2;
            shadow_instance_storage_buffer_ = gpu::GPUBuffer::create_dynamic(
                context_->device(), gpu::GPUBuffer::Type::Storage, shadow_instance_storage_capacity_);
        }

        packed_shadow_instances_.clear();
        packed_shadow_instances_.reserve(total_shadow);
        for (const auto& [name, instances] : shadow_instance_batches_) {
            packed_shadow_instances_.insert(packed_shadow_instances_.end(), instances.begin(), instances.end());
        }

        shadow_instance_storage_buffer_->update(cmd, packed_shadow_instances_.data(), packed_shadow_instances_.size() * sizeof(gpu::ShadowInstanceData));
    }
}

void SceneRenderer::render_instanced_models(const CameraState& camera) {
    if (instance_batches_.empty() || !main_render_pass_) return;

    SDL_GPUCommandBuffer* gpu_cmd = context_->current_command_buffer();
    if (!gpu_cmd) return;

    gpu::GPUPipeline* pipeline = pipeline_registry_->get_instanced_model_pipeline();
    if (!pipeline || !instance_storage_buffer_) return;

    bool fog_active = frame_fog_active_;

    // Push shared camera uniforms (once)
    gpu::InstancedCameraUniforms camera_uniforms = {};
    camera_uniforms.view = camera.view;
    camera_uniforms.projection = camera.projection;
    camera_uniforms.cameraPos = camera.position;

    // Push shared lighting uniforms
    gpu::InstancedLightingUniforms lighting_uniforms = {};
    lighting_uniforms.lightDir = light_dir_;
    lighting_uniforms.lightColor = lighting::LIGHT_COLOR;
    lighting_uniforms.ambientColor = fog_active ? lighting::AMBIENT_COLOR : lighting::AMBIENT_COLOR_NO_FOG;
    lighting_uniforms.fogColor = fog_active ? fog::COLOR : fog::DISTANT_COLOR;
    lighting_uniforms.fogStart = fog_active ? fog::START : fog::DISTANT_START;
    lighting_uniforms.fogEnd = fog_active ? fog::END : fog::DISTANT_END;
    lighting_uniforms.fogEnabled = fog_active ? 1 : 0;
    lighting_uniforms.cameraPos = camera.position;

    pipeline->bind(main_render_pass_);
    SDL_PushGPUVertexUniformData(gpu_cmd, 0, &camera_uniforms, sizeof(camera_uniforms));

    // Bind instance storage buffer
    SDL_GPUBuffer* storage_buf = instance_storage_buffer_->handle();
    SDL_BindGPUVertexStorageBuffers(main_render_pass_, 0, &storage_buf, 1);

    // Bind shadow map data
    bind_shadow_data(main_render_pass_, gpu_cmd, 1);

    // Push lighting uniforms once (only hasTexture varies per mesh)
    SDL_PushGPUFragmentUniformData(gpu_cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));

    // Track last state to avoid redundant GPU calls
    int last_has_texture = lighting_uniforms.hasTexture;
    SDL_GPUTexture* last_bound_texture = nullptr;

    // Draw each model type as a single instanced draw call per mesh
    uint32_t base_instance = 0;
    for (const auto& [batch_handle, instances] : instance_batches_) {
        if (instances.empty()) continue;

        Model* model = get_model_cached(frame_model_cache_, *model_manager_, batch_handle);
        if (!model) {
            base_instance += static_cast<uint32_t>(instances.size());
            continue;
        }

        uint32_t instance_count = static_cast<uint32_t>(instances.size());

        for (auto& mesh : model->meshes) {
            if (!mesh.uploaded) {
                ModelLoader::upload_to_gpu(context_->device(), *model);
            }
            if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) continue;

            int has_tex = (mesh.has_texture && mesh.texture) ? 1 : 0;
            if (has_tex != last_has_texture) {
                lighting_uniforms.hasTexture = has_tex;
                SDL_PushGPUFragmentUniformData(gpu_cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));
                last_has_texture = has_tex;
            }

            if (mesh.has_texture && mesh.texture && default_sampler_) {
                SDL_GPUTexture* tex_handle = mesh.texture->handle();
                if (tex_handle != last_bound_texture) {
                    SDL_GPUTextureSamplerBinding tex_binding = { tex_handle, default_sampler_ };
                    SDL_BindGPUFragmentSamplers(main_render_pass_, 0, &tex_binding, 1);
                    last_bound_texture = tex_handle;
                }
            }

            mesh.bind_buffers(main_render_pass_);
            if (collect_stats_) {
                render_stats_.draw_calls++;
                render_stats_.triangle_count += mesh.index_count() / 3 * instance_count;
            }
            SDL_DrawGPUIndexedPrimitives(main_render_pass_,
                                          mesh.index_count(),
                                          instance_count, 0, 0, base_instance);
        }

        base_instance += instance_count;
    }
}

void SceneRenderer::render_instanced_shadow_models(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                     const glm::mat4& light_view_projection) {
    if (shadow_instance_batches_.empty() || !pass || !shadow_instance_storage_buffer_) return;

    auto* pipeline = pipeline_registry_->get_instanced_shadow_model_pipeline();
    if (!pipeline) return;

    pipeline->bind(pass);

    gpu::InstancedShadowUniforms shadow_uniforms = {};
    shadow_uniforms.lightViewProjection = light_view_projection;
    SDL_PushGPUVertexUniformData(cmd, 0, &shadow_uniforms, sizeof(shadow_uniforms));

    SDL_GPUBuffer* storage_buf = shadow_instance_storage_buffer_->handle();
    SDL_BindGPUVertexStorageBuffers(pass, 0, &storage_buf, 1);

    uint32_t base_instance = 0;
    for (const auto& [batch_handle, instances] : shadow_instance_batches_) {
        if (instances.empty()) continue;

        Model* model = get_model_cached(frame_model_cache_, *model_manager_, batch_handle);
        if (!model) {
            base_instance += static_cast<uint32_t>(instances.size());
            continue;
        }

        uint32_t instance_count = static_cast<uint32_t>(instances.size());

        for (auto& mesh : model->meshes) {
            if (!mesh.uploaded) ModelLoader::upload_to_gpu(context_->device(), *model);
            if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) continue;

            mesh.bind_buffers(pass);
            if (collect_stats_) {
                render_stats_.draw_calls++;
                render_stats_.triangle_count += mesh.index_count() / 3 * instance_count;
            }
            SDL_DrawGPUIndexedPrimitives(pass,
                                          mesh.index_count(),
                                          instance_count, 0, 0, base_instance);
        }

        base_instance += instance_count;
    }
}

void SceneRenderer::render_skinned_model_command(const SkinnedModelCommand& cmd, const CameraState& camera) {
    // Standalone path (used by non-batched callers): binds pipeline + shadows itself
    if (!main_render_pass_) return;
    gpu::GPUPipeline* pipeline = pipeline_registry_->get_skinned_model_pipeline();
    if (!pipeline) return;
    pipeline->bind(main_render_pass_);
    bind_shadow_data(main_render_pass_, context_->current_command_buffer(), 1);
    SDL_GPUTexture* unused_tex = nullptr;
    int unused_has = -1;
    render_skinned_model_command_inner(cmd, camera, unused_tex, unused_has);
}

void SceneRenderer::render_skinned_model_command_inner(const SkinnedModelCommand& cmd, const CameraState& camera,
                                                        SDL_GPUTexture*& last_texture, int& last_has_texture) {
    Model* model = resolve_model(frame_model_cache_, frame_model_name_cache_, *model_manager_, cmd.model_handle, cmd.model_name);
    if (!model || !main_render_pass_) return;

    SDL_GPUCommandBuffer* gpu_cmd = context_->current_command_buffer();
    if (!gpu_cmd) return;

    const glm::mat4& model_mat = cmd.transform;

    gpu::ModelTransformUniforms transform_uniforms = {};
    transform_uniforms.model = model_mat;
    transform_uniforms.view = camera.view;
    transform_uniforms.projection = camera.projection;
    transform_uniforms.cameraPos = camera.position;
    transform_uniforms.normalMatrix = glm::transpose(glm::inverse(model_mat));
    transform_uniforms.useSkinning = 1;

    bool fog_active = frame_fog_active_;

    gpu::ModelLightingUniforms lighting_uniforms = {};
    lighting_uniforms.lightDir = light_dir_;
    lighting_uniforms.lightColor = lighting::LIGHT_COLOR;
    lighting_uniforms.ambientColor = fog_active ? lighting::AMBIENT_COLOR : lighting::AMBIENT_COLOR_NO_FOG;
    lighting_uniforms.tintColor = cmd.tint;
    lighting_uniforms.fogColor = fog_active ? fog::COLOR : fog::DISTANT_COLOR;
    lighting_uniforms.fogStart = fog_active ? fog::START : fog::DISTANT_START;
    lighting_uniforms.fogEnd = fog_active ? fog::END : fog::DISTANT_END;
    lighting_uniforms.fogEnabled = fog_active ? 1 : 0;
    lighting_uniforms.cameraPos = camera.position;

    SDL_PushGPUVertexUniformData(gpu_cmd, 0, &transform_uniforms, sizeof(transform_uniforms));
    SDL_PushGPUVertexUniformData(gpu_cmd, 1, cmd.bone_matrices->data(),
                                  MAX_BONES * sizeof(glm::mat4));

    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded) {
            ModelLoader::upload_to_gpu(context_->device(), *model);
        }

        if (!mesh.vertex_buffer || !mesh.index_buffer) continue;
        if (mesh.index_count() == 0) continue;

        int has_tex = (mesh.has_texture && mesh.texture) ? 1 : 0;
        if (has_tex != last_has_texture) {
            lighting_uniforms.hasTexture = has_tex;
            SDL_PushGPUFragmentUniformData(gpu_cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));
            last_has_texture = has_tex;
        }

        if (mesh.has_texture && mesh.texture && default_sampler_) {
            SDL_GPUTexture* tex_handle = mesh.texture->handle();
            if (tex_handle != last_texture) {
                SDL_GPUTextureSamplerBinding tex_binding = { tex_handle, default_sampler_ };
                SDL_BindGPUFragmentSamplers(main_render_pass_, 0, &tex_binding, 1);
                last_texture = tex_handle;
            }
        }

        mesh.bind_buffers(main_render_pass_);
        if (collect_stats_) { render_stats_.draw_calls++; render_stats_.triangle_count += mesh.index_count() / 3; }
        SDL_DrawGPUIndexedPrimitives(main_render_pass_,
                                      mesh.index_count(),
                                      1, 0, 0, 0);
    }
}

// ============================================================================
// UI Rendering
// ============================================================================

void SceneRenderer::render_ui_commands(const UIScene& ui_scene, const CameraState& camera) {
    for (const auto& cmd : ui_scene.commands()) {
        std::visit([this, &camera](const auto& data) {
            using T = std::decay_t<decltype(data)>;

            if constexpr (std::is_same_v<T, FilledRectCommand>) {
                ui_->draw_filled_rect(data.x, data.y, data.w, data.h, data.color);
            }
            else if constexpr (std::is_same_v<T, RectOutlineCommand>) {
                ui_->draw_rect_outline(data.x, data.y, data.w, data.h, data.color, data.line_width);
            }
            else if constexpr (std::is_same_v<T, CircleCommand>) {
                ui_->draw_circle(data.x, data.y, data.radius, data.color, data.segments);
            }
            else if constexpr (std::is_same_v<T, CircleOutlineCommand>) {
                ui_->draw_circle_outline(data.x, data.y, data.radius, data.color,
                                       data.line_width, data.segments);
            }
            else if constexpr (std::is_same_v<T, LineCommand>) {
                ui_->draw_line(data.x1, data.y1, data.x2, data.y2, data.color, data.line_width);
            }
            else if constexpr (std::is_same_v<T, TextCommand>) {
                ui_->draw_text(data.text, data.x, data.y, data.color, data.scale);
            }
            else if constexpr (std::is_same_v<T, ButtonCommand>) {
                ui_->draw_button(data.x, data.y, data.w, data.h, data.label, data.color, data.selected);
            }
        }, cmd.data);
    }
}

void SceneRenderer::draw_billboard_3d(const Billboard3DCommand& cmd,
                                       const CameraState& camera) {
    glm::vec4 world_pos(cmd.world_x, cmd.world_y, cmd.world_z, 1.0f);
    glm::vec4 clip_pos = camera.projection * camera.view * world_pos;
    if (clip_pos.w <= 0.01f) return;

    glm::vec3 ndc = glm::vec3(clip_pos) / clip_pos.w;
    if (ndc.x < -1.5f || ndc.x > 1.5f || ndc.y < -1.5f || ndc.y > 1.5f || ndc.z < -1.0f || ndc.z > 1.0f) {
        return;
    }

    float screen_x = (ndc.x * 0.5f + 0.5f) * context_->width();
    float screen_y = (1.0f - (ndc.y * 0.5f + 0.5f)) * context_->height();

    float distance_scale = 100.0f / clip_pos.w;
    distance_scale = glm::clamp(distance_scale, 0.3f, 1.5f);

    float bar_w = cmd.width * 2.0f * distance_scale;
    float bar_h = cmd.width * 0.4f * distance_scale;

    float x = screen_x - bar_w * 0.5f;
    float y = screen_y - bar_h * 0.5f;

    ui_->draw_filled_rect(x - 1, y - 1, bar_w + 2, bar_h + 2, cmd.frame_color);
    ui_->draw_filled_rect(x, y, bar_w, bar_h, cmd.bg_color);
    float fill_w = bar_w * cmd.fill_ratio;
    ui_->draw_filled_rect(x, y, fill_w, bar_h, cmd.fill_color);
}

// ============================================================================
// Debug Line Rendering
// ============================================================================

void SceneRenderer::render_debug_lines(const RenderScene& scene, const CameraState& camera) {
    const auto& lines = scene.debug_lines();
    if (lines.empty() || !main_render_pass_) return;

    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) return;

    gpu::GPUPipeline* pipeline = pipeline_registry_->get_grid_pipeline();
    if (!pipeline) return;

    const size_t line_count = lines.size();
    const size_t vertex_count = line_count * 2;
    // Grid vertex format: float3 position + float4 color = 7 floats per vertex
    const size_t vertex_stride = sizeof(float) * 7;
    const size_t buffer_size = vertex_count * vertex_stride;

    // Recreate buffer if too small
    if (!debug_line_vertex_buffer_ || debug_line_buffer_capacity_ < line_count) {
        // Round up to avoid frequent reallocations
        size_t new_capacity = std::max(line_count, size_t(256));
        if (debug_line_buffer_capacity_ > 0) {
            new_capacity = std::max(new_capacity, debug_line_buffer_capacity_ * 2);
        }
        debug_line_vertex_buffer_ = gpu::GPUBuffer::create_dynamic(
            context_->device(),
            gpu::GPUBuffer::Type::Vertex,
            new_capacity * 2 * vertex_stride
        );
        if (!debug_line_vertex_buffer_) return;
        debug_line_buffer_capacity_ = new_capacity;
    }

    // Build vertex data: position(3) + color(4) per vertex
    // Unpack RGBA packed uint32 to float4
    std::vector<float> vertices(vertex_count * 7);
    float* dst = vertices.data();
    for (const auto& line : lines) {
        float r = static_cast<float>((line.color >> 24) & 0xFF) / 255.0f;
        float g = static_cast<float>((line.color >> 16) & 0xFF) / 255.0f;
        float b = static_cast<float>((line.color >> 8) & 0xFF) / 255.0f;
        float a = static_cast<float>((line.color >> 0) & 0xFF) / 255.0f;

        // Start vertex
        *dst++ = line.start.x; *dst++ = line.start.y; *dst++ = line.start.z;
        *dst++ = r; *dst++ = g; *dst++ = b; *dst++ = a;

        // End vertex
        *dst++ = line.end.x; *dst++ = line.end.y; *dst++ = line.end.z;
        *dst++ = r; *dst++ = g; *dst++ = b; *dst++ = a;
    }

    // Upload vertex data
    debug_line_vertex_buffer_->update(cmd, vertices.data(), buffer_size);

    // Bind pipeline and draw
    pipeline->bind(main_render_pass_);

    // Push view-projection matrix as vertex uniform (same as grid rendering)
    glm::mat4 vp = camera.projection * camera.view;
    SDL_PushGPUVertexUniformData(cmd, 0, &vp, sizeof(vp));

    // Bind vertex buffer
    SDL_GPUBufferBinding binding = {};
    binding.buffer = debug_line_vertex_buffer_->handle();
    binding.offset = 0;
    SDL_BindGPUVertexBuffers(main_render_pass_, 0, &binding, 1);

    // Draw all lines
    SDL_DrawGPUPrimitives(main_render_pass_, static_cast<uint32_t>(vertex_count), 1, 0, 0);

    if (collect_stats_) {
        render_stats_.draw_calls++;
    }
}

// ============================================================================
// Depth Pre-Pass
// ============================================================================

void SceneRenderer::render_depth_prepass(const RenderScene& scene, const CameraState& camera) {
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd || !depth_texture_) return;

    // Ensure depth texture matches current window size
    int w = context_->width();
    int h = context_->height();
    if (depth_texture_->width() != w || depth_texture_->height() != h) {
        depth_texture_ = gpu::GPUTexture::create_depth(context_->device(), w, h);
        if (!depth_texture_) return;
    }

    // Begin a depth-only render pass (no color attachment)
    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = depth_texture_->handle();
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth_target);
    if (!pass) return;

    const glm::mat4 vp = camera.projection * camera.view;

    // --- Instanced models ---
    if (!shadow_instance_batches_.empty() && shadow_instance_storage_buffer_) {
        auto* pipeline = pipeline_registry_->get_depth_prepass_instanced_pipeline();
        if (pipeline) {
            pipeline->bind(pass);

            gpu::InstancedShadowUniforms uniforms = {};
            uniforms.lightViewProjection = vp;  // reusing struct, but it's camera VP
            SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));

            SDL_GPUBuffer* storage_buf = shadow_instance_storage_buffer_->handle();
            SDL_BindGPUVertexStorageBuffers(pass, 0, &storage_buf, 1);

            uint32_t base_instance = 0;
            for (const auto& [batch_handle, instances] : shadow_instance_batches_) {
                if (instances.empty()) continue;

                Model* model = get_model_cached(frame_model_cache_, *model_manager_, batch_handle);
                if (!model) {
                    base_instance += static_cast<uint32_t>(instances.size());
                    continue;
                }

                uint32_t instance_count = static_cast<uint32_t>(instances.size());

                for (auto& mesh : model->meshes) {
                    if (!mesh.uploaded) ModelLoader::upload_to_gpu(context_->device(), *model);
                    if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) continue;

                    mesh.bind_buffers(pass);
                    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(),
                                                  instance_count, 0, 0, base_instance);
                }

                base_instance += instance_count;
            }
        }
    }

    // --- Non-instanced static models ---
    if (!non_instanced_commands_.empty()) {
        auto* pipeline = pipeline_registry_->get_depth_prepass_pipeline();
        if (pipeline) {
            pipeline->bind(pass);

            for (const auto* model_cmd : non_instanced_commands_) {
                Model* model = resolve_model(frame_model_cache_, frame_model_name_cache_, *model_manager_, model_cmd->model_handle, model_cmd->model_name);
                if (!model) continue;

                gpu::ShadowTransformUniforms uniforms = {};
                uniforms.lightViewProjection = vp;  // camera VP
                uniforms.model = model_cmd->transform;
                SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));

                for (auto& mesh : model->meshes) {
                    if (!mesh.uploaded) ModelLoader::upload_to_gpu(context_->device(), *model);
                    if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) continue;

                    mesh.bind_buffers(pass);
                    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), 1, 0, 0, 0);
                }
            }
        }
    }

    // --- Skinned models ---
    {
        auto* pipeline = pipeline_registry_->get_depth_prepass_skinned_pipeline();
        if (pipeline) {
            bool bound = false;
            for (const auto& skinned_cmd : scene.skinned_commands()) {
                // Distance + frustum culling already done in build_instance_batches for static,
                // but skinned models are checked inline in render_3d_scene. Re-check here.
                const glm::mat4& t = skinned_cmd.transform;
                glm::vec3 world_pos(t[3][0], t[3][1], t[3][2]);
                float dx = world_pos.x - camera.position.x;
                float dz = world_pos.z - camera.position.z;
                if (dx * dx + dz * dz > frame_draw_dist_sq_) continue;

                if (frame_do_frustum_cull_) {
                    Model* model = resolve_model(frame_model_cache_, frame_model_name_cache_, *model_manager_, skinned_cmd.model_handle, skinned_cmd.model_name);
                    if (model) {
                        glm::vec3 world_center = glm::vec3(t * glm::vec4(model->bounding_center, 1.0f));
                        float max_scale = std::max({
                            glm::length(glm::vec3(t[0])),
                            glm::length(glm::vec3(t[1])),
                            glm::length(glm::vec3(t[2]))
                        });
                        if (!frame_frustum_.intersects_sphere(world_center, model->bounding_half_diag * max_scale)) {
                            continue;
                        }
                    }
                }

                if (!bound) {
                    pipeline->bind(pass);
                    bound = true;
                }

                Model* model = resolve_model(frame_model_cache_, frame_model_name_cache_, *model_manager_, skinned_cmd.model_handle, skinned_cmd.model_name);
                if (!model) continue;

                gpu::ShadowTransformUniforms uniforms = {};
                uniforms.lightViewProjection = vp;  // camera VP
                uniforms.model = skinned_cmd.transform;
                SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
                SDL_PushGPUVertexUniformData(cmd, 1, skinned_cmd.bone_matrices->data(),
                                              MAX_BONES * sizeof(glm::mat4));

                for (auto& mesh : model->meshes) {
                    if (!mesh.uploaded) ModelLoader::upload_to_gpu(context_->device(), *model);
                    if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) continue;

                    mesh.bind_buffers(pass);
                    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), 1, 0, 0, 0);
                }
            }
        }
    }

    // --- Terrain ---
    if (scene.should_draw_ground()) {
        terrain_->render_depth_prepass(pass, cmd, vp);
    }

    SDL_EndGPURenderPass(pass);
    depth_prepass_ran_ = true;
}

// ============================================================================
// Shadow Rendering
// ============================================================================

void SceneRenderer::render_shadow_passes(const RenderScene& scene, const CameraState& camera) {
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) return;

    // Update cascade matrices
    shadow_map_->update(camera.view, camera.projection, light_dir_, 5.0f, 2000.0f);

    // Pre-collect skinned model commands for shadow rendering
    shadow_skinned_commands_.clear();
    for (const auto& skinned_cmd : scene.skinned_commands()) {
        shadow_skinned_commands_.push_back(&skinned_cmd);
    }

    for (int cascade = 0; cascade < shadow_map_->active_cascades(); ++cascade) {
        SDL_GPURenderPass* shadow_pass = shadow_map_->begin_shadow_pass(cmd, cascade);
        if (!shadow_pass) continue;

        const auto& cascade_data = shadow_map_->cascades()[cascade];

        // Render static models into shadow map (instanced)
        render_instanced_shadow_models(shadow_pass, cmd, cascade_data.light_view_projection);

        // Render non-instanced static models into shadow map
        if (!non_instanced_commands_.empty()) {
            auto* shadow_pipeline = pipeline_registry_->get_shadow_model_pipeline();
            if (shadow_pipeline) {
                shadow_pipeline->bind(shadow_pass);
                for (const auto* model_cmd : non_instanced_commands_) {
                    Model* model = resolve_model(frame_model_cache_, frame_model_name_cache_, *model_manager_, model_cmd->model_handle, model_cmd->model_name);
                    if (!model) continue;

                    gpu::ShadowTransformUniforms shadow_uniforms = {};
                    shadow_uniforms.lightViewProjection = cascade_data.light_view_projection;
                    shadow_uniforms.model = model_cmd->transform;
                    SDL_PushGPUVertexUniformData(cmd, 0, &shadow_uniforms, sizeof(shadow_uniforms));

                    for (auto& mesh : model->meshes) {
                        if (!mesh.uploaded) ModelLoader::upload_to_gpu(context_->device(), *model);
                        if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) continue;
                        mesh.bind_buffers(shadow_pass);
                        if (collect_stats_) { render_stats_.draw_calls++; render_stats_.triangle_count += mesh.index_count() / 3; }
                        SDL_DrawGPUIndexedPrimitives(shadow_pass,
                                                      mesh.index_count(),
                                                      1, 0, 0, 0);
                    }
                }
            }
        }

        // Render skinned models into shadow map
        if (!shadow_skinned_commands_.empty()) {
            auto* shadow_skinned_pipeline = pipeline_registry_->get_shadow_skinned_model_pipeline();
            if (shadow_skinned_pipeline) {
                shadow_skinned_pipeline->bind(shadow_pass);

                for (const auto* data : shadow_skinned_commands_) {
                    Model* model = resolve_model(frame_model_cache_, frame_model_name_cache_, *model_manager_, data->model_handle, data->model_name);
                    if (!model) continue;

                    gpu::ShadowTransformUniforms shadow_uniforms = {};
                    shadow_uniforms.lightViewProjection = cascade_data.light_view_projection;
                    shadow_uniforms.model = data->transform;
                    SDL_PushGPUVertexUniformData(cmd, 0, &shadow_uniforms, sizeof(shadow_uniforms));
                    SDL_PushGPUVertexUniformData(cmd, 1, data->bone_matrices->data(),
                                                  MAX_BONES * sizeof(glm::mat4));

                    for (auto& mesh : model->meshes) {
                        if (!mesh.uploaded) ModelLoader::upload_to_gpu(context_->device(), *model);
                        if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) continue;

                        mesh.bind_buffers(shadow_pass);
                        if (collect_stats_) { render_stats_.draw_calls++; render_stats_.triangle_count += mesh.index_count() / 3; }
                        SDL_DrawGPUIndexedPrimitives(shadow_pass,
                                                      mesh.index_count(),
                                                      1, 0, 0, 0);
                    }
                }
            }
        }

        // Render terrain into shadow map
        terrain_->render_shadow(shadow_pass, cmd, cascade_data.light_view_projection);

        shadow_map_->end_shadow_pass();
    }
}


void SceneRenderer::bind_shadow_data(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, int sampler_slot) {
    if (!shadow_map_->is_ready()) return;

    // Bind 4 individual cascade shadow textures starting at sampler_slot
    SDL_GPUTextureSamplerBinding shadow_bindings[4];
    for (int i = 0; i < render::CSM_MAX_CASCADES; ++i) {
        shadow_bindings[i].texture = shadow_map_->shadow_texture(i);
        shadow_bindings[i].sampler = shadow_map_->shadow_sampler();
    }
    SDL_BindGPUFragmentSamplers(pass, sampler_slot, shadow_bindings, render::CSM_MAX_CASCADES);

    // Use frame-cached shadow uniforms (computed once in render_frame)
    SDL_PushGPUFragmentUniformData(cmd, 1, &frame_shadow_uniforms_, sizeof(frame_shadow_uniforms_));
}

} // namespace mmo::engine::scene

#include "scene_renderer.hpp"
#include "engine/core/assert.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/profiler.hpp"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_debug.hpp"
#include "engine/gpu/gpu_pipeline.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_uniforms.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/heightmap.hpp"
#include "engine/model_loader.hpp"
#include "engine/render/ambient_occlusion.hpp"
#include "engine/render/bloom.hpp"
#include "engine/render/effect_renderer.hpp"
#include "engine/render/grass_renderer.hpp"
#include "engine/render/render_context.hpp"
#include "engine/render/shadow_map.hpp"
#include "engine/render/terrain_renderer.hpp"
#include "engine/render/ui_renderer.hpp"
#include "engine/render/volumetric_fog.hpp"
#include "engine/render/world_renderer.hpp"
#include "engine/render_constants.hpp"
#include "engine/systems/effect_system.hpp"
#include "frustum.hpp"
#include "SDL3/SDL_gpu.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <type_traits>

#include "engine/graphics_settings.hpp"
#include "engine/scene/camera_state.hpp"
#include "engine/scene/render_scene.hpp"
#include "engine/scene/transform_math.hpp"
#include "engine/scene/ui_scene.hpp"
#include "glm/common.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/matrix.hpp"

namespace mmo::engine::scene {

namespace gpu = mmo::engine::gpu;
namespace render = mmo::engine::render;

// ModelManager::get_model(handle) is already O(1) via vector index — direct
// lookup beats a hashmap cache, so we don't memoize.
static inline Model* resolve_model(ModelManager& mgr, ModelHandle handle, const std::string& name) {
    if (handle != INVALID_MODEL_HANDLE) {
        return mgr.get_model(handle);
    }
    if (name.empty()) {
        return nullptr;
    }
    return mgr.get_model(name);
}

static inline ModelHandle resolve_handle(ModelManager& mgr, ModelHandle handle, const std::string& name) {
    if (handle != INVALID_MODEL_HANDLE) {
        return handle;
    }
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
      volumetric_fog_(std::make_unique<render::VolumetricFog>()) {}

SceneRenderer::~SceneRenderer() {
    shutdown();
}

// ========== Accessors (out-of-line for forward-declared types) ==========

render::RenderContext* SceneRenderer::context() {
    return context_;
}
render::TerrainRenderer& SceneRenderer::terrain() {
    return *terrain_;
}
ModelManager& SceneRenderer::models() {
    return *model_manager_;
}
render::GrassRenderer* SceneRenderer::grass() {
    return grass_renderer_.get();
}
float SceneRenderer::get_terrain_height(float x, float z) {
    return terrain_->get_height(x, z);
}

bool SceneRenderer::init(render::RenderContext& context, float world_width, float world_height) {
    context_ = &context;

    if (!pipeline_registry_->init(context_->device())) {
        std::cerr << "Failed to initialize pipeline registry" << '\n';
        return false;
    }
    pipeline_registry_->set_swapchain_format(context_->swapchain_format());

    model_manager_->set_device(&context_->device());

    if (!terrain_->init(context_->device(), *pipeline_registry_, world_width, world_height)) {
        std::cerr << "Failed to initialize terrain renderer" << '\n';
        return false;
    }

    if (!world_->init(context_->device(), *pipeline_registry_, world_width, world_height, model_manager_.get())) {
        std::cerr << "Failed to initialize world renderer" << '\n';
        return false;
    }

    world_->set_terrain_height_func([this](float x, float z) { return terrain_->get_height(x, z); });

    int w = context_->width();
    int h = context_->height();

    if (!ui_->init(context_->device(), *pipeline_registry_, w, h)) {
        std::cerr << "Failed to initialize UI renderer" << '\n';
        return false;
    }

    if (!effects_->init(context_->device(), *pipeline_registry_, model_manager_.get())) {
        std::cerr << "Failed to initialize effect renderer" << '\n';
        return false;
    }
    effects_->set_terrain_height_func([this](float x, float z) { return terrain_->get_height(x, z); });

    depth_texture_ = gpu::GPUTexture::create_depth(context_->device(), w, h);
    if (!depth_texture_) {
        std::cerr << "Failed to create depth texture" << '\n';
        return false;
    }

    init_pipelines();
    init_billboard_buffers();

    // Preload all pipelines upfront to avoid hitching during gameplay
    if (!pipeline_registry_->preload_all_pipelines()) {
        std::cerr << "Warning: Some pipelines failed to preload" << '\n';
    }

    if (grass_renderer_) {
        grass_renderer_->init(context_->device(), *pipeline_registry_, world_width, world_height);
    }

    const GraphicsSettings& gfx = graphics_ ? *graphics_ : default_graphics_;
    static constexpr int resolution_table[] = {512, 1024, 2048, 4096};
    int shadow_res = resolution_table[std::clamp(gfx.shadow_resolution, 0, 3)];
    shadow_map_->set_active_cascades(gfx.shadow_cascades + 1);
    if (!shadow_map_->init(context_->device(), shadow_res)) {
        std::cerr << "Warning: Failed to initialize shadow map (shadows disabled)" << '\n';
    }

    if (!ao_->init(context_->device(), w, h)) {
        std::cerr << "Warning: Failed to initialize GTAO (AO disabled)" << '\n';
    }

    if (!bloom_->init(context_->device(), w, h)) {
        std::cerr << "Warning: Failed to initialize Bloom (bloom disabled)" << '\n';
    }

    if (!volumetric_fog_->init(context_->device(), w, h)) {
        std::cerr << "Warning: Failed to initialize Volumetric Fog (fog disabled)" << '\n';
    }

    gpu_timer_pool_.init(context_->device(), 64);

    cluster_grid_ready_ = cluster_grid_.init(context_->device(), render::lighting::MAX_LIGHTS);
    if (!cluster_grid_ready_) {
        std::cerr << "Warning: Failed to initialize ClusterGrid (dynamic lighting disabled)" << '\n';
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

    gpu_timer_pool_.shutdown();
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
        std::cerr << "Warning: Some pipelines failed to preload" << '\n';
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
        std::cerr << "Warning: Failed to create default GPU sampler" << '\n';
    }

    // Create 1x1 white dummy texture for binding slots that require a texture
    {
        uint32_t white_pixel = 0xFFFFFFFF;
        dummy_white_texture_ =
            gpu::GPUTexture::create_2d(context_->device(), 1, 1, gpu::TextureFormat::RGBA8, &white_pixel);
    }

    // 1x1 flat tangent-space normal: (0,0,1) packed as UNORM (128,128,255).
    // Bound on the normal-map slot when the material has no normal map, so the
    // shader's branch-free path produces N == N_geo.
    {
        uint8_t flat_normal[4] = {128, 128, 255, 255};
        default_normal_texture_ =
            gpu::GPUTexture::create_2d(context_->device(), 1, 1, gpu::TextureFormat::RGBA8, flat_normal);
    }
}

void SceneRenderer::init_billboard_buffers() {
    constexpr size_t BILLBOARD_BUFFER_SIZE = 6 * 7 * sizeof(float);
    billboard_vertex_buffer_ =
        gpu::GPUBuffer::create_dynamic(context_->device(), gpu::GPUBuffer::Type::Vertex, BILLBOARD_BUFFER_SIZE);

    if (!billboard_vertex_buffer_) {
        std::cerr << "Warning: Failed to create billboard vertex buffer" << '\n';
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

    // Forward lighting tunables to renderers that own their own uniform builders.
    if (terrain_) {
        terrain_->set_lighting_tunables(settings.ambient_strength, settings.sun_intensity);
    }
    if (grass_renderer_) {
        grass_renderer_->ambient_strength = settings.ambient_strength;
        grass_renderer_->sun_intensity = settings.sun_intensity;
    }
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
            std::cerr << "Warning: Failed to recreate default GPU sampler (anisotropic level: " << level << ")" << '\n';
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

    std::cout << "[Renderer] Heightmap set for terrain rendering" << '\n';
}

int SceneRenderer::spawn_effect(const mmo::engine::EffectDefinition* definition, const glm::vec3& position,
                                const glm::vec3& direction, float range) {
    return effect_system_->spawn_effect(definition, position, direction, range);
}

// ============================================================================
// Frame Lifecycle
// ============================================================================

void SceneRenderer::begin_frame() {
    context_->begin_frame();
    had_main_pass_this_frame_ = false;
    ui_->set_screen_size(context_->width(), context_->height());
}

void SceneRenderer::end_frame() {
    current_swapchain_ = nullptr;
    context_->end_frame();
}

void SceneRenderer::begin_main_pass() {
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) {
        std::cerr << "begin_main_pass: No active command buffer" << '\n';
        return;
    }

    uint32_t sw_width = 0;
    uint32_t sw_height = 0;
    current_swapchain_ = context_->acquire_swapchain_texture(cmd, &sw_width, &sw_height);
    if (!current_swapchain_) {
        std::cerr << "begin_main_pass: Failed to acquire swapchain texture" << '\n';
        return;
    }

    if (depth_texture_ && (depth_texture_->width() != static_cast<int>(sw_width) ||
                           depth_texture_->height() != static_cast<int>(sw_height))) {
        depth_texture_ = gpu::GPUTexture::create_depth(context_->device(), sw_width, sw_height);
        if (!depth_texture_) {
            std::cerr << "begin_main_pass: Failed to resize depth texture" << '\n';
            return;
        }
    }

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = current_swapchain_;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = {0.35f, 0.45f, 0.6f, 1.0f};

    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = depth_texture_ ? depth_texture_->handle() : nullptr;
    // If depth pre-pass already wrote depth, preserve it (LOAD) instead of clearing
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    main_render_pass_ = SDL_BeginGPURenderPass(cmd, &color_target, 1, depth_texture_ ? &depth_target : nullptr);
    if (!main_render_pass_) {
        std::cerr << "begin_main_pass: Failed to begin render pass" << '\n';
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
        std::cerr << "begin_ui: No active command buffer" << '\n';
        return;
    }

    if (!current_swapchain_) {
        uint32_t sw_width = 0;
        uint32_t sw_height = 0;
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

void SceneRenderer::render_frame(RenderScene& scene, const UIScene& ui_scene, const CameraState& camera, float dt) {
    ENGINE_PROFILE_ZONE("SceneRenderer::render_frame");
    if (collect_stats_) {
        render_stats_ = {};
        render_stats_.jobs_pending = core::jobs::JobSystem::instance().pending_count();
    }
    const GraphicsSettings& gfx = graphics_ ? *graphics_ : default_graphics_;

    // Cache frame-level state once (avoids repeated ternary/function calls)
    frame_fog_active_ = gfx.fog_enabled;
    frame_draw_dist_sq_ = gfx.get_draw_distance() * gfx.get_draw_distance();
    frame_do_frustum_cull_ = gfx.frustum_culling;

    // Update particle effect system
    auto get_terrain_height = [this](float x, float z) -> float { return terrain_->get_height(x, z); };
    effect_system_->update(dt, get_terrain_height);

    begin_frame();
    gpu_timer_pool_.begin_frame();

    bool has_content = scene.has_3d_content();
    // Always render 3D through the HDR offscreen target so scene lighting stays
    // in linear HDR; the composite pass tonemaps + gamma-corrects to the swapchain.
    // ao_mode == 0 simply skips the AO pass (aoStrength=0 in composite).
    bool use_ao = ao_->is_ready();

    if (has_content) {
        // Extract main camera frustum once (reused in render_3d_scene for skinned models).
        frame_frustum_.extract_from_matrix(camera.view_projection);

        if (camera.projection != cached_projection_) {
            cached_projection_ = camera.projection;
            cached_inv_projection_ = glm::inverse(camera.projection);
        }

        // Update cascade matrices FIRST so we can extract per-cascade light frustums
        // and use them to cull shadow casters per cascade in build_instance_batches.
        const bool shadows_on = shadow_map_->is_ready() && gfx.shadow_mode > 0;
        if (shadows_on) {
            shadow_map_->update(camera.view, camera.projection, light_dir_, 5.0f, 2000.0f);
            const int active = shadow_map_->active_cascades();
            for (int c = 0; c < active; ++c) {
                shadow_cascade_frustums_[c].extract_from_matrix(shadow_map_->cascades()[c].light_view_projection);
            }
        }

        // Build instance batches (cull for main + per-cascade shadows) and upload.
        if (SDL_GPUCommandBuffer* bb_cmd = context_->current_command_buffer()) {
            ENGINE_GPU_SCOPE(bb_cmd, "InstanceBatchBuild");
            ENGINE_GPU_TIMER(gpu_timer_pool_, bb_cmd, "InstanceBatchBuild");
            build_instance_batches(scene, camera, frame_frustum_);
            upload_instance_buffers();
        } else {
            build_instance_batches(scene, camera, frame_frustum_);
            upload_instance_buffers();
        }

        // Upload grass chunk storage buffer BEFORE any render pass begins
        // (copy pass cannot be nested in a render pass).
        if (scene.should_draw_grass() && gfx.grass_enabled && grass_renderer_) {
            grass_renderer_->grass_view_distance = std::min(gfx.get_draw_distance(), 600.0f);
            grass_renderer_->update(dt, skybox_time_);
            SDL_GPUCommandBuffer* pre_cmd = context_->current_command_buffer();
            if (pre_cmd) {
                ENGINE_GPU_SCOPE(pre_cmd, "GrassChunkUpload");
                grass_renderer_->upload_chunks(pre_cmd, camera.position, &frame_frustum_);
            }
        }

        if (shadows_on) {
            render_shadow_passes(scene, camera);
        }

        // Cache shadow uniforms (cascade matrices are stable after update()).
        if (shadow_map_->is_ready()) {
            frame_shadow_uniforms_ = shadow_map_->get_shadow_uniforms(gfx.shadow_mode);
        }

        // Build cluster light grid + upload SSBOs before main pass begins
        // (copy passes cannot be nested in render passes). Skip the CPU build
        // + GPU upload when the scene has zero dynamic lights — the per-frag
        // cluster path early-outs via gridDim.w==0. We still need to bind the
        // SSBOs to terrain/grass pipelines below so those binding slots are
        // valid; the buffers retain their last-uploaded state (initially zero).
        const bool any_dyn_lights = !scene.point_lights().empty() || !scene.spot_lights().empty();
        if (cluster_grid_ready_) {
            if (any_dyn_lights) {
                uint32_t cw = static_cast<uint32_t>(std::max(context_->width(), 1));
                uint32_t ch = static_cast<uint32_t>(std::max(context_->height(), 1));
                cluster_grid_.begin_frame(camera, cw, ch, 0.1f, gfx.get_draw_distance());
                for (const auto& l : scene.point_lights()) cluster_grid_.add_point_light(l);
                for (const auto& l : scene.spot_lights()) cluster_grid_.add_spot_light(l);
                cluster_grid_.build();
                if (SDL_GPUCommandBuffer* lc = context_->current_command_buffer()) {
                    ENGINE_GPU_SCOPE(lc, "ClusterLightUpload");
                    cluster_grid_.upload(lc);
                }
            }
            terrain_->set_cluster_lighting(cluster_grid_.light_data_buffer(), cluster_grid_.cluster_offsets_buffer(),
                                           cluster_grid_.light_indices_buffer(), &cluster_grid_.params(),
                                           sizeof(cluster_grid_.params()));
            if (grass_renderer_) {
                grass_renderer_->set_cluster_lighting(
                    cluster_grid_.light_data_buffer(), cluster_grid_.cluster_offsets_buffer(),
                    cluster_grid_.light_indices_buffer(), &cluster_grid_.params(), sizeof(cluster_grid_.params()));
            }
        }

        SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();

        if (use_ao) {
            // === GTAO PATH: render to offscreen, then AO passes, then composite ===

            // Acquire swapchain first (needed for composite + UI later)
            uint32_t sw_width = 0;
            uint32_t sw_height = 0;
            current_swapchain_ = context_->acquire_swapchain_texture(cmd, &sw_width, &sw_height);

            // Resize GTAO + bloom + volumetric fog textures if window size changed
            if (current_swapchain_) {
                ao_->resize(static_cast<int>(sw_width), static_cast<int>(sw_height));
                bloom_->resize(static_cast<int>(sw_width), static_cast<int>(sw_height));
                volumetric_fog_->resize(static_cast<int>(sw_width), static_cast<int>(sw_height));
            }

            {
                ENGINE_GPU_SCOPE(cmd, "AOOffscreen");
                ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, "AOOffscreen");
                main_render_pass_ = ao_->begin_offscreen_pass(cmd);
                had_main_pass_this_frame_ = (main_render_pass_ != nullptr);

                if (main_render_pass_ && cmd) {
                    render_3d_scene(scene, camera, dt);
                    ao_->end_offscreen_pass();
                    main_render_pass_ = nullptr;
                }
            }

            // AO computation + blur + bloom + composite to swapchain
            if (current_swapchain_) {
                glm::mat4 inv_proj = cached_inv_projection_;
                if (gfx.ao_mode > 0 && cmd) {
                    const char* ao_name = (gfx.ao_mode == 1) ? "SSAO" : "GTAO";
                    ENGINE_GPU_SCOPE(cmd, ao_name);
                    ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, ao_name);
                    if (gfx.ao_mode == 1) {
                        ao_->render_ssao_pass(cmd, *pipeline_registry_, camera.projection, inv_proj, gfx.ao_radius);
                    } else {
                        ao_->render_gtao_pass(cmd, *pipeline_registry_, camera.projection, inv_proj, gfx.ao_radius);
                    }
                    {
                        ENGINE_GPU_SCOPE(cmd, "BlurAO");
                        ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, "BlurAO");
                        ao_->render_blur_pass(cmd, *pipeline_registry_);
                    }
                }

                // Bloom: downsample + upsample chain on offscreen color
                SDL_GPUTexture* bloom_tex = nullptr;
                if (gfx.bloom_enabled && bloom_->is_ready()) {
                    SDL_GPUTexture* scene_in = ao_->offscreen_color() ? ao_->offscreen_color()->handle() : nullptr;
                    if (cmd && scene_in) {
                        ENGINE_GPU_SCOPE(cmd, "Bloom");
                        ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, "Bloom");
                        bloom_->render(cmd, *pipeline_registry_, scene_in, gfx.bloom_threshold);
                    }
                    bloom_tex = bloom_->bloom_texture();
                }

                // Volumetric fog: ray march through fog volume using depth + shadow map
                SDL_GPUTexture* fog_tex = nullptr;
                if ((gfx.volumetric_fog || gfx.god_rays) && volumetric_fog_->is_ready() && shadow_map_->is_ready() &&
                    ao_->offscreen_depth()) {
                    ENGINE_GPU_SCOPE(cmd, "VolumetricFog");
                    ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, "VolumetricFog");
                    // Default fog_density is 0.05; treat as a multiplier on the renderer's base density.
                    float fog_density_mul = gfx.fog_density / 0.05f;
                    volumetric_fog_->render(cmd, *pipeline_registry_, ao_->offscreen_depth()->handle(), *shadow_map_,
                                            camera, light_dir_, gfx.god_rays, gfx.volumetric_fog, fog_density_mul);
                    fog_tex = volumetric_fog_->fog_texture();
                }

                if (cmd) {
                    render::AmbientOcclusion::CompositeParams composite_params;
                    composite_params.bloom_strength = gfx.bloom_enabled ? gfx.bloom_strength : 0.0f;
                    composite_params.ao_strength = gfx.ao_mode > 0 ? gfx.ao_strength : 0.0f;
                    composite_params.exposure = gfx.exposure;
                    composite_params.tonemap_mode = gfx.tonemap_mode;
                    composite_params.contrast = gfx.contrast;
                    composite_params.saturation = gfx.saturation;

                    ENGINE_GPU_SCOPE(cmd, "Composite");
                    ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, "Composite");
                    ao_->render_composite_pass(cmd, *pipeline_registry_, current_swapchain_, bloom_tex, fog_tex,
                                               composite_params);
                }
            }
        } else {
            // === NORMAL PATH: render directly to swapchain ===

            ENGINE_GPU_SCOPE(cmd, "MainScene");
            ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, "MainScene");
            begin_main_pass();

            if (main_render_pass_ && context_->current_command_buffer()) {
                render_3d_scene(scene, camera, dt);
            }

            end_main_pass();
        }
    }

    if (SDL_GPUCommandBuffer* ui_cmd = context_->current_command_buffer()) {
        ENGINE_GPU_SCOPE(ui_cmd, "UI");
        ENGINE_GPU_TIMER(gpu_timer_pool_, ui_cmd, "UI");
        begin_ui();
        render_ui_commands(ui_scene, camera);
        for (const auto& billboard : scene.billboards()) {
            draw_billboard_3d(billboard, camera);
        }
        end_ui();
    }

    if (post_ui_callback_) {
        if (SDL_GPUCommandBuffer* post_cmd = context_->current_command_buffer()) {
            ENGINE_GPU_SCOPE(post_cmd, "PostUICallback");
            ENGINE_GPU_TIMER(gpu_timer_pool_, post_cmd, "PostUICallback");
            post_ui_callback_(post_cmd, current_swapchain_);
        }
    }

    if (SDL_GPUCommandBuffer* end_cmd = context_->current_command_buffer()) {
        gpu_timer_pool_.end_frame(end_cmd);
    }
    if (collect_stats_) {
        gpu_timer_pool_.read_results(render_stats_.pass_times_ms);
    }
    end_frame();
}

// ============================================================================
// 3D Scene Rendering (shared between normal and AO paths)
// ============================================================================

void SceneRenderer::render_3d_scene(RenderScene& scene, const CameraState& camera, float dt) {
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
        terrain_->render(main_render_pass_, cmd, camera.view, camera.projection, camera.position, light_dir_,
                         shadow_binding_count > 0 ? shadow_bindings : nullptr, shadow_binding_count, &frame_frustum_);
    }

    if (scene.should_draw_grass() && gfx.grass_enabled && grass_renderer_) {
        // view_distance + chunk upload already performed before render pass began.
        SDL_PushGPUFragmentUniformData(cmd, 1, &frame_shadow_uniforms_, sizeof(frame_shadow_uniforms_));
        grass_renderer_->render(main_render_pass_, cmd, camera.view, camera.projection, camera.position, light_dir_,
                                shadow_binding_count > 0 ? shadow_bindings : nullptr, shadow_binding_count);
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
            bind_cluster_lights(main_render_pass_, context_->current_command_buffer(), 2);
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
                if (collect_stats_) {
                    render_stats_.entities_distance_culled++;
                }
                continue;
            }

            if (do_frustum_cull) {
                Model* model = resolve_model(*model_manager_, data.model_handle, data.model_name);
                if (model) {
                    glm::vec3 world_center = glm::vec3(t * glm::vec4(model->bounding_center, 1.0f));
                    float max_scale = max_scale_factor(t);
                    if (!frustum.intersects_sphere(world_center, model->bounding_half_diag * max_scale)) {
                        if (collect_stats_) {
                            render_stats_.entities_frustum_culled++;
                        }
                        continue;
                    }
                }
            }

            if (collect_stats_) {
                render_stats_.entities_rendered++;
            }

            // Bind pipeline + shadow data once for all skinned models
            if (!skinned_pipeline_bound) {
                gpu::GPUPipeline* skinned_pipeline = pipeline_registry_->get_skinned_model_pipeline();
                if (!skinned_pipeline) {
                    break;
                }
                skinned_pipeline->bind(main_render_pass_);
                bind_shadow_data(main_render_pass_, context_->current_command_buffer(), 1);
                bind_cluster_lights(main_render_pass_, context_->current_command_buffer(), 2);
                skinned_pipeline_bound = true;
            }

            render_skinned_model_command_inner(data, camera, last_skinned_texture, last_skinned_has_texture);
        }
    }

    // Process particle effect spawn commands from the scene
    const auto& spawns = scene.particle_effect_spawns();
    for (const auto& spawn_cmd : spawns) {
        if (spawn_cmd.definition) {
            effect_system_->spawn_effect(spawn_cmd.definition, spawn_cmd.position, spawn_cmd.direction,
                                         spawn_cmd.range);
        }
    }

    scene.clear_particle_effect_spawns();

    // Render new particle-based effects
    effects_->draw_particle_effects(main_render_pass_, context_->current_command_buffer(), *effect_system_, camera.view,
                                    camera.projection, camera.position);

    // Debug line rendering (only if game submitted any)
    render_debug_lines(scene, camera);
}

// ============================================================================
// Model Rendering
// ============================================================================

void SceneRenderer::render_model_command(const ModelCommand& cmd, const CameraState& camera) {
    // Standalone path: binds pipeline + shadows itself
    if (!main_render_pass_) {
        return;
    }
    gpu::GPUPipeline* pipeline = pipeline_registry_->get_model_pipeline();
    if (!pipeline) {
        return;
    }
    pipeline->bind(main_render_pass_);
    bind_shadow_data(main_render_pass_, context_->current_command_buffer(), 1);
    bind_cluster_lights(main_render_pass_, context_->current_command_buffer(), 2);
    render_model_command_inner(cmd, camera);
}

void SceneRenderer::render_model_command_inner(const ModelCommand& cmd, const CameraState& camera) {
    Model* model = resolve_model(*model_manager_, cmd.model_handle, cmd.model_name);
    if (!model || !main_render_pass_) {
        return;
    }

    SDL_GPUCommandBuffer* gpu_cmd = context_->current_command_buffer();
    if (!gpu_cmd) {
        return;
    }

    const glm::mat4& model_mat = cmd.transform;

    gpu::ModelTransformUniforms transform_uniforms = {};
    transform_uniforms.model = model_mat;
    transform_uniforms.view = camera.view;
    transform_uniforms.projection = camera.projection;
    transform_uniforms.cameraPos = camera.position;
    transform_uniforms.normalMatrix = compute_normal_matrix(model_mat);
    transform_uniforms.useSkinning = 0;

    bool fog_active = !cmd.no_fog && frame_fog_active_;

    const GraphicsSettings& gfx_local = graphics_ ? *graphics_ : default_graphics_;
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
    lighting_uniforms.ambientStrength = gfx_local.ambient_strength;
    lighting_uniforms.sunIntensity = gfx_local.sun_intensity;

    SDL_PushGPUVertexUniformData(gpu_cmd, 0, &transform_uniforms, sizeof(transform_uniforms));

    for (auto& mesh : model->meshes) {
        if (mesh.shadow_only) {
            continue; // canopy proxy etc. — shadow-pass only
        }
        ENGINE_ASSERT(mesh.uploaded, "Mesh not uploaded before non-instanced render");
        if (!mesh.uploaded) {
            continue;
        }

        if (!mesh.vertex_buffer || !mesh.index_buffer) {
            continue;
        }
        if (mesh.index_count() == 0) {
            continue;
        }

        lighting_uniforms.hasTexture = (mesh.has_texture && mesh.texture) ? 1 : 0;
        bool has_nm = mesh.material.normal_texture != nullptr;
        lighting_uniforms.hasNormalMap = has_nm ? 1 : 0;
        lighting_uniforms.normalScale = mesh.material.normal_scale;
        SDL_PushGPUFragmentUniformData(gpu_cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));

        if (mesh.has_texture && mesh.texture && default_sampler_) {
            SDL_GPUTextureSamplerBinding tex_binding = {mesh.texture->handle(), default_sampler_};
            SDL_BindGPUFragmentSamplers(main_render_pass_, 0, &tex_binding, 1);
        }

        if (default_sampler_) {
            SDL_GPUTexture* nm_tex = has_nm ? mesh.material.normal_texture->handle()
                                            : (default_normal_texture_ ? default_normal_texture_->handle() : nullptr);
            if (nm_tex) {
                SDL_GPUTextureSamplerBinding nm_binding = {nm_tex, default_sampler_};
                SDL_BindGPUFragmentSamplers(main_render_pass_, 5, &nm_binding, 1);
            }
        }

        mesh.bind_buffers(main_render_pass_);
        if (collect_stats_) {
            render_stats_.draw_calls++;
            render_stats_.triangle_count += mesh.index_count() / 3;
        }
        SDL_DrawGPUIndexedPrimitives(main_render_pass_, mesh.index_count(), 1, 0, 0, 0);
    }
}

// ============================================================================
// Instanced Rendering
// ============================================================================

void SceneRenderer::build_instance_batches(const RenderScene& scene, const CameraState& camera,
                                           const Frustum& frustum) {
    // Clear vectors but preserve map keys + vector capacity to avoid re-hashing
    // and re-allocating every frame.
    for (auto& [handle, vec] : instance_batches_) vec.clear();
    for (int c = 0; c < 4; ++c) {
        for (auto& [handle, vec] : shadow_instance_batches_[c]) vec.clear();
    }
    non_instanced_commands_.clear();

    // Shadow state: which cascades are active, and their light frustums.
    const int active_cascades = shadow_map_->is_ready() ? shadow_map_->active_cascades() : 0;

    bool do_frustum_cull = frame_do_frustum_cull_;
    float draw_dist_sq = frame_draw_dist_sq_;

    for (const auto& cmd : scene.model_commands()) {
        const glm::mat4& t = cmd.transform;
        glm::vec3 world_pos(t[3][0], t[3][1], t[3][2]);

        float dx = world_pos.x - camera.position.x;
        float dz = world_pos.z - camera.position.z;
        if (dx * dx + dz * dz > draw_dist_sq) {
            if (collect_stats_) {
                render_stats_.entities_distance_culled++;
            }
            continue;
        }

        ModelHandle handle = resolve_handle(*model_manager_, cmd.model_handle, cmd.model_name);

        glm::vec3 world_center{};
        float world_radius = 0.0f;
        bool have_bounds = false;
        if (do_frustum_cull || active_cascades > 0) {
            Model* model = (handle != INVALID_MODEL_HANDLE) ? model_manager_->get_model(handle) : nullptr;
            if (!model && !cmd.model_name.empty()) {
                model = model_manager_->get_model(cmd.model_name);
            }
            if (model) {
                world_center = glm::vec3(t * glm::vec4(model->bounding_center, 1.0f));
                world_radius = model->bounding_half_diag * max_scale_factor(t);
                have_bounds = true;
            }
        }

        bool main_visible = true;
        if (do_frustum_cull && have_bounds) {
            if (!frustum.intersects_sphere(world_center, world_radius)) {
                main_visible = false;
            }
        }

        if (!main_visible) {
            if (collect_stats_) {
                render_stats_.entities_frustum_culled++;
            }
            if (!cmd.force_non_instanced && have_bounds) {
                gpu::ShadowInstanceData shadow_instance{};
                shadow_instance.model = cmd.transform;
                for (int c = 0; c < active_cascades; ++c) {
                    if (shadow_cascade_frustums_[c].intersects_sphere(world_center, world_radius)) {
                        shadow_instance_batches_[c][handle].push_back(shadow_instance);
                    }
                }
            }
            continue;
        }

        if (cmd.force_non_instanced) {
            if (collect_stats_) {
                render_stats_.entities_rendered++;
            }
            non_instanced_commands_.push_back(&cmd);
            continue;
        }

        if (collect_stats_) {
            render_stats_.entities_rendered++;
        }

        gpu::InstanceData instance{};
        instance.model = cmd.transform;
        instance.normalMatrix = compute_normal_matrix(cmd.transform);
        instance.tint = cmd.tint;
        instance.noFog = cmd.no_fog ? 1.0f : 0.0f;
        instance_batches_[handle].push_back(instance);

        gpu::ShadowInstanceData shadow_instance{};
        shadow_instance.model = cmd.transform;
        for (int c = 0; c < active_cascades; ++c) {
            if (!have_bounds || shadow_cascade_frustums_[c].intersects_sphere(world_center, world_radius)) {
                shadow_instance_batches_[c][handle].push_back(shadow_instance);
            }
        }
    }
}

void SceneRenderer::upload_instance_buffers() {
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) {
        return;
    }

    // Upload main instance buffer
    size_t total_instances = 0;
    for (const auto& [name, instances] : instance_batches_) {
        total_instances += instances.size();
    }

    if (total_instances > 0) {
        size_t required_size = total_instances * sizeof(gpu::InstanceData);

        if (!instance_storage_buffer_ || instance_storage_capacity_ < required_size) {
            instance_storage_capacity_ = required_size * 2; // over-allocate
            instance_storage_buffer_ = gpu::GPUBuffer::create_dynamic(context_->device(), gpu::GPUBuffer::Type::Storage,
                                                                      instance_storage_capacity_);
        }

        packed_instances_.clear();
        packed_instances_.reserve(total_instances);
        for (const auto& [name, instances] : instance_batches_) {
            packed_instances_.insert(packed_instances_.end(), instances.begin(), instances.end());
        }

        instance_storage_buffer_->update(cmd, packed_instances_.data(), required_size);
    }

    // Upload shadow instance buffer (per-cascade sections packed contiguously).
    // Layout: [cascade 0 instances][cascade 1 instances]... each cascade orders by ModelHandle.
    size_t total_shadow = 0;
    for (int c = 0; c < 4; ++c) {
        for (const auto& [name, instances] : shadow_instance_batches_[c]) {
            total_shadow += instances.size();
        }
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
        for (int c = 0; c < 4; ++c) {
            shadow_cascade_base_instance_[c] = static_cast<uint32_t>(packed_shadow_instances_.size());
            for (const auto& [name, instances] : shadow_instance_batches_[c]) {
                packed_shadow_instances_.insert(packed_shadow_instances_.end(), instances.begin(), instances.end());
            }
        }

        shadow_instance_storage_buffer_->update(cmd, packed_shadow_instances_.data(), required_size);
    } else {
        shadow_cascade_base_instance_ = {0, 0, 0, 0};
    }
}

void SceneRenderer::render_instanced_models(const CameraState& camera) {
    if (instance_batches_.empty() || !main_render_pass_) {
        return;
    }

    SDL_GPUCommandBuffer* gpu_cmd = context_->current_command_buffer();
    if (!gpu_cmd) {
        return;
    }

    gpu::GPUPipeline* pipeline = pipeline_registry_->get_instanced_model_pipeline();
    if (!pipeline || !instance_storage_buffer_) {
        return;
    }

    bool fog_active = frame_fog_active_;

    // Push shared camera uniforms (once)
    gpu::InstancedCameraUniforms camera_uniforms = {};
    camera_uniforms.view = camera.view;
    camera_uniforms.projection = camera.projection;
    camera_uniforms.cameraPos = camera.position;

    // Push shared lighting uniforms
    const GraphicsSettings& gfx_inst = graphics_ ? *graphics_ : default_graphics_;
    gpu::InstancedLightingUniforms lighting_uniforms = {};
    lighting_uniforms.lightDir = light_dir_;
    lighting_uniforms.lightColor = lighting::LIGHT_COLOR;
    lighting_uniforms.ambientColor = fog_active ? lighting::AMBIENT_COLOR : lighting::AMBIENT_COLOR_NO_FOG;
    lighting_uniforms.fogColor = fog_active ? fog::COLOR : fog::DISTANT_COLOR;
    lighting_uniforms.fogStart = fog_active ? fog::START : fog::DISTANT_START;
    lighting_uniforms.fogEnd = fog_active ? fog::END : fog::DISTANT_END;
    lighting_uniforms.fogEnabled = fog_active ? 1 : 0;
    lighting_uniforms.cameraPos = camera.position;
    lighting_uniforms.ambientStrength = gfx_inst.ambient_strength;
    lighting_uniforms.sunIntensity = gfx_inst.sun_intensity;

    pipeline->bind(main_render_pass_);
    SDL_PushGPUVertexUniformData(gpu_cmd, 0, &camera_uniforms, sizeof(camera_uniforms));

    // Bind instance storage buffer
    SDL_GPUBuffer* storage_buf = instance_storage_buffer_->handle();
    SDL_BindGPUVertexStorageBuffers(main_render_pass_, 0, &storage_buf, 1);

    // Bind shadow map data
    bind_shadow_data(main_render_pass_, gpu_cmd, 1);
    bind_cluster_lights(main_render_pass_, gpu_cmd, 2);

    // Push lighting uniforms once (only hasTexture varies per mesh)
    SDL_PushGPUFragmentUniformData(gpu_cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));

    // Track last state to avoid redundant GPU calls
    int last_has_texture = lighting_uniforms.hasTexture;
    SDL_GPUTexture* last_bound_texture = nullptr;

    // Draw each model type as a single instanced draw call per mesh
    uint32_t base_instance = 0;
    for (const auto& [batch_handle, instances] : instance_batches_) {
        if (instances.empty()) {
            continue;
        }

        Model* model = (batch_handle != INVALID_MODEL_HANDLE) ? model_manager_->get_model(batch_handle) : nullptr;
        if (!model) {
            base_instance += static_cast<uint32_t>(instances.size());
            continue;
        }

        uint32_t instance_count = static_cast<uint32_t>(instances.size());

        for (auto& mesh : model->meshes) {
            if (mesh.shadow_only) {
                continue;
            }
            ENGINE_ASSERT(mesh.uploaded, "Mesh not uploaded before instanced render");
            if (!mesh.uploaded) {
                continue;
            }
            if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) {
                continue;
            }

            int has_tex = (mesh.has_texture && mesh.texture) ? 1 : 0;
            bool has_nm = mesh.material.normal_texture != nullptr;
            if (has_tex != last_has_texture || lighting_uniforms.hasNormalMap != (has_nm ? 1 : 0) ||
                lighting_uniforms.normalScale != mesh.material.normal_scale) {
                lighting_uniforms.hasTexture = has_tex;
                lighting_uniforms.hasNormalMap = has_nm ? 1 : 0;
                lighting_uniforms.normalScale = mesh.material.normal_scale;
                SDL_PushGPUFragmentUniformData(gpu_cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));
                last_has_texture = has_tex;
            }

            if (mesh.has_texture && mesh.texture && default_sampler_) {
                SDL_GPUTexture* tex_handle = mesh.texture->handle();
                if (tex_handle != last_bound_texture) {
                    SDL_GPUTextureSamplerBinding tex_binding = {tex_handle, default_sampler_};
                    SDL_BindGPUFragmentSamplers(main_render_pass_, 0, &tex_binding, 1);
                    last_bound_texture = tex_handle;
                }
            }

            if (default_sampler_) {
                SDL_GPUTexture* nm_tex = has_nm
                                             ? mesh.material.normal_texture->handle()
                                             : (default_normal_texture_ ? default_normal_texture_->handle() : nullptr);
                if (nm_tex) {
                    SDL_GPUTextureSamplerBinding nm_binding = {nm_tex, default_sampler_};
                    SDL_BindGPUFragmentSamplers(main_render_pass_, 5, &nm_binding, 1);
                }
            }

            mesh.bind_buffers(main_render_pass_);
            if (collect_stats_) {
                render_stats_.draw_calls++;
                render_stats_.triangle_count += mesh.index_count() / 3 * instance_count;
            }
            SDL_DrawGPUIndexedPrimitives(main_render_pass_, mesh.index_count(), instance_count, 0, 0, base_instance);
        }

        base_instance += instance_count;
    }
}

void SceneRenderer::render_instanced_shadow_models(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                   const glm::mat4& light_view_projection, int cascade) {
    if (cascade < 0 || cascade >= 4) {
        return;
    }
    const auto& cascade_batches = shadow_instance_batches_[cascade];
    if (cascade_batches.empty() || !pass || !shadow_instance_storage_buffer_) {
        return;
    }

    auto* pipeline = pipeline_registry_->get_instanced_shadow_model_pipeline();
    if (!pipeline) {
        return;
    }

    pipeline->bind(pass);

    gpu::InstancedShadowUniforms shadow_uniforms = {};
    shadow_uniforms.lightViewProjection = light_view_projection;
    SDL_PushGPUVertexUniformData(cmd, 0, &shadow_uniforms, sizeof(shadow_uniforms));

    SDL_GPUBuffer* storage_buf = shadow_instance_storage_buffer_->handle();
    SDL_BindGPUVertexStorageBuffers(pass, 0, &storage_buf, 1);

    // Track last state to skip redundant pushes/binds for back-to-back meshes.
    SDL_GPUTexture* last_tex = nullptr;
    int last_has_tex = -1;

    uint32_t base_instance = shadow_cascade_base_instance_[cascade];
    for (const auto& [batch_handle, instances] : cascade_batches) {
        if (instances.empty()) {
            continue;
        }

        Model* model = (batch_handle != INVALID_MODEL_HANDLE) ? model_manager_->get_model(batch_handle) : nullptr;
        if (!model) {
            base_instance += static_cast<uint32_t>(instances.size());
            continue;
        }

        uint32_t instance_count = static_cast<uint32_t>(instances.size());

        for (auto& mesh : model->meshes) {
            if (!mesh.cast_shadows) {
                continue; // skip leaves / transparent fluff
            }
            ENGINE_ASSERT(mesh.uploaded, "Mesh not uploaded before instanced shadow render");
            if (!mesh.uploaded) {
                continue;
            }
            if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) {
                continue;
            }

            int has_tex = (mesh.has_texture && mesh.texture) ? 1 : 0;
            if (has_tex != last_has_tex) {
                struct {
                    int hasTexture;
                    float _pad[3];
                } shadow_frag = {};
                shadow_frag.hasTexture = has_tex;
                SDL_PushGPUFragmentUniformData(cmd, 0, &shadow_frag, sizeof(shadow_frag));
                last_has_tex = has_tex;
            }

            if (has_tex && default_sampler_) {
                SDL_GPUTexture* th = mesh.texture->handle();
                if (th != last_tex) {
                    SDL_GPUTextureSamplerBinding tb = {th, default_sampler_};
                    SDL_BindGPUFragmentSamplers(pass, 0, &tb, 1);
                    last_tex = th;
                }
            } else if (default_sampler_ && dummy_white_texture_ && last_tex != dummy_white_texture_->handle()) {
                SDL_GPUTextureSamplerBinding dummy = {dummy_white_texture_->handle(), default_sampler_};
                SDL_BindGPUFragmentSamplers(pass, 0, &dummy, 1);
                last_tex = dummy_white_texture_->handle();
            }

            mesh.bind_buffers(pass);
            if (collect_stats_) {
                render_stats_.draw_calls++;
                render_stats_.triangle_count += mesh.index_count() / 3 * instance_count;
            }
            SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), instance_count, 0, 0, base_instance);
        }

        base_instance += instance_count;
    }
}

void SceneRenderer::render_skinned_model_command(const SkinnedModelCommand& cmd, const CameraState& camera) {
    // Standalone path (used by non-batched callers): binds pipeline + shadows itself
    if (!main_render_pass_) {
        return;
    }
    gpu::GPUPipeline* pipeline = pipeline_registry_->get_skinned_model_pipeline();
    if (!pipeline) {
        return;
    }
    pipeline->bind(main_render_pass_);
    bind_shadow_data(main_render_pass_, context_->current_command_buffer(), 1);
    bind_cluster_lights(main_render_pass_, context_->current_command_buffer(), 2);
    SDL_GPUTexture* unused_tex = nullptr;
    int unused_has = -1;
    render_skinned_model_command_inner(cmd, camera, unused_tex, unused_has);
}

void SceneRenderer::render_skinned_model_command_inner(const SkinnedModelCommand& cmd, const CameraState& camera,
                                                       SDL_GPUTexture*& last_texture, int& last_has_texture) {
    Model* model = resolve_model(*model_manager_, cmd.model_handle, cmd.model_name);
    if (!model || !main_render_pass_) {
        return;
    }

    SDL_GPUCommandBuffer* gpu_cmd = context_->current_command_buffer();
    if (!gpu_cmd) {
        return;
    }

    const glm::mat4& model_mat = cmd.transform;

    gpu::ModelTransformUniforms transform_uniforms = {};
    transform_uniforms.model = model_mat;
    transform_uniforms.view = camera.view;
    transform_uniforms.projection = camera.projection;
    transform_uniforms.cameraPos = camera.position;
    transform_uniforms.normalMatrix = compute_normal_matrix(model_mat);
    transform_uniforms.useSkinning = 1;

    bool fog_active = frame_fog_active_;

    const GraphicsSettings& gfx_skinned = graphics_ ? *graphics_ : default_graphics_;
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
    lighting_uniforms.ambientStrength = gfx_skinned.ambient_strength;
    lighting_uniforms.sunIntensity = gfx_skinned.sun_intensity;

    SDL_PushGPUVertexUniformData(gpu_cmd, 0, &transform_uniforms, sizeof(transform_uniforms));
    SDL_PushGPUVertexUniformData(gpu_cmd, 1, cmd.bone_matrices->data(),
                                 mmo::engine::animation::MAX_BONES * sizeof(glm::mat4));

    for (auto& mesh : model->meshes) {
        if (mesh.shadow_only) {
            continue;
        }
        ENGINE_ASSERT(mesh.uploaded, "Mesh not uploaded before skinned render");
        if (!mesh.uploaded) {
            continue;
        }

        if (!mesh.vertex_buffer || !mesh.index_buffer) {
            continue;
        }
        if (mesh.index_count() == 0) {
            continue;
        }

        int has_tex = (mesh.has_texture && mesh.texture) ? 1 : 0;
        bool has_nm = mesh.material.normal_texture != nullptr;
        if (has_tex != last_has_texture || lighting_uniforms.hasNormalMap != (has_nm ? 1 : 0) ||
            lighting_uniforms.normalScale != mesh.material.normal_scale) {
            lighting_uniforms.hasTexture = has_tex;
            lighting_uniforms.hasNormalMap = has_nm ? 1 : 0;
            lighting_uniforms.normalScale = mesh.material.normal_scale;
            SDL_PushGPUFragmentUniformData(gpu_cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));
            last_has_texture = has_tex;
        }

        if (mesh.has_texture && mesh.texture && default_sampler_) {
            SDL_GPUTexture* tex_handle = mesh.texture->handle();
            if (tex_handle != last_texture) {
                SDL_GPUTextureSamplerBinding tex_binding = {tex_handle, default_sampler_};
                SDL_BindGPUFragmentSamplers(main_render_pass_, 0, &tex_binding, 1);
                last_texture = tex_handle;
            }
        }

        if (default_sampler_) {
            SDL_GPUTexture* nm_tex = has_nm ? mesh.material.normal_texture->handle()
                                            : (default_normal_texture_ ? default_normal_texture_->handle() : nullptr);
            if (nm_tex) {
                SDL_GPUTextureSamplerBinding nm_binding = {nm_tex, default_sampler_};
                SDL_BindGPUFragmentSamplers(main_render_pass_, 5, &nm_binding, 1);
            }
        }

        mesh.bind_buffers(main_render_pass_);
        if (collect_stats_) {
            render_stats_.draw_calls++;
            render_stats_.triangle_count += mesh.index_count() / 3;
        }
        SDL_DrawGPUIndexedPrimitives(main_render_pass_, mesh.index_count(), 1, 0, 0, 0);
    }
}

// ============================================================================
// UI Rendering
// ============================================================================

void SceneRenderer::render_ui_commands(const UIScene& ui_scene, const CameraState& camera) {
    for (const auto& cmd : ui_scene.commands()) {
        std::visit(
            [this, &camera](const auto& data) {
                using T = std::decay_t<decltype(data)>;

                if constexpr (std::is_same_v<T, FilledRectCommand>) {
                    ui_->draw_filled_rect(data.x, data.y, data.w, data.h, data.color);
                } else if constexpr (std::is_same_v<T, RectOutlineCommand>) {
                    ui_->draw_rect_outline(data.x, data.y, data.w, data.h, data.color, data.line_width);
                } else if constexpr (std::is_same_v<T, CircleCommand>) {
                    ui_->draw_circle(data.x, data.y, data.radius, data.color, data.segments);
                } else if constexpr (std::is_same_v<T, CircleOutlineCommand>) {
                    ui_->draw_circle_outline(data.x, data.y, data.radius, data.color, data.line_width, data.segments);
                } else if constexpr (std::is_same_v<T, LineCommand>) {
                    ui_->draw_line(data.x1, data.y1, data.x2, data.y2, data.color, data.line_width);
                } else if constexpr (std::is_same_v<T, TextCommand>) {
                    ui_->draw_text(data.text, data.x, data.y, data.color, data.scale);
                } else if constexpr (std::is_same_v<T, ButtonCommand>) {
                    ui_->draw_button(data.x, data.y, data.w, data.h, data.label, data.color, data.selected);
                }
            },
            cmd.data);
    }
}

void SceneRenderer::draw_billboard_3d(const Billboard3DCommand& cmd, const CameraState& camera) {
    glm::vec4 world_pos(cmd.world_x, cmd.world_y, cmd.world_z, 1.0f);
    glm::vec4 clip_pos = camera.projection * camera.view * world_pos;
    if (clip_pos.w <= 0.01f) {
        return;
    }

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
    if (lines.empty() || !main_render_pass_) {
        return;
    }

    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) {
        return;
    }

    gpu::GPUPipeline* pipeline = pipeline_registry_->get_grid_pipeline();
    if (!pipeline) {
        return;
    }

    const size_t line_count = lines.size();
    const size_t vertex_count = line_count * 2;
    // Grid vertex format: float3 position + float4 color = 7 floats per vertex
    const size_t vertex_stride = sizeof(float) * 7;
    const size_t buffer_size = vertex_count * vertex_stride;

    // Recreate buffer if too small
    if (!debug_line_vertex_buffer_ || debug_line_buffer_capacity_ < line_count) {
        // Round up to avoid frequent reallocations
        size_t new_capacity = std::max(line_count, static_cast<size_t>(256));
        if (debug_line_buffer_capacity_ > 0) {
            new_capacity = std::max(new_capacity, debug_line_buffer_capacity_ * 2);
        }
        debug_line_vertex_buffer_ = gpu::GPUBuffer::create_dynamic(context_->device(), gpu::GPUBuffer::Type::Vertex,
                                                                   new_capacity * 2 * vertex_stride);
        if (!debug_line_vertex_buffer_) {
            return;
        }
        debug_line_buffer_capacity_ = new_capacity;
    }

    // Build vertex data: position(3) + color(4) per vertex
    // Unpack RGBA packed uint32 to float4. Reuse scratch buffer to avoid per-frame alloc.
    debug_line_scratch_.resize(vertex_count * 7);
    float* dst = debug_line_scratch_.data();
    for (const auto& line : lines) {
        float r = static_cast<float>((line.color >> 24) & 0xFF) / 255.0f;
        float g = static_cast<float>((line.color >> 16) & 0xFF) / 255.0f;
        float b = static_cast<float>((line.color >> 8) & 0xFF) / 255.0f;
        float a = static_cast<float>((line.color >> 0) & 0xFF) / 255.0f;

        // Start vertex
        *dst++ = line.start.x;
        *dst++ = line.start.y;
        *dst++ = line.start.z;
        *dst++ = r;
        *dst++ = g;
        *dst++ = b;
        *dst++ = a;

        // End vertex
        *dst++ = line.end.x;
        *dst++ = line.end.y;
        *dst++ = line.end.z;
        *dst++ = r;
        *dst++ = g;
        *dst++ = b;
        *dst++ = a;
    }

    // Upload vertex data
    debug_line_vertex_buffer_->update(cmd, debug_line_scratch_.data(), buffer_size);

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
// Shadow Rendering
// ============================================================================

void SceneRenderer::render_shadow_passes(const RenderScene& scene, const CameraState& camera) {
    SDL_GPUCommandBuffer* cmd = context_->current_command_buffer();
    if (!cmd) {
        return;
    }

    ENGINE_GPU_SCOPE(cmd, "ShadowPass");
    ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, "ShadowPass");

    // Note: shadow_map_->update() was already called in render_frame() so that the
    // per-cascade light frustums could be used during build_instance_batches.

    // Pre-resolve Model* + world bounds once per skinned entity (outside the cascade loop).
    // Avoids N cascades * M entities hash lookups per frame.
    auto& skinned_scratch = shadow_skinned_scratch_;
    skinned_scratch.clear();
    skinned_scratch.reserve(scene.skinned_commands().size());
    for (const auto& skinned_cmd : scene.skinned_commands()) {
        Model* model = resolve_model(*model_manager_, skinned_cmd.model_handle, skinned_cmd.model_name);
        if (!model) {
            continue;
        }
        const glm::mat4& t = skinned_cmd.transform;
        glm::vec3 wc = glm::vec3(t * glm::vec4(model->bounding_center, 1.0f));
        float wr = model->bounding_half_diag * max_scale_factor(t);
        skinned_scratch.push_back({&skinned_cmd, model, wc, wr});
    }

    // Pre-resolve Model* for non-instanced static commands too.
    auto& static_scratch = shadow_static_scratch_;
    static_scratch.clear();
    static_scratch.reserve(non_instanced_commands_.size());
    for (const auto* model_cmd : non_instanced_commands_) {
        Model* model = resolve_model(*model_manager_, model_cmd->model_handle, model_cmd->model_name);
        if (model) {
            static_scratch.push_back({model_cmd, model});
        }
    }

    SDL_GPUTextureSamplerBinding dummy_binding = {};
    if (default_sampler_ && dummy_white_texture_) {
        dummy_binding = {dummy_white_texture_->handle(), default_sampler_};
    }

    struct ShadowFragConst {
        int hasTexture;
        float _pad[3];
    };
    const ShadowFragConst frag_notex = {0, {}};
    const ShadowFragConst frag_tex = {1, {}};

    auto cascade_label = [](int c) -> const char* {
        switch (c & 3) {
            case 0:
                return "ShadowPass:Cascade0";
            case 1:
                return "ShadowPass:Cascade1";
            case 2:
                return "ShadowPass:Cascade2";
            default:
                return "ShadowPass:Cascade3";
        }
    };

    for (int cascade = 0; cascade < shadow_map_->active_cascades(); ++cascade) {
        const char* cascade_name = cascade_label(cascade);
        ENGINE_GPU_SCOPE(cmd, cascade_name);
        ENGINE_GPU_TIMER(gpu_timer_pool_, cmd, cascade_name);
        SDL_GPURenderPass* shadow_pass = shadow_map_->begin_shadow_pass(cmd, cascade);
        if (!shadow_pass) {
            continue;
        }

        const auto& cascade_data = shadow_map_->cascades()[cascade];

        // All cascades now draw entities — trees use a 20-triangle canopy proxy
        // (instead of 2000 leaf quads) so even the far cascade cost is trivial.
        // Per-cascade frustum culling below still skips anything outside the
        // cascade's light frustum.

        // Render static models into shadow map (instanced)
        render_instanced_shadow_models(shadow_pass, cmd, cascade_data.light_view_projection, cascade);

        // Render non-instanced static models into shadow map.
        // Per-cascade frustum cull so near cascades don't draw distant casters.
        if (!static_scratch.empty()) {
            auto* shadow_pipeline = pipeline_registry_->get_shadow_model_pipeline();
            if (shadow_pipeline) {
                shadow_pipeline->bind(shadow_pass);
                SDL_GPUTexture* last_tex = nullptr;
                int last_has_tex = -1;
                for (const auto& rs : static_scratch) {
                    // Per-cascade cull: skip if not in this cascade's light frustum.
                    const glm::mat4& t = rs.cmd->transform;
                    glm::vec3 wc = glm::vec3(t * glm::vec4(rs.model->bounding_center, 1.0f));
                    float wr = rs.model->bounding_half_diag * max_scale_factor(t);
                    if (!shadow_cascade_frustums_[cascade].intersects_sphere(wc, wr)) {
                        continue;
                    }

                    gpu::ShadowTransformUniforms shadow_uniforms = {};
                    shadow_uniforms.lightViewProjection = cascade_data.light_view_projection;
                    shadow_uniforms.model = rs.cmd->transform;
                    SDL_PushGPUVertexUniformData(cmd, 0, &shadow_uniforms, sizeof(shadow_uniforms));

                    for (auto& mesh : rs.model->meshes) {
                        if (!mesh.cast_shadows) {
                            continue;
                        }
                        ENGINE_ASSERT(mesh.uploaded, "Mesh not uploaded before non-instanced shadow render");
                        if (!mesh.uploaded) {
                            continue;
                        }
                        if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) {
                            continue;
                        }

                        int has_tex = (mesh.has_texture && mesh.texture) ? 1 : 0;
                        if (has_tex != last_has_tex) {
                            SDL_PushGPUFragmentUniformData(cmd, 0, has_tex ? &frag_tex : &frag_notex,
                                                           sizeof(ShadowFragConst));
                            last_has_tex = has_tex;
                        }

                        if (has_tex && default_sampler_) {
                            SDL_GPUTexture* th = mesh.texture->handle();
                            if (th != last_tex) {
                                SDL_GPUTextureSamplerBinding tb = {th, default_sampler_};
                                SDL_BindGPUFragmentSamplers(shadow_pass, 0, &tb, 1);
                                last_tex = th;
                            }
                        } else if (default_sampler_ && dummy_white_texture_ && last_tex != dummy_binding.texture) {
                            SDL_BindGPUFragmentSamplers(shadow_pass, 0, &dummy_binding, 1);
                            last_tex = dummy_binding.texture;
                        }

                        mesh.bind_buffers(shadow_pass);
                        if (collect_stats_) {
                            render_stats_.draw_calls++;
                            render_stats_.triangle_count += mesh.index_count() / 3;
                        }
                        SDL_DrawGPUIndexedPrimitives(shadow_pass, mesh.index_count(), 1, 0, 0, 0);
                    }
                }
            }
        }

        // Render skinned models into shadow map.
        if (!skinned_scratch.empty()) {
            auto* shadow_skinned_pipeline = pipeline_registry_->get_shadow_skinned_model_pipeline();
            if (shadow_skinned_pipeline) {
                shadow_skinned_pipeline->bind(shadow_pass);

                SDL_GPUTexture* last_tex = nullptr;
                int last_has_tex = -1;
                for (const auto& rs : skinned_scratch) {
                    // Per-cascade cull: skip if not in this cascade's light frustum.
                    if (!shadow_cascade_frustums_[cascade].intersects_sphere(rs.world_center, rs.world_radius)) {
                        continue;
                    }

                    const SkinnedModelCommand* data = rs.cmd;
                    Model* model = rs.model;

                    gpu::ShadowTransformUniforms shadow_uniforms = {};
                    shadow_uniforms.lightViewProjection = cascade_data.light_view_projection;
                    shadow_uniforms.model = data->transform;
                    SDL_PushGPUVertexUniformData(cmd, 0, &shadow_uniforms, sizeof(shadow_uniforms));
                    SDL_PushGPUVertexUniformData(cmd, 1, data->bone_matrices->data(),
                                                 mmo::engine::animation::MAX_BONES * sizeof(glm::mat4));

                    for (auto& mesh : model->meshes) {
                        if (!mesh.cast_shadows) {
                            continue;
                        }
                        ENGINE_ASSERT(mesh.uploaded, "Mesh not uploaded before skinned shadow render");
                        if (!mesh.uploaded) {
                            continue;
                        }
                        if (!mesh.vertex_buffer || !mesh.index_buffer || mesh.index_count() == 0) {
                            continue;
                        }

                        int has_tex = (mesh.has_texture && mesh.texture) ? 1 : 0;
                        if (has_tex != last_has_tex) {
                            SDL_PushGPUFragmentUniformData(cmd, 0, has_tex ? &frag_tex : &frag_notex,
                                                           sizeof(ShadowFragConst));
                            last_has_tex = has_tex;
                        }

                        if (has_tex && default_sampler_) {
                            SDL_GPUTexture* th = mesh.texture->handle();
                            if (th != last_tex) {
                                SDL_GPUTextureSamplerBinding tb = {th, default_sampler_};
                                SDL_BindGPUFragmentSamplers(shadow_pass, 0, &tb, 1);
                                last_tex = th;
                            }
                        } else if (default_sampler_ && dummy_white_texture_ && last_tex != dummy_binding.texture) {
                            SDL_BindGPUFragmentSamplers(shadow_pass, 0, &dummy_binding, 1);
                            last_tex = dummy_binding.texture;
                        }

                        mesh.bind_buffers(shadow_pass);
                        if (collect_stats_) {
                            render_stats_.draw_calls++;
                            render_stats_.triangle_count += mesh.index_count() / 3;
                        }
                        SDL_DrawGPUIndexedPrimitives(shadow_pass, mesh.index_count(), 1, 0, 0, 0);
                    }
                }
            }
        }

        // Render terrain into shadow map (push no-texture fragment state).
        // Terrain is the last thing drawn in this cascade — set no-texture state once.
        SDL_PushGPUFragmentUniformData(cmd, 0, &frag_notex, sizeof(ShadowFragConst));
        if (default_sampler_ && dummy_white_texture_) {
            SDL_BindGPUFragmentSamplers(shadow_pass, 0, &dummy_binding, 1);
        }
        terrain_->render_shadow(shadow_pass, cmd, cascade_data.light_view_projection,
                                &shadow_cascade_frustums_[cascade]);

        shadow_map_->end_shadow_pass();
    }
}


void SceneRenderer::bind_shadow_data(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, int sampler_slot) {
    if (!shadow_map_->is_ready()) {
        return;
    }

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

void SceneRenderer::bind_cluster_lights(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, int uniform_slot) {
    if (!cluster_grid_ready_) {
        return;
    }
    SDL_GPUBuffer* bufs[3] = {
        cluster_grid_.light_data_buffer(),
        cluster_grid_.cluster_offsets_buffer(),
        cluster_grid_.light_indices_buffer(),
    };
    if (!bufs[0] || !bufs[1] || !bufs[2]) {
        return;
    }
    SDL_BindGPUFragmentStorageBuffers(pass, 0, bufs, 3);
    const auto& p = cluster_grid_.params();
    SDL_PushGPUFragmentUniformData(cmd, uniform_slot, &p, sizeof(p));
}

} // namespace mmo::engine::scene

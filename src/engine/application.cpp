#include "application.hpp"
#include "engine/core/asset/file_watcher.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/logger.hpp"
#include "engine/core/profiler.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/render/render_context.hpp"
#include "engine/scene/scene_renderer.hpp"
#include "engine/systems/camera_system.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_timer.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace mmo::engine {

using namespace mmo::engine::render;
using namespace mmo::engine::scene;
using namespace mmo::engine::systems;

Application::Application() = default;

Application::~Application() = default;

bool Application::init_engine() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        ENGINE_LOG_FATAL("app", "Failed to initialize SDL: {}", SDL_GetError());
        return false;
    }

    // getenv is single-threaded at engine boot, before any worker thread is spawned.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* env = std::getenv("MMO_HOT_RELOAD"); env && env[0] == '1') {
        core::asset::FileWatcher::instance().init();
        ENGINE_LOG_INFO("app", "Hot-reload enabled (MMO_HOT_RELOAD=1)");
    }
    return true;
}

void Application::run() {
    running_ = true;
    last_frame_time_ = SDL_GetTicks();
    fps_timer_ = last_frame_time_;

    while (running_) {
        uint64_t current_time = SDL_GetTicks();
        float dt = (current_time - last_frame_time_) / 1000.0f;
        last_frame_time_ = current_time;

        // Clamp delta time to avoid huge jumps
        dt = std::min(dt, 0.1f);

        // FPS counter
        frame_count_++;
        if (current_time - fps_timer_ >= 1000) {
            fps_ = static_cast<float>(frame_count_);
            frame_count_ = 0;
            fps_timer_ = current_time;
        }

        if (core::asset::FileWatcher::instance().is_initialized()) {
            core::asset::FileWatcher::instance().poll_main_thread();
        }

        // Process input - poll events, let subclass see them first
        {
            ENGINE_PROFILE_ZONE("App::poll_events");
            input_.reset_camera_deltas();
            input_.clear_menu_inputs();
            input_.clear_key_edges();
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running_ = false;
                    break;
                }
                // Let subclass consume the event (e.g. for ImGui)
                if (!on_event(event)) {
                    input_.process_event(event);
                }
            }
            if (!running_) {
                break;
            }
            input_.post_process_events();
        }

        {
            ENGINE_PROFILE_ZONE("App::update");
            on_update(dt);
        }
        {
            ENGINE_PROFILE_ZONE("App::render");
            on_render();
        }

        ENGINE_PROFILE_FRAME();
    }
}

void Application::shutdown_engine() {
    core::asset::FileWatcher::instance().shutdown();
    core::jobs::JobSystem::instance().shutdown();
    SDL_Quit();
}

// ========== Rendering facade ==========

bool Application::init_renderer(int width, int height, const std::string& title, float world_width,
                                float world_height) {
    context_ = std::make_unique<RenderContext>();
    if (!context_->init(width, height, title)) {
        return false;
    }
    context_->query_display_modes();

    scene_renderer_ = std::make_unique<SceneRenderer>();
    if (!scene_renderer_->init(*context_, world_width, world_height)) {
        return false;
    }

    if (auto* pr = scene_renderer_->pipeline_registry()) {
        pr->enable_hot_reload(core::asset::FileWatcher::instance());
    }
    scene_renderer_->models().enable_hot_reload(core::asset::FileWatcher::instance());

    camera_ = std::make_unique<CameraSystem>();

    // Auto-load settings from disk
    auto path = settings_file_path();
    if (!path.empty()) {
        graphics_settings_.load(path);
    }
    apply_all_graphics_settings(graphics_settings_);

    return true;
}

void Application::shutdown_renderer() {
    // Auto-save settings on shutdown
    auto path = settings_file_path();
    if (!path.empty()) {
        if (!graphics_settings_.save(path)) {
            ENGINE_LOG_ERROR("settings", "Failed to save settings to {}", path);
        }
    }

    camera_.reset();
    if (scene_renderer_) {
        scene_renderer_->shutdown();
    }
    if (context_) {
        context_->shutdown();
    }
    scene_renderer_.reset();
    context_.reset();
}

void Application::render_frame(RenderScene& scene, const UIScene& ui_scene, const CameraState& camera, float dt) {
    scene_renderer_->render_frame(scene, ui_scene, camera, dt);
}

void Application::set_heightmap(const Heightmap& heightmap) {
    scene_renderer_->set_heightmap(heightmap);
}

void Application::set_graphics_settings(const GraphicsSettings& settings) {
    scene_renderer_->set_graphics_settings(settings);
}

void Application::set_anisotropic_filter(int level) {
    scene_renderer_->set_anisotropic_filter(level);
}

void Application::set_vsync_mode(int mode) {
    scene_renderer_->set_vsync_mode(mode);
}

int Application::max_vsync_mode() const {
    return scene_renderer_->max_vsync_mode();
}

void Application::set_window_mode(int window_mode, int resolution_index) {
    context_->set_window_mode(window_mode, resolution_index);
}

std::vector<Application::DisplayMode> Application::available_resolutions() const {
    std::vector<DisplayMode> result;
    for (const auto& m : context_->available_resolutions()) {
        result.push_back({m.w, m.h});
    }
    return result;
}

ModelManager& Application::models() {
    return scene_renderer_->models();
}

float Application::get_terrain_height(float x, float z) {
    return scene_renderer_->get_terrain_height(x, z);
}

int Application::screen_width() const {
    return context_->width();
}

int Application::screen_height() const {
    return context_->height();
}

void Application::set_collect_render_stats(bool enabled) {
    scene_renderer_->set_collect_stats(enabled);
}

const RenderStats& Application::render_stats() const {
    return scene_renderer_->render_stats();
}

std::string Application::gpu_driver_name() const {
    return context_->device().driver_name();
}

void Application::apply_all_graphics_settings(const GraphicsSettings& settings) {
    graphics_settings_ = settings;
    set_graphics_settings(settings);
    set_anisotropic_filter(settings.anisotropic_filter);
    set_vsync_mode(settings.vsync_mode);
    set_window_mode(settings.window_mode, settings.resolution_index);
}

std::string Application::settings_file_path() const {
    char* pref_path = SDL_GetPrefPath("mmo4", "mmo4");
    if (!pref_path) {
        return {};
    }
    std::string result = std::string(pref_path) + "settings.cfg";
    SDL_free(pref_path);
    return result;
}

// ========== Camera facade ==========

CameraController& Application::camera() {
    return *camera_;
}

const CameraController& Application::camera() const {
    return *camera_;
}

} // namespace mmo::engine

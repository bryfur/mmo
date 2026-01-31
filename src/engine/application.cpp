#include "application.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_timer.h"
#include "engine/render/render_context.hpp"
#include "engine/scene/scene_renderer.hpp"
#include "engine/systems/camera_system.hpp"
#include <cstdint>
#include <iostream>
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
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return false;
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
        if (dt > 0.1f) dt = 0.1f;

        // FPS counter
        frame_count_++;
        if (current_time - fps_timer_ >= 1000) {
            fps_ = static_cast<float>(frame_count_);
            frame_count_ = 0;
            fps_timer_ = current_time;
        }

        // Process input
        if (!input_.process_events()) {
            running_ = false;
            break;
        }

        on_update(dt);
        on_render();
    }
}

void Application::shutdown_engine() {
    SDL_Quit();
}

// ========== Rendering facade ==========

bool Application::init_renderer(int width, int height, const std::string& title,
                                float world_width, float world_height) {
    context_ = std::make_unique<RenderContext>();
    if (!context_->init(width, height, title)) return false;

    scene_renderer_ = std::make_unique<SceneRenderer>();
    if (!scene_renderer_->init(*context_, world_width, world_height)) return false;

    camera_ = std::make_unique<CameraSystem>();

    return true;
}

void Application::shutdown_renderer() {
    camera_.reset();
    if (scene_renderer_) scene_renderer_->shutdown();
    if (context_) context_->shutdown();
    scene_renderer_.reset();
    context_.reset();
}

void Application::render_frame(const RenderScene& scene, const UIScene& ui_scene,
                               const CameraState& camera, float dt) {
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

// ========== Camera facade ==========

CameraController& Application::camera() {
    return *camera_;
}

const CameraController& Application::camera() const {
    return *camera_;
}

} // namespace mmo::engine

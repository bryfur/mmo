#include "application.hpp"
#include <SDL3/SDL.h>
#include <iostream>

namespace mmo::engine {

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

} // namespace mmo::engine

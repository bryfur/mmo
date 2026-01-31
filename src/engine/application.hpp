#pragma once

#include "engine/input_handler.hpp"
#include <SDL3/SDL.h>
#include <cstdint>

namespace mmo::engine {

/**
 * Base application class that owns the SDL lifecycle, main loop,
 * frame timing, and input handling.
 *
 * Game-specific subclasses override on_init/on_update/on_render/on_shutdown.
 */
class Application {
public:
    Application();
    virtual ~Application();

    /** Initialize SDL subsystems. Call before on_init(). */
    bool init_engine();

    /** Run the main loop until quit() is called. */
    void run();

    /** Shut down SDL. Call after on_shutdown(). */
    void shutdown_engine();

    /** Request the main loop to stop. */
    void quit() { running_ = false; }

    float fps() const { return fps_; }

protected:
    /** Game-specific initialization (renderer, network, etc). */
    virtual bool on_init() = 0;

    /** Game-specific shutdown. */
    virtual void on_shutdown() {}

    /** Called once per frame with delta time in seconds. */
    virtual void on_update(float dt) = 0;

    /** Called once per frame after on_update. */
    virtual void on_render() = 0;

    InputHandler& input() { return input_; }
    const InputHandler& input() const { return input_; }

private:
    InputHandler input_;
    bool running_ = false;
    uint64_t last_frame_time_ = 0;
    float fps_ = 0.0f;
    int frame_count_ = 0;
    uint64_t fps_timer_ = 0;
};

} // namespace mmo::engine

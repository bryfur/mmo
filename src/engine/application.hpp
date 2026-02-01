#pragma once

#include "engine/input_handler.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <string>

#include "engine/render_stats.hpp"

namespace mmo::engine {

namespace render { class RenderContext; }
namespace scene {
    class SceneRenderer;
    class RenderScene;
    class UIScene;
    struct CameraState;
}
namespace systems {
    class CameraController;
    class CameraSystem;
}

class ModelManager;
struct GraphicsSettings;
struct Heightmap;

/**
 * Base application class that owns the SDL lifecycle, main loop,
 * frame timing, input handling, and core engine subsystems.
 *
 * Game-specific subclasses override on_init/on_update/on_render/on_shutdown
 * and interact with the renderer and camera through the protected facade.
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

    // ========== Rendering facade ==========

    /** Initialize the rendering subsystems (window, GPU, scene renderer, camera). */
    bool init_renderer(int width, int height, const std::string& title,
                       float world_width = 8000.0f, float world_height = 8000.0f);

    /** Shut down all rendering subsystems. */
    void shutdown_renderer();

    /** Render a complete frame from scene descriptions. */
    void render_frame(const scene::RenderScene& scene, const scene::UIScene& ui_scene,
                      const scene::CameraState& camera, float dt);

    void set_heightmap(const Heightmap& heightmap);
    void set_graphics_settings(const GraphicsSettings& settings);
    void set_anisotropic_filter(int level);
    void set_vsync_mode(int mode);
    void set_fullscreen(bool exclusive);

    ModelManager& models();
    float get_terrain_height(float x, float z);

    int screen_width() const;
    int screen_height() const;

    void set_collect_render_stats(bool enabled);
    const RenderStats& render_stats() const;
    std::string gpu_driver_name() const;

    // ========== Camera facade ==========

    systems::CameraController& camera();
    const systems::CameraController& camera() const;

private:
    InputHandler input_;
    bool running_ = false;
    uint64_t last_frame_time_ = 0;
    float fps_ = 0.0f;
    int frame_count_ = 0;
    uint64_t fps_timer_ = 0;

    // Owned engine subsystems (opaque to subclasses)
    std::unique_ptr<render::RenderContext> context_;
    std::unique_ptr<scene::SceneRenderer> scene_renderer_;
    std::unique_ptr<systems::CameraSystem> camera_;
};

} // namespace mmo::engine

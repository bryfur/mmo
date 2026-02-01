#pragma once

#include "engine/application.hpp"
#include "engine/scene/render_scene.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/scene/camera_state.hpp"
#include "server/game_config.hpp"
#include "client/ecs/components.hpp"
#include "editor_camera.hpp"
#include <entt/entt.hpp>
#include <string>

namespace mmo::editor {

/**
 * Editor application for visually editing the MMO world.
 * Loads entities from data files, allows manipulation, and saves changes back.
 */
class EditorApplication : public engine::Application {
public:
    EditorApplication();
    ~EditorApplication();

    bool init();
    void shutdown();

protected:
    bool on_init() override;
    void on_shutdown() override;
    void on_update(float dt) override;
    void on_render() override;

private:
    // Asset loading
    bool load_models(const std::string& assets_path);

    // Entity loading
    void load_entities_from_config();
    void spawn_building(const server::BuildingConfig& config);
    void spawn_town_npc(const server::TownNPCConfig& config);

    // Rendering
    void build_render_scene();
    void add_entity_to_scene(entt::entity entity);
    engine::scene::CameraState get_camera_state() const;

    // Input handling
    void handle_camera_input(float dt);

    // ECS registry and config
    entt::registry registry_;
    server::GameConfig config_;

    // Camera
    EditorCamera camera_;

    // Scene descriptions (built each frame)
    engine::scene::RenderScene render_scene_;
    engine::scene::UIScene ui_scene_;

    // Editor state
    bool show_help_ = false;
};

} // namespace mmo::editor

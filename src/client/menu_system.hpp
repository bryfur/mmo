#pragma once

#include "menu_types.hpp"
#include "controls_settings.hpp"
#include "engine/graphics_settings.hpp"
#include "engine/input_handler.hpp"
#include "engine/scene/ui_scene.hpp"
#include <functional>
#include <vector>

namespace mmo::client {

class MenuSystem {
public:
    MenuSystem(engine::InputHandler& input, std::function<void()> on_quit);

    void update(float dt);
    void build_ui(engine::scene::UIScene& ui, float screen_w, float screen_h);

    bool is_open() const { return menu_open_; }

    const engine::GraphicsSettings& graphics_settings() const { return graphics_settings_; }
    const ControlsSettings& controls_settings() const { return controls_settings_; }

private:
    void init_menu_items();
    void init_main_menu();
    void init_controls_menu();
    void init_graphics_menu();

    engine::InputHandler& input_;
    std::function<void()> on_quit_;

    bool menu_open_ = false;
    int menu_selected_index_ = 0;
    MenuPage current_menu_page_ = MenuPage::Main;
    std::vector<MenuItem> menu_items_;

    engine::GraphicsSettings graphics_settings_;
    ControlsSettings controls_settings_;

    int prev_menu_selected_ = -1;
    float menu_highlight_progress_ = 1.0f;
};

} // namespace mmo::client

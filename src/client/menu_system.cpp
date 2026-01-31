#include "menu_system.hpp"
#include "client/menu_types.hpp"
#include "engine/input_handler.hpp"
#include "engine/render_constants.hpp"
#include "engine/scene/ui_scene.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

namespace mmo::client {

using namespace mmo::engine;
using namespace mmo::engine::scene;

MenuSystem::MenuSystem(InputHandler& input, std::function<void()> on_quit)
    : input_(input), on_quit_(std::move(on_quit)) {
    init_menu_items();
}

void MenuSystem::init_menu_items() {
    current_menu_page_ = MenuPage::Main;
    init_main_menu();
}

void MenuSystem::init_main_menu() {
    menu_items_.clear();
    menu_selected_index_ = 0;

    MenuItem controls_item;
    controls_item.label = "Controls";
    controls_item.type = MenuItemType::Submenu;
    controls_item.target_page = MenuPage::Controls;
    menu_items_.push_back(controls_item);

    MenuItem graphics_item;
    graphics_item.label = "Graphics";
    graphics_item.type = MenuItemType::Submenu;
    graphics_item.target_page = MenuPage::Graphics;
    menu_items_.push_back(graphics_item);

    MenuItem resume_item;
    resume_item.label = "Resume Game";
    resume_item.type = MenuItemType::Button;
    resume_item.action = [this]() {
        menu_open_ = false;
        input_.set_game_input_enabled(true);
    };
    menu_items_.push_back(resume_item);

    MenuItem quit_item;
    quit_item.label = "Quit to Desktop";
    quit_item.type = MenuItemType::Button;
    quit_item.action = on_quit_;
    menu_items_.push_back(quit_item);
}

void MenuSystem::init_controls_menu() {
    menu_items_.clear();
    menu_selected_index_ = 0;

    MenuItem mouse_sens;
    mouse_sens.label = "Mouse Sensitivity";
    mouse_sens.type = MenuItemType::FloatSlider;
    mouse_sens.float_value = &controls_settings_.mouse_sensitivity;
    mouse_sens.float_min = 0.05f;
    mouse_sens.float_max = 1.0f;
    mouse_sens.float_step = 0.05f;
    menu_items_.push_back(mouse_sens);

    MenuItem ctrl_sens;
    ctrl_sens.label = "Controller Sensitivity";
    ctrl_sens.type = MenuItemType::FloatSlider;
    ctrl_sens.float_value = &controls_settings_.controller_sensitivity;
    ctrl_sens.float_min = 0.5f;
    ctrl_sens.float_max = 5.0f;
    ctrl_sens.float_step = 0.25f;
    menu_items_.push_back(ctrl_sens);

    MenuItem invert_x;
    invert_x.label = "Invert Camera X";
    invert_x.type = MenuItemType::Toggle;
    invert_x.toggle_value = &controls_settings_.invert_camera_x;
    menu_items_.push_back(invert_x);

    MenuItem invert_y;
    invert_y.label = "Invert Camera Y";
    invert_y.type = MenuItemType::Toggle;
    invert_y.toggle_value = &controls_settings_.invert_camera_y;
    menu_items_.push_back(invert_y);

    MenuItem back_item;
    back_item.label = "< Back";
    back_item.type = MenuItemType::Submenu;
    back_item.target_page = MenuPage::Main;
    menu_items_.push_back(back_item);
}

void MenuSystem::init_graphics_menu() {
    menu_items_.clear();
    menu_selected_index_ = 0;

    MenuItem fog;
    fog.label = "Fog";
    fog.type = MenuItemType::Toggle;
    fog.toggle_value = &graphics_settings_.fog_enabled;
    menu_items_.push_back(fog);

    MenuItem grass;
    grass.label = "Grass";
    grass.type = MenuItemType::Toggle;
    grass.toggle_value = &graphics_settings_.grass_enabled;
    menu_items_.push_back(grass);

    MenuItem skybox;
    skybox.label = "Skybox";
    skybox.type = MenuItemType::Toggle;
    skybox.toggle_value = &graphics_settings_.skybox_enabled;
    menu_items_.push_back(skybox);

    MenuItem mountains;
    mountains.label = "Mountains";
    mountains.type = MenuItemType::Toggle;
    mountains.toggle_value = &graphics_settings_.mountains_enabled;
    menu_items_.push_back(mountains);

    MenuItem trees;
    trees.label = "Trees";
    trees.type = MenuItemType::Toggle;
    trees.toggle_value = &graphics_settings_.trees_enabled;
    menu_items_.push_back(trees);

    MenuItem rocks;
    rocks.label = "Rocks";
    rocks.type = MenuItemType::Toggle;
    rocks.toggle_value = &graphics_settings_.rocks_enabled;
    menu_items_.push_back(rocks);

    MenuItem draw_dist;
    draw_dist.label = "Draw Distance";
    draw_dist.type = MenuItemType::Slider;
    draw_dist.slider_value = &graphics_settings_.draw_distance;
    draw_dist.slider_min = 0;
    draw_dist.slider_max = 4;
    draw_dist.slider_labels = {"500", "1000", "2000", "4000", "8000"};
    menu_items_.push_back(draw_dist);

    MenuItem frustum_cull;
    frustum_cull.label = "Frustum Culling";
    frustum_cull.type = MenuItemType::Toggle;
    frustum_cull.toggle_value = &graphics_settings_.frustum_culling;
    menu_items_.push_back(frustum_cull);

    MenuItem aniso;
    aniso.label = "Anisotropic Filter";
    aniso.type = MenuItemType::Slider;
    aniso.slider_value = &graphics_settings_.anisotropic_filter;
    aniso.slider_min = 0;
    aniso.slider_max = 4;
    aniso.slider_labels = {"Off", "2x", "4x", "8x", "16x"};
    menu_items_.push_back(aniso);

    MenuItem vsync;
    vsync.label = "VSync";
    vsync.type = MenuItemType::Slider;
    vsync.slider_value = &graphics_settings_.vsync_mode;
    vsync.slider_min = 0;
    vsync.slider_max = 2;
    vsync.slider_labels = {"Off", "Double Buffer", "Triple Buffer"};
    menu_items_.push_back(vsync);

    MenuItem fps_counter;
    fps_counter.label = "Show FPS";
    fps_counter.type = MenuItemType::Toggle;
    fps_counter.toggle_value = &graphics_settings_.show_fps;
    menu_items_.push_back(fps_counter);

    MenuItem back_item;
    back_item.label = "< Back";
    back_item.type = MenuItemType::Submenu;
    back_item.target_page = MenuPage::Main;
    menu_items_.push_back(back_item);
}

void MenuSystem::update(float dt) {
    menu_highlight_progress_ = std::min(1.0f, menu_highlight_progress_ + dt * 8.0f);

    if (menu_selected_index_ != prev_menu_selected_) {
        menu_highlight_progress_ = 0.0f;
        prev_menu_selected_ = menu_selected_index_;
    }

    if (input_.menu_toggle_pressed()) {
        if (current_menu_page_ != MenuPage::Main) {
            current_menu_page_ = MenuPage::Main;
            init_main_menu();
        } else {
            menu_open_ = !menu_open_;
            input_.set_game_input_enabled(!menu_open_);
        }
        input_.clear_menu_inputs();
        return;
    }

    if (!menu_open_) return;

    if (input_.menu_up_pressed()) {
        menu_selected_index_ = (menu_selected_index_ - 1 + static_cast<int>(menu_items_.size())) % static_cast<int>(menu_items_.size());
    }
    if (input_.menu_down_pressed()) {
        menu_selected_index_ = (menu_selected_index_ + 1) % static_cast<int>(menu_items_.size());
    }

    MenuItem& item = menu_items_[menu_selected_index_];
    if (item.type == MenuItemType::Toggle) {
        if (input_.menu_select_pressed() || input_.menu_left_pressed() || input_.menu_right_pressed()) {
            if (item.toggle_value) {
                *item.toggle_value = !*item.toggle_value;
            }
        }
    } else if (item.type == MenuItemType::Slider) {
        if (item.slider_value) {
            if (input_.menu_left_pressed()) {
                *item.slider_value = std::max(item.slider_min, *item.slider_value - 1);
            }
            if (input_.menu_right_pressed()) {
                *item.slider_value = std::min(item.slider_max, *item.slider_value + 1);
            }
        }
    } else if (item.type == MenuItemType::FloatSlider) {
        if (item.float_value) {
            if (input_.menu_left_pressed()) {
                *item.float_value = std::max(item.float_min, *item.float_value - item.float_step);
            }
            if (input_.menu_right_pressed()) {
                *item.float_value = std::min(item.float_max, *item.float_value + item.float_step);
            }
        }
    } else if (item.type == MenuItemType::Button) {
        if (input_.menu_select_pressed()) {
            if (item.action) {
                item.action();
            }
        }
    } else if (item.type == MenuItemType::Submenu) {
        if (input_.menu_select_pressed()) {
            current_menu_page_ = item.target_page;
            switch (item.target_page) {
                case MenuPage::Main:
                    init_main_menu();
                    break;
                case MenuPage::Controls:
                    init_controls_menu();
                    break;
                case MenuPage::Graphics:
                    init_graphics_menu();
                    break;
            }
        }
    }

    input_.clear_menu_inputs();
}

void MenuSystem::build_ui(UIScene& ui, float screen_w, float screen_h) {
    if (!menu_open_) return;

    float panel_w = 550.0f;
    float panel_h = 70.0f + menu_items_.size() * 50.0f + 50.0f;
    float panel_x = (screen_w - panel_w) / 2.0f;
    float panel_y = (screen_h - panel_h) / 2.0f;

    ui.add_filled_rect(panel_x, panel_y, panel_w, panel_h, ui_colors::MENU_BG);
    ui.add_rect_outline(panel_x, panel_y, panel_w, panel_h, ui_colors::WHITE, 2.0f);

    const char* title = "SETTINGS";
    switch (current_menu_page_) {
        case MenuPage::Main: title = "SETTINGS"; break;
        case MenuPage::Controls: title = "CONTROLS"; break;
        case MenuPage::Graphics: title = "GRAPHICS"; break;
    }
    ui.add_text(title, panel_x + panel_w / 2.0f - 60.0f, panel_y + 15.0f, 1.5f, ui_colors::WHITE);

    float item_y = panel_y + 70.0f;
    for (size_t i = 0; i < menu_items_.size(); ++i) {
        const MenuItem& item = menu_items_[i];
        bool selected = (static_cast<int>(i) == menu_selected_index_);

        if (selected) {
            ui.add_filled_rect(panel_x + 10.0f, item_y, panel_w - 20.0f, 40.0f, ui_colors::SELECTION);
        }

        uint32_t text_color = selected ? ui_colors::WHITE : ui_colors::TEXT_DIM;
        ui.add_text(item.label, panel_x + 30.0f, item_y + 10.0f, 1.0f, text_color);

        if (item.type == MenuItemType::Toggle && item.toggle_value) {
            std::string value_str = *item.toggle_value ? "ON" : "OFF";
            uint32_t value_color = *item.toggle_value ? ui_colors::VALUE_ON : ui_colors::VALUE_OFF;
            ui.add_text(value_str, panel_x + panel_w - 80.0f, item_y + 10.0f, 1.0f, value_color);
        } else if (item.type == MenuItemType::Slider && item.slider_value) {
            std::string value_str;
            int idx = *item.slider_value - item.slider_min;
            if (!item.slider_labels.empty() && idx >= 0 && idx < static_cast<int>(item.slider_labels.size())) {
                value_str = item.slider_labels[idx];
            } else {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", *item.slider_value);
                value_str = buf;
            }

            std::string display = "< " + value_str + " >";
            ui.add_text(display, panel_x + panel_w - 120.0f, item_y + 10.0f, 1.0f, ui_colors::VALUE_SLIDER);
        } else if (item.type == MenuItemType::FloatSlider && item.float_value) {
            float slider_x = panel_x + panel_w - 200.0f;
            float slider_w = 120.0f;
            float slider_h = 8.0f;
            float slider_y_center = item_y + 18.0f;

            ui.add_filled_rect(slider_x, slider_y_center - slider_h/2, slider_w, slider_h, 0xFF444444);

            float fill_pct = (*item.float_value - item.float_min) / (item.float_max - item.float_min);
            ui.add_filled_rect(slider_x, slider_y_center - slider_h/2, slider_w * fill_pct, slider_h, ui_colors::VALUE_SLIDER);

            char value_buf[32];
            snprintf(value_buf, sizeof(value_buf), "%.2f", *item.float_value);
            ui.add_text(value_buf, panel_x + panel_w - 65.0f, item_y + 10.0f, 0.9f, ui_colors::WHITE);
        } else if (item.type == MenuItemType::Submenu) {
            ui.add_text(">", panel_x + panel_w - 40.0f, item_y + 10.0f, 1.0f, text_color);
        }

        item_y += 50.0f;
    }

    const char* hint = "W/S: Navigate  |  A/D: Adjust  |  SPACE: Select  |  ESC: Back";
    ui.add_text(hint, panel_x + 20.0f, panel_y + panel_h - 30.0f, 0.75f, ui_colors::TEXT_HINT);
}

} // namespace mmo::client

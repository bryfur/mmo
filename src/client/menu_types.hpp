#pragma once

#include <string>
#include <vector>
#include <functional>

namespace mmo {

enum class MenuPage {
    Main,
    Controls,
    Graphics
};

enum class MenuItemType {
    Toggle,
    Slider,
    FloatSlider,
    Button,
    Submenu
};

struct MenuItem {
    std::string label;
    MenuItemType type;
    bool* toggle_value = nullptr;
    int* slider_value = nullptr;
    float* float_value = nullptr;
    float float_min = 0.0f;
    float float_max = 1.0f;
    float float_step = 0.05f;
    int slider_min = 0;
    int slider_max = 2;
    std::vector<std::string> slider_labels;
    std::function<void()> action = nullptr;
    MenuPage target_page = MenuPage::Main;
};

} // namespace mmo

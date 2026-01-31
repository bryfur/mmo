#include "ui_scene.hpp"
#include <cstdint>
#include <string>
#include <utility>

namespace mmo::engine::scene {

void UIScene::clear() {
    commands_.clear();
}

void UIScene::add_filled_rect(float x, float y, float w, float h, uint32_t color) {
    UICommand cmd;
    cmd.data = FilledRectCommand{x, y, w, h, color};
    commands_.push_back(std::move(cmd));
}

void UIScene::add_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width) {
    UICommand cmd;
    cmd.data = RectOutlineCommand{x, y, w, h, color, line_width};
    commands_.push_back(std::move(cmd));
}

void UIScene::add_circle(float x, float y, float radius, uint32_t color, int segments) {
    UICommand cmd;
    cmd.data = CircleCommand{x, y, radius, color, segments};
    commands_.push_back(std::move(cmd));
}

void UIScene::add_circle_outline(float x, float y, float radius, uint32_t color, 
                                  float line_width, int segments) {
    UICommand cmd;
    cmd.data = CircleOutlineCommand{x, y, radius, color, line_width, segments};
    commands_.push_back(std::move(cmd));
}

void UIScene::add_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width) {
    UICommand cmd;
    cmd.data = LineCommand{x1, y1, x2, y2, color, line_width};
    commands_.push_back(std::move(cmd));
}

void UIScene::add_text(const std::string& text, float x, float y, float scale, uint32_t color) {
    UICommand cmd;
    TextCommand text_cmd;
    text_cmd.text = text;
    text_cmd.x = x;
    text_cmd.y = y;
    text_cmd.scale = scale;
    text_cmd.color = color;
    cmd.data = std::move(text_cmd);
    commands_.push_back(std::move(cmd));
}

void UIScene::add_button(float x, float y, float w, float h, const std::string& label,
                          uint32_t color, bool selected) {
    UICommand cmd;
    ButtonCommand btn_cmd;
    btn_cmd.x = x;
    btn_cmd.y = y;
    btn_cmd.w = w;
    btn_cmd.h = h;
    btn_cmd.label = label;
    btn_cmd.color = color;
    btn_cmd.selected = selected;
    cmd.data = std::move(btn_cmd);
    commands_.push_back(std::move(cmd));
}

} // namespace mmo::engine::scene

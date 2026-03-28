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

float UIScene::add_text_wrapped(const std::string& text, float x, float y, float max_width,
                                 float scale, uint32_t color, float line_height) {
    constexpr float CHAR_WIDTH_BASE = 8.0f;
    float char_w = CHAR_WIDTH_BASE * scale;
    if (line_height <= 0.0f) line_height = 18.0f * scale;
    int max_chars = static_cast<int>(max_width / char_w);
    if (max_chars < 1) max_chars = 1;

    float cur_y = y;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t remaining = text.size() - pos;
        if (static_cast<int>(remaining) <= max_chars) {
            // Rest fits on one line
            add_text(text.substr(pos), x, cur_y, scale, color);
            cur_y += line_height;
            break;
        }
        // Find last space within max_chars for word boundary
        size_t end = pos + max_chars;
        size_t wrap = text.rfind(' ', end);
        if (wrap == std::string::npos || wrap <= pos) {
            // No space found — hard break
            wrap = end;
        }
        add_text(text.substr(pos, wrap - pos), x, cur_y, scale, color);
        cur_y += line_height;
        pos = wrap;
        // Skip the space we broke at
        if (pos < text.size() && text[pos] == ' ') ++pos;
    }
    return cur_y - y;
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

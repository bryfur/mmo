#include "ui_scene.hpp"

namespace mmo {

void UIScene::clear() {
    commands_.clear();
    has_target_reticle_ = false;
}

void UIScene::add_filled_rect(float x, float y, float w, float h, uint32_t color) {
    UICommand cmd;
    cmd.type = UICommandType::FilledRect;
    cmd.filled_rect.x = x;
    cmd.filled_rect.y = y;
    cmd.filled_rect.w = w;
    cmd.filled_rect.h = h;
    cmd.filled_rect.color = color;
    commands_.push_back(std::move(cmd));
}

void UIScene::add_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width) {
    UICommand cmd;
    cmd.type = UICommandType::RectOutline;
    cmd.rect_outline.x = x;
    cmd.rect_outline.y = y;
    cmd.rect_outline.w = w;
    cmd.rect_outline.h = h;
    cmd.rect_outline.color = color;
    cmd.rect_outline.line_width = line_width;
    commands_.push_back(std::move(cmd));
}

void UIScene::add_circle(float x, float y, float radius, uint32_t color, int segments) {
    UICommand cmd;
    cmd.type = UICommandType::Circle;
    cmd.circle.x = x;
    cmd.circle.y = y;
    cmd.circle.radius = radius;
    cmd.circle.color = color;
    cmd.circle.segments = segments;
    commands_.push_back(std::move(cmd));
}

void UIScene::add_circle_outline(float x, float y, float radius, uint32_t color, 
                                  float line_width, int segments) {
    UICommand cmd;
    cmd.type = UICommandType::CircleOutline;
    cmd.circle_outline.x = x;
    cmd.circle_outline.y = y;
    cmd.circle_outline.radius = radius;
    cmd.circle_outline.color = color;
    cmd.circle_outline.line_width = line_width;
    cmd.circle_outline.segments = segments;
    commands_.push_back(std::move(cmd));
}

void UIScene::add_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width) {
    UICommand cmd;
    cmd.type = UICommandType::Line;
    cmd.line.x1 = x1;
    cmd.line.y1 = y1;
    cmd.line.x2 = x2;
    cmd.line.y2 = y2;
    cmd.line.color = color;
    cmd.line.line_width = line_width;
    commands_.push_back(std::move(cmd));
}

void UIScene::add_text(const std::string& text, float x, float y, float scale, uint32_t color) {
    UICommand cmd;
    cmd.type = UICommandType::Text;
    cmd.text.text = text;
    cmd.text.x = x;
    cmd.text.y = y;
    cmd.text.scale = scale;
    cmd.text.color = color;
    commands_.push_back(std::move(cmd));
}

void UIScene::add_button(float x, float y, float w, float h, const std::string& label,
                          uint32_t color, bool selected) {
    UICommand cmd;
    cmd.type = UICommandType::Button;
    cmd.button.x = x;
    cmd.button.y = y;
    cmd.button.w = w;
    cmd.button.h = h;
    cmd.button.label = label;
    cmd.button.color = color;
    cmd.button.selected = selected;
    commands_.push_back(std::move(cmd));
}

void UIScene::add_target_reticle() {
    UICommand cmd;
    cmd.type = UICommandType::TargetReticle;
    commands_.push_back(std::move(cmd));
    has_target_reticle_ = true;
}

void UIScene::add_player_health_bar(float health_ratio, float max_health) {
    UICommand cmd;
    cmd.type = UICommandType::PlayerHealthBar;
    cmd.player_health.health_ratio = health_ratio;
    cmd.player_health.max_health = max_health;
    commands_.push_back(std::move(cmd));
}

void UIScene::add_enemy_health_bar_3d(float world_x, float world_y, float world_z,
                                       float width, float health_ratio) {
    UICommand cmd;
    cmd.type = UICommandType::EnemyHealthBar3D;
    cmd.enemy_health_3d.world_x = world_x;
    cmd.enemy_health_3d.world_y = world_y;
    cmd.enemy_health_3d.world_z = world_z;
    cmd.enemy_health_3d.width = width;
    cmd.enemy_health_3d.health_ratio = health_ratio;
    commands_.push_back(std::move(cmd));
}

} // namespace mmo

#include "editor_tools.hpp"
#include "editor_application.hpp"
#include "editor_raycaster.hpp"
#include "engine/scene/render_scene.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/model_loader.hpp"
#include "engine/heightmap.hpp"
#include "client/ecs/components.hpp"
#include <imgui.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace mmo::editor {

using namespace mmo::client::ecs;
using namespace mmo::protocol;

// ============================================================================
// Terrain Brush Tool
// ============================================================================

bool TerrainBrushTool::on_mouse_down(int button, float mx, float my, EditorApplication& app) {
    if (button == SDL_BUTTON_LEFT) {
        painting_ = true;
        // For flatten mode, capture target height on first click
        if (mode_ == BrushMode::Flatten && app.cursor_on_terrain()) {
            flatten_target_ = app.cursor_world_pos().y;
        }
        return true;
    }
    return false;
}

bool TerrainBrushTool::on_mouse_up(int button, float mx, float my, EditorApplication& app) {
    if (button == SDL_BUTTON_LEFT) {
        painting_ = false;
        return true;
    }
    return false;
}

bool TerrainBrushTool::on_scroll(float delta, bool shift_held, EditorApplication& app) {
    if (shift_held) {
        strength_ = std::clamp(strength_ + delta * 10.0f, 10.0f, 300.0f);
    } else {
        radius_ = std::clamp(radius_ + delta * 25.0f, 50.0f, 500.0f);
    }
    return true;
}

bool TerrainBrushTool::on_key_down(int scancode, EditorApplication& app) {
    switch (scancode) {
        case SDL_SCANCODE_Q: mode_ = BrushMode::Raise; return true;
        case SDL_SCANCODE_W: mode_ = BrushMode::Lower; return true;
        case SDL_SCANCODE_E: mode_ = BrushMode::Smooth; return true;
        case SDL_SCANCODE_R: mode_ = BrushMode::Flatten; return true;
    }
    return false;
}

void TerrainBrushTool::update(float dt, EditorApplication& app) {
    if (painting_ && app.cursor_on_terrain()) {
        apply_brush(app.cursor_world_pos(), dt, app);
    }
}

void TerrainBrushTool::apply_brush(const glm::vec3& center, float dt, EditorApplication& app) {
    auto& hm = app.heightmap();
    if (hm.resolution == 0) return;

    float texel_size = hm.world_size / static_cast<float>(hm.resolution - 1);
    int radius_texels = static_cast<int>(radius_ / texel_size) + 1;

    int center_tx = static_cast<int>((center.x - hm.world_origin_x) / hm.world_size * (hm.resolution - 1));
    int center_tz = static_cast<int>((center.z - hm.world_origin_z) / hm.world_size * (hm.resolution - 1));

    // For smooth mode, precompute average in region
    float smooth_avg = 0.0f;
    int smooth_count = 0;
    if (mode_ == BrushMode::Smooth) {
        for (int tz = center_tz - radius_texels; tz <= center_tz + radius_texels; ++tz) {
            for (int tx = center_tx - radius_texels; tx <= center_tx + radius_texels; ++tx) {
                if (tx < 0 || tx >= (int)hm.resolution || tz < 0 || tz >= (int)hm.resolution) continue;
                float wx = hm.world_origin_x + (float(tx) / (hm.resolution - 1)) * hm.world_size;
                float wz = hm.world_origin_z + (float(tz) / (hm.resolution - 1)) * hm.world_size;
                float dist = std::sqrt((wx - center.x) * (wx - center.x) + (wz - center.z) * (wz - center.z));
                if (dist <= radius_) {
                    smooth_avg += hm.get_height_local(tx, tz);
                    smooth_count++;
                }
            }
        }
        if (smooth_count > 0) smooth_avg /= smooth_count;
    }

    bool modified = false;
    for (int tz = center_tz - radius_texels; tz <= center_tz + radius_texels; ++tz) {
        for (int tx = center_tx - radius_texels; tx <= center_tx + radius_texels; ++tx) {
            if (tx < 0 || tx >= (int)hm.resolution || tz < 0 || tz >= (int)hm.resolution) continue;

            float wx = hm.world_origin_x + (float(tx) / (hm.resolution - 1)) * hm.world_size;
            float wz = hm.world_origin_z + (float(tz) / (hm.resolution - 1)) * hm.world_size;
            float dist = std::sqrt((wx - center.x) * (wx - center.x) + (wz - center.z) * (wz - center.z));
            if (dist > radius_) continue;

            float falloff = 0.5f * (1.0f + std::cos(3.14159f * dist / radius_));
            float current = hm.get_height_local(tx, tz);
            float new_height = current;

            switch (mode_) {
                case BrushMode::Raise:
                    new_height += strength_ * falloff * dt;
                    break;
                case BrushMode::Lower:
                    new_height -= strength_ * falloff * dt;
                    break;
                case BrushMode::Smooth:
                    new_height += (smooth_avg - current) * falloff * dt * 3.0f;
                    break;
                case BrushMode::Flatten:
                    new_height += (flatten_target_ - current) * falloff * dt * 3.0f;
                    break;
            }

            new_height = std::clamp(new_height, hm.min_height, hm.max_height);
            float normalized = (new_height - hm.min_height) / (hm.max_height - hm.min_height);
            hm.height_data[tz * hm.resolution + tx] = static_cast<uint16_t>(normalized * 65535.0f);
            modified = true;
        }
    }

    if (modified) {
        app.mark_heightmap_dirty();
    }
}

void TerrainBrushTool::build_imgui(EditorApplication& app) {
    ImGui::Text("Terrain Brush");
    ImGui::Separator();

    int mode_i = static_cast<int>(mode_);
    ImGui::RadioButton("Raise (Q)", &mode_i, 0); ImGui::SameLine();
    ImGui::RadioButton("Lower (W)", &mode_i, 1);
    ImGui::RadioButton("Smooth (E)", &mode_i, 2); ImGui::SameLine();
    ImGui::RadioButton("Flatten (R)", &mode_i, 3);
    mode_ = static_cast<BrushMode>(mode_i);

    ImGui::SliderFloat("Radius", &radius_, 50.0f, 500.0f, "%.0f");
    ImGui::SliderFloat("Strength", &strength_, 10.0f, 300.0f, "%.0f");

    if (mode_ == BrushMode::Flatten) {
        ImGui::Text("Target: %.1f (click to set)", flatten_target_);
    }
}

void TerrainBrushTool::render_overlay(engine::scene::RenderScene& scene,
                                       engine::scene::UIScene& ui,
                                       EditorApplication& app) {
    if (!app.cursor_on_terrain()) return;

    auto cam = app.get_camera_state();
    auto center = app.cursor_world_pos();
    int sw = app.screen_width();
    int sh = app.screen_height();

    // Draw brush circle by projecting terrain points to screen
    const int segments = 48;
    uint32_t color;
    switch (mode_) {
        case BrushMode::Raise:   color = 0xFF00FF00; break; // green
        case BrushMode::Lower:   color = 0xFFFF4444; break; // red
        case BrushMode::Smooth:  color = 0xFF44AAFF; break; // blue
        case BrushMode::Flatten: color = 0xFFFFFF44; break; // yellow
    }

    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * 3.14159f * i / segments;
        float a1 = 2.0f * 3.14159f * (i + 1) / segments;

        float wx0 = center.x + radius_ * std::cos(a0);
        float wz0 = center.z + radius_ * std::sin(a0);
        float wy0 = app.get_terrain_height(wx0, wz0) + 2.0f;

        float wx1 = center.x + radius_ * std::cos(a1);
        float wz1 = center.z + radius_ * std::sin(a1);
        float wy1 = app.get_terrain_height(wx1, wz1) + 2.0f;

        glm::vec4 p0 = cam.view_projection * glm::vec4(wx0, wy0, wz0, 1.0f);
        glm::vec4 p1 = cam.view_projection * glm::vec4(wx1, wy1, wz1, 1.0f);

        if (p0.w > 0.1f && p1.w > 0.1f) {
            float sx0 = (p0.x / p0.w * 0.5f + 0.5f) * sw;
            float sy0 = (1.0f - (p0.y / p0.w * 0.5f + 0.5f)) * sh;
            float sx1 = (p1.x / p1.w * 0.5f + 0.5f) * sw;
            float sy1 = (1.0f - (p1.y / p1.w * 0.5f + 0.5f)) * sh;
            ui.add_line(sx0, sy0, sx1, sy1, color, 2.0f);
        }
    }
}

// ============================================================================
// Select Tool
// ============================================================================

entt::entity SelectTool::pick_entity(float mx, float my, EditorApplication& app) {
    auto cam = app.get_camera_state();
    auto ray = app.raycaster().screen_to_ray(mx, my, app.screen_width(), app.screen_height(), cam);

    float best_t = 1e30f;
    entt::entity best = entt::null;

    auto view = app.registry().view<Transform, EntityInfo>();
    for (auto entity : view) {
        auto& t = view.get<Transform>(entity);
        auto& info = view.get<EntityInfo>(entity);

        auto* model = app.models().get_model(info.model_name);
        if (!model) continue;

        float model_dim = model->max_dimension();
        if (model_dim < 0.001f) continue;
        float scale = (info.target_size * 1.5f) / model_dim;

        float cx = (model->min_x + model->max_x) / 2.0f;
        float cz = (model->min_z + model->max_z) / 2.0f;

        glm::vec3 aabb_min(
            t.x + (model->min_x - cx) * scale,
            t.y,
            t.z + (model->min_z - cz) * scale
        );
        glm::vec3 aabb_max(
            t.x + (model->max_x - cx) * scale,
            t.y + (model->max_y - model->min_y) * scale,
            t.z + (model->max_z - cz) * scale
        );

        float hit_t = EditorRaycaster::intersect_aabb(ray, aabb_min, aabb_max);
        if (hit_t >= 0.0f && hit_t < best_t) {
            best_t = hit_t;
            best = entity;
        }
    }

    return best;
}

bool SelectTool::on_mouse_down(int button, float mx, float my, EditorApplication& app) {
    if (button == SDL_BUTTON_LEFT) {
        auto picked = pick_entity(mx, my, app);
        if (picked != entt::null) {
            selected_ = picked;
            dragging_ = true;
        } else {
            selected_ = entt::null;
            dragging_ = false;
        }
        return true;
    }
    return false;
}

bool SelectTool::on_mouse_up(int button, float mx, float my, EditorApplication& app) {
    if (button == SDL_BUTTON_LEFT) {
        dragging_ = false;
        return true;
    }
    return false;
}

bool SelectTool::on_mouse_move(float mx, float my, EditorApplication& app) {
    if (dragging_ && selected_ != entt::null && app.cursor_on_terrain()) {
        auto& t = app.registry().get<Transform>(selected_);
        auto pos = app.cursor_world_pos();
        t.x = pos.x;
        t.z = pos.z;
        t.y = app.get_terrain_height(t.x, t.z);
        return true;
    }
    return false;
}

bool SelectTool::on_scroll(float delta, bool shift_held, EditorApplication& app) {
    if (selected_ != entt::null && app.registry().valid(selected_)) {
        auto& t = app.registry().get<Transform>(selected_);
        t.rotation += delta * 0.26f; // ~15 degree increments
        return true;
    }
    return false;
}

bool SelectTool::on_key_down(int scancode, EditorApplication& app) {
    if (scancode == SDL_SCANCODE_DELETE && selected_ != entt::null) {
        if (app.registry().valid(selected_)) {
            app.registry().destroy(selected_);
        }
        selected_ = entt::null;
        return true;
    }
    if (scancode == SDL_SCANCODE_ESCAPE) {
        selected_ = entt::null;
        return true;
    }
    return false;
}

void SelectTool::build_imgui(EditorApplication& app) {
    ImGui::Text("Select Tool");
    ImGui::Separator();

    if (selected_ == entt::null || !app.registry().valid(selected_)) {
        ImGui::TextDisabled("No selection");
        ImGui::TextDisabled("Click an entity to select");
        return;
    }

    auto& t = app.registry().get<Transform>(selected_);
    auto& info = app.registry().get<EntityInfo>(selected_);

    if (app.registry().all_of<Name>(selected_)) {
        auto& n = app.registry().get<Name>(selected_);
        ImGui::Text("Name: %s", n.value.c_str());
    }
    ImGui::Text("Model: %s", info.model_name.c_str());

    ImGui::DragFloat3("Position", &t.x, 1.0f);
    float rot_deg = glm::degrees(t.rotation);
    if (ImGui::DragFloat("Rotation", &rot_deg, 1.0f, -360.0f, 360.0f)) {
        t.rotation = glm::radians(rot_deg);
    }
    ImGui::DragFloat("Size", &info.target_size, 0.5f, 1.0f, 500.0f);

    ImGui::Spacing();
    if (ImGui::Button("Snap to Ground", ImVec2(-1, 0))) {
        t.y = app.get_terrain_height(t.x, t.z);
    }
    if (ImGui::Button("Delete (Del)", ImVec2(-1, 0))) {
        app.registry().destroy(selected_);
        selected_ = entt::null;
    }
}

void SelectTool::render_overlay(engine::scene::RenderScene& scene,
                                 engine::scene::UIScene& ui,
                                 EditorApplication& app) {
    // Selection highlight is handled in add_entity_to_scene via selected_entity()
}

// ============================================================================
// Placement Tool
// ============================================================================

void PlacementTool::build_palette(EditorApplication& app) {
    palette_.clear();
    categories_.clear();

    // Read palette from models.json manifest
    std::ifstream manifest_file("data/models.json");
    if (!manifest_file.is_open()) {
        std::cerr << "PlacementTool: Failed to open data/models.json\n";
        palette_built_ = true;
        return;
    }

    nlohmann::json manifest;
    try {
        manifest_file >> manifest;
    } catch (const std::exception& e) {
        std::cerr << "PlacementTool: Failed to parse data/models.json: " << e.what() << "\n";
        palette_built_ = true;
        return;
    }

    for (auto& entry : manifest["models"]) {
        if (!entry.value("placeable", false)) continue;

        std::string id = entry["id"];
        if (!app.models().get_model(id)) continue;

        PlaceableObject obj;
        obj.model_name = id;
        obj.display_name = entry.value("display_name", id);
        obj.category = entry.value("category", "other");
        obj.default_size = entry.value("default_size", 30.0f);
        obj.default_color = entry.value("default_color", (uint32_t)0xFFFFFFFF);

        // Capitalize category for display
        if (!obj.category.empty()) {
            obj.category[0] = static_cast<char>(std::toupper(obj.category[0]));
        }

        palette_.push_back(obj);
    }

    // Build unique category list (preserving manifest order)
    for (auto& obj : palette_) {
        bool found = false;
        for (auto& cat : categories_) {
            if (cat == obj.category) { found = true; break; }
        }
        if (!found) categories_.push_back(obj.category);
    }

    palette_built_ = true;
}

bool PlacementTool::on_mouse_down(int button, float mx, float my, EditorApplication& app) {
    if (button == SDL_BUTTON_LEFT && selected_object_ >= 0 && app.cursor_on_terrain()) {
        auto& obj = palette_[selected_object_];
        auto pos = app.cursor_world_pos();

        auto entity = app.registry().create();
        app.registry().emplace<Transform>(entity, pos.x, pos.y, pos.z, placement_rotation_);

        EntityInfo info;
        info.type = EntityType::Environment;
        info.model_name = obj.model_name;
        info.target_size = obj.default_size * placement_scale_;
        info.color = obj.default_color;
        app.registry().emplace<EntityInfo>(entity, info);

        app.registry().emplace<Name>(entity, obj.display_name);
        return true;
    }
    return false;
}

bool PlacementTool::on_scroll(float delta, bool shift_held, EditorApplication& app) {
    if (shift_held) {
        placement_scale_ = std::clamp(placement_scale_ + delta * 0.1f, 0.1f, 10.0f);
    } else {
        placement_rotation_ += delta * 0.26f; // ~15 degrees
    }
    return true;
}

void PlacementTool::build_imgui(EditorApplication& app) {
    if (!palette_built_) build_palette(app);

    ImGui::Text("Place Object");
    ImGui::Separator();

    // Category tabs
    for (int i = 0; i < (int)categories_.size(); ++i) {
        if (i > 0) ImGui::SameLine();
        bool selected = (i == active_category_);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.3f, 1.0f));
        if (ImGui::Button(categories_[i].c_str())) {
            active_category_ = i;
            selected_object_ = -1;
        }
        if (selected) ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Object list for active category
    if (active_category_ < (int)categories_.size()) {
        const auto& cat = categories_[active_category_];
        for (int i = 0; i < (int)palette_.size(); ++i) {
            if (palette_[i].category != cat) continue;
            bool is_selected = (i == selected_object_);
            if (ImGui::Selectable(palette_[i].display_name.c_str(), is_selected)) {
                selected_object_ = i;
                placement_scale_ = 1.0f;
            }
        }
    }

    ImGui::Separator();
    ImGui::SliderFloat("Rotation", &placement_rotation_, -3.14159f, 3.14159f, "%.2f rad");
    ImGui::SliderFloat("Scale", &placement_scale_, 0.1f, 10.0f, "%.1fx");
}

void PlacementTool::render_overlay(engine::scene::RenderScene& scene,
                                    engine::scene::UIScene& ui,
                                    EditorApplication& app) {
    if (selected_object_ < 0 || !app.cursor_on_terrain()) return;

    auto& obj = palette_[selected_object_];
    auto* model = app.models().get_model(obj.model_name);
    if (!model) return;

    auto pos = app.cursor_world_pos();

    // Build transform like add_entity_to_scene does
    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::translate(transform, pos);
    transform = glm::rotate(transform, placement_rotation_, glm::vec3(0.0f, 1.0f, 0.0f));

    float model_dim = model->max_dimension();
    float scale = (obj.default_size * placement_scale_ * 1.5f) / model_dim;
    transform = glm::scale(transform, glm::vec3(scale));

    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    transform = transform * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    // Ghost preview - semi-transparent
    scene.add_model(obj.model_name, transform, glm::vec4(0.5f, 1.0f, 0.5f, 0.5f));
}

} // namespace mmo::editor

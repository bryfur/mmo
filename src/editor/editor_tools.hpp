#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace mmo::engine::scene {
class RenderScene;
class UIScene;
} // namespace mmo::engine::scene

namespace mmo::editor {

class EditorApplication;

enum class ToolType { Select, Terrain, Place };
enum class BrushMode { Raise, Lower, Smooth, Flatten };

// ============================================================================
// Base tool interface
// ============================================================================

class EditorTool {
public:
    virtual ~EditorTool() = default;

    virtual ToolType type() const = 0;
    virtual const char* name() const = 0;

    // Input callbacks - return true if consumed
    virtual bool on_mouse_down(int button, float mx, float my, EditorApplication& app) { return false; }
    virtual bool on_mouse_up(int button, float mx, float my, EditorApplication& app) { return false; }
    virtual bool on_mouse_move(float mx, float my, EditorApplication& app) { return false; }
    virtual bool on_scroll(float delta, bool shift_held, EditorApplication& app) { return false; }
    virtual bool on_key_down(int scancode, EditorApplication& app) { return false; }

    // Per-frame update while tool is active
    virtual void update(float dt, EditorApplication& app) {}

    // Build ImGui panel for this tool
    virtual void build_imgui(EditorApplication& app) {}

    // Add 3D overlay to the render scene (brush circle, ghost preview, etc.)
    virtual void render_overlay(engine::scene::RenderScene& scene,
                                engine::scene::UIScene& ui,
                                EditorApplication& app) {}
};

// ============================================================================
// Terrain Brush Tool
// ============================================================================

class TerrainBrushTool : public EditorTool {
public:
    ToolType type() const override { return ToolType::Terrain; }
    const char* name() const override { return "Terrain Brush"; }

    bool on_mouse_down(int button, float mx, float my, EditorApplication& app) override;
    bool on_mouse_up(int button, float mx, float my, EditorApplication& app) override;
    bool on_scroll(float delta, bool shift_held, EditorApplication& app) override;
    bool on_key_down(int scancode, EditorApplication& app) override;

    void update(float dt, EditorApplication& app) override;
    void build_imgui(EditorApplication& app) override;
    void render_overlay(engine::scene::RenderScene& scene,
                        engine::scene::UIScene& ui,
                        EditorApplication& app) override;

private:
    void apply_brush(const glm::vec3& center, float dt, EditorApplication& app);

    BrushMode mode_ = BrushMode::Raise;
    float radius_ = 200.0f;
    float strength_ = 80.0f;
    float flatten_target_ = 0.0f;
    bool painting_ = false;
};

// ============================================================================
// Select Tool
// ============================================================================

class SelectTool : public EditorTool {
public:
    ToolType type() const override { return ToolType::Select; }
    const char* name() const override { return "Select"; }

    bool on_mouse_down(int button, float mx, float my, EditorApplication& app) override;
    bool on_mouse_up(int button, float mx, float my, EditorApplication& app) override;
    bool on_mouse_move(float mx, float my, EditorApplication& app) override;
    bool on_scroll(float delta, bool shift_held, EditorApplication& app) override;
    bool on_key_down(int scancode, EditorApplication& app) override;

    void build_imgui(EditorApplication& app) override;
    void render_overlay(engine::scene::RenderScene& scene,
                        engine::scene::UIScene& ui,
                        EditorApplication& app) override;

    entt::entity selected() const { return selected_; }

private:
    entt::entity pick_entity(float mx, float my, EditorApplication& app);

    entt::entity selected_ = entt::null;
    bool dragging_ = false;
};

// ============================================================================
// Placement Tool
// ============================================================================

struct PlaceableObject {
    std::string category;
    std::string model_name;
    std::string display_name;
    float default_size = 30.0f;
    uint32_t default_color = 0xFFFFFFFF;
};

class PlacementTool : public EditorTool {
public:
    ToolType type() const override { return ToolType::Place; }
    const char* name() const override { return "Place Object"; }

    bool on_mouse_down(int button, float mx, float my, EditorApplication& app) override;
    bool on_scroll(float delta, bool shift_held, EditorApplication& app) override;

    void build_imgui(EditorApplication& app) override;
    void render_overlay(engine::scene::RenderScene& scene,
                        engine::scene::UIScene& ui,
                        EditorApplication& app) override;

private:
    void build_palette(EditorApplication& app);

    std::vector<PlaceableObject> palette_;
    std::vector<std::string> categories_;
    int active_category_ = 0;
    int selected_object_ = -1;
    float placement_rotation_ = 0.0f;
    float placement_scale_ = 1.0f;
    bool palette_built_ = false;
};

} // namespace mmo::editor

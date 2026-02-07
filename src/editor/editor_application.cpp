#include "editor_application.hpp"
#include "world_save.hpp"
#include "protocol/protocol.hpp"
#include "protocol/heightmap.hpp"
#include "server/heightmap_generator.hpp"
#include "server/entity_config.hpp"
#include "engine/model_loader.hpp"
#include "engine/heightmap.hpp"
#include "engine/render/render_context.hpp"
#include "engine/scene/scene_renderer.hpp"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <iostream>
#include <SDL3/SDL.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <sys/stat.h>

namespace mmo::editor {

using namespace mmo::client::ecs;
using namespace mmo::protocol;

EditorApplication::EditorApplication() = default;
EditorApplication::~EditorApplication() = default;

bool EditorApplication::init() {
    if (!init_engine()) {
        std::cerr << "Failed to initialize engine" << std::endl;
        return false;
    }

    if (!on_init()) {
        std::cerr << "Failed to initialize editor application" << std::endl;
        return false;
    }

    return true;
}

bool EditorApplication::on_init() {
    // Load game configuration
    std::cout << "Loading game configuration..." << std::endl;
    if (!config_.load("data")) {
        std::cerr << "Failed to load game config from data/" << std::endl;
        return false;
    }

    // Initialize renderer
    std::cout << "Initializing renderer..." << std::endl;
    if (!init_renderer(1280, 720, "MMO Editor",
                      config_.world().width, config_.world().height)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return false;
    }

    // Initialize ImGui
    init_imgui();

    // Generate heightmap (may be overridden by save load below)
    std::cout << "Generating heightmap..." << std::endl;
    protocol::HeightmapChunk hm_chunk;
    server::heightmap_init(hm_chunk, 0, 0, protocol::heightmap_config::CHUNK_RESOLUTION);
    server::heightmap_generator::generate_procedural(hm_chunk, config_.world().width, config_.world().height);

    heightmap_.resolution = hm_chunk.resolution;
    heightmap_.world_origin_x = hm_chunk.world_origin_x;
    heightmap_.world_origin_z = hm_chunk.world_origin_z;
    heightmap_.world_size = hm_chunk.world_size;
    heightmap_.min_height = protocol::heightmap_config::MIN_HEIGHT;
    heightmap_.max_height = protocol::heightmap_config::MAX_HEIGHT;
    heightmap_.height_data = hm_chunk.height_data;

    set_heightmap(heightmap_);

    // Load 3D models
    std::cout << "Loading 3D models..." << std::endl;
    if (!load_models("assets")) {
        std::cerr << "Warning: Some models failed to load" << std::endl;
    }

    // Initialize generation defaults from config
    gen_center_x_ = config_.world().width / 2.0f;
    gen_center_z_ = config_.world().height / 2.0f;
    town_gen_.wall_distance = config_.wall().distance;
    town_gen_.log_spacing = config_.wall().spacing;
    town_gen_.gate_width = config_.wall().gate_width;
    monster_gen_.count = config_.monster().count;
    monster_gen_.safe_zone_radius = config_.safe_zone_radius();

    // Load saved world if it exists, otherwise start empty
    if (WorldSave::exists(save_dir_)) {
        std::cout << "Loading saved world from " << save_dir_ << "..." << std::endl;
        load_world();
    } else {
        std::cout << "No save found. Use Procedural Generation to populate the world." << std::endl;
    }

    // Initialize tools
    select_tool_ = std::make_unique<SelectTool>();
    terrain_tool_ = std::make_unique<TerrainBrushTool>();
    place_tool_ = std::make_unique<PlacementTool>();
    active_tool_ = select_tool_.get();
    active_tool_type_ = ToolType::Select;

    // Set post-UI callback for ImGui rendering
    scene_renderer().set_post_ui_callback(
        [this](SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain) {
            imgui_render(cmd, swapchain);
        });

    // Record initial mtime for hot-reload
    {
        struct stat st;
        std::string path = save_dir_ + "/world_entities.json";
        if (stat(path.c_str(), &st) == 0) {
            last_entity_mtime_ = st.st_mtime;
        }
    }

    std::cout << "Editor initialized successfully" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  RMB + WASD - Camera movement" << std::endl;
    std::cout << "  RMB + Mouse - Look around" << std::endl;
    std::cout << "  1/2/3 - Select/Terrain/Place tools" << std::endl;
    std::cout << "  Ctrl+S - Save world" << std::endl;
    std::cout << "  ESC - Quit" << std::endl;

    return true;
}

void EditorApplication::shutdown() {
    on_shutdown();
    shutdown_engine();
}

void EditorApplication::on_shutdown() {
    shutdown_imgui();
    shutdown_renderer();
}

// ============================================================================
// ImGui Lifecycle
// ============================================================================

void EditorApplication::init_imgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Make the style a bit more compact for editor use
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;

    auto* ctx = scene_renderer().context();
    ImGui_ImplSDL3_InitForSDLGPU(ctx->window());

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = ctx->device().handle();
    init_info.ColorTargetFormat = ctx->swapchain_format();
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&init_info);

    imgui_initialized_ = true;
    std::cout << "ImGui initialized with SDL3 GPU backend" << std::endl;
}

void EditorApplication::shutdown_imgui() {
    if (!imgui_initialized_) return;
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    imgui_initialized_ = false;
}

void EditorApplication::imgui_new_frame() {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void EditorApplication::imgui_render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data) return;

    Imgui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = swapchain;
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    if (pass) {
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, pass);
        SDL_EndGPURenderPass(pass);
    }
}

// ============================================================================
// Event Handling
// ============================================================================

bool EditorApplication::on_event(const SDL_Event& event) {
    // Forward all events to ImGui
    ImGui_ImplSDL3_ProcessEvent(&event);

    ImGuiIO& io = ImGui::GetIO();

    // If ImGui wants keyboard, don't process further
    if (io.WantCaptureKeyboard && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)) {
        return true;
    }

    // Handle mouse events
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        // RMB = enter camera mode
        if (event.button.button == SDL_BUTTON_RIGHT) {
            camera_active_ = true;
            SDL_SetWindowRelativeMouseMode(scene_renderer().context()->window(), true);
            return true;
        }
        // If ImGui wants mouse, consume
        if (io.WantCaptureMouse) return true;

        // Generation center placement mode
        if (gen_placing_ && event.button.button == SDL_BUTTON_LEFT && cursor_on_terrain_) {
            gen_center_x_ = cursor_world_pos_.x;
            gen_center_z_ = cursor_world_pos_.z;
            gen_center_set_ = true;
            gen_placing_ = false;
            toast_message_ = "Center placed";
            toast_timer_ = 2.0f;
            return true;
        }

        // Forward to active tool
        if (active_tool_ && !camera_active_) {
            active_tool_->on_mouse_down(event.button.button, mouse_x_, mouse_y_, *this);
        }
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event.button.button == SDL_BUTTON_RIGHT) {
            camera_active_ = false;
            SDL_SetWindowRelativeMouseMode(scene_renderer().context()->window(), false);
            return true;
        }
        if (io.WantCaptureMouse) return true;

        if (active_tool_ && !camera_active_) {
            active_tool_->on_mouse_up(event.button.button, mouse_x_, mouse_y_, *this);
        }
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (camera_active_) {
            float sensitivity = 0.003f;
            camera_.rotate_yaw(event.motion.xrel * sensitivity);
            camera_.rotate_pitch(-event.motion.yrel * sensitivity);
            return true;
        }
        mouse_x_ = event.motion.x;
        mouse_y_ = event.motion.y;
        if (!io.WantCaptureMouse && active_tool_) {
            active_tool_->on_mouse_move(mouse_x_, mouse_y_, *this);
        }
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        if (io.WantCaptureMouse) return true;
        if (active_tool_ && !camera_active_) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
            active_tool_->on_scroll(event.wheel.y, shift, *this);
        }
        return true;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        int sc = event.key.scancode;

        // Ctrl+S = save
        if (sc == SDL_SCANCODE_S && (event.key.mod & SDL_KMOD_CTRL)) {
            save_world();
            return true;
        }

        // ESC - cancel placement mode, or quit
        if (sc == SDL_SCANCODE_ESCAPE) {
            if (gen_placing_) {
                gen_placing_ = false;
                return true;
            }
            quit();
            return true;
        }

        // Tool switching: 1/2/3
        if (sc == SDL_SCANCODE_1) {
            active_tool_ = select_tool_.get();
            active_tool_type_ = ToolType::Select;
            return true;
        }
        if (sc == SDL_SCANCODE_2) {
            active_tool_ = terrain_tool_.get();
            active_tool_type_ = ToolType::Terrain;
            return true;
        }
        if (sc == SDL_SCANCODE_3) {
            active_tool_ = place_tool_.get();
            active_tool_type_ = ToolType::Place;
            return true;
        }

        // Forward to active tool
        if (active_tool_ && !camera_active_) {
            if (active_tool_->on_key_down(sc, *this)) return true;
        }
    }

    return false;
}

// ============================================================================
// Update
// ============================================================================

void EditorApplication::on_update(float dt) {
    // Camera movement (only when RMB held)
    if (camera_active_) {
        handle_camera_input(dt);
    }
    camera_.update(dt);

    // Cursor raycast (when mouse is free)
    if (!camera_active_) {
        update_cursor_raycast();
    }

    // Update active tool
    if (active_tool_) {
        active_tool_->update(dt, *this);
    }

    // Throttled heightmap update (~10Hz)
    if (heightmap_dirty_) {
        heightmap_update_timer_ += dt;
        if (heightmap_update_timer_ >= 0.1f) {
            set_heightmap(heightmap_);
            heightmap_dirty_ = false;
            heightmap_update_timer_ = 0.0f;
        }
    }

    // Hot-reload check (~2Hz)
    reload_check_timer_ += dt;
    if (reload_check_timer_ >= 0.5f) {
        reload_check_timer_ = 0.0f;
        check_hot_reload();
    }

    // Toast timer
    if (toast_timer_ > 0.0f) {
        toast_timer_ -= dt;
    }
}

void EditorApplication::handle_camera_input(float dt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);

    float move_speed = camera_.get_move_speed();
    if (keys[SDL_SCANCODE_LSHIFT]) move_speed *= 0.3f;
    if (keys[SDL_SCANCODE_LCTRL]) move_speed *= 3.0f;

    if (keys[SDL_SCANCODE_W]) camera_.move_forward(move_speed * dt);
    if (keys[SDL_SCANCODE_S]) camera_.move_forward(-move_speed * dt);
    if (keys[SDL_SCANCODE_A]) camera_.move_right(-move_speed * dt);
    if (keys[SDL_SCANCODE_D]) camera_.move_right(move_speed * dt);
    if (keys[SDL_SCANCODE_Q]) camera_.move_up(-move_speed * dt);
    if (keys[SDL_SCANCODE_E]) camera_.move_up(move_speed * dt);
}

void EditorApplication::update_cursor_raycast() {
    auto cam = get_camera_state();
    auto ray = raycaster_.screen_to_ray(mouse_x_, mouse_y_, screen_width(), screen_height(), cam);

    glm::vec3 hit;
    auto height_fn = [this](float x, float z) { return get_terrain_height(x, z); };
    cursor_on_terrain_ = raycaster_.intersect_terrain(ray, hit, height_fn);
    if (cursor_on_terrain_) {
        cursor_world_pos_ = hit;
    }
}

// ============================================================================
// Render
// ============================================================================

void EditorApplication::on_render() {
    render_scene_.clear();
    ui_scene_.clear();

    // Build ImGui frame
    imgui_new_frame();
    build_imgui_ui();
    ImGui::Render();

    // Build render scene from entities
    build_render_scene();

    // Tool overlay (brush circle, ghost preview, etc.)
    if (active_tool_) {
        active_tool_->render_overlay(render_scene_, ui_scene_, *this);
    }

    // Generation radius circle overlay — follows cursor while placing, fixed when placed
    if (gen_placing_ || gen_center_set_) {
        float cx = gen_placing_ ? cursor_world_pos_.x : gen_center_x_;
        float cz = gen_placing_ ? cursor_world_pos_.z : gen_center_z_;

        auto cam = get_camera_state();
        int sw = screen_width();
        int sh = screen_height();

        float radius = env_gen_.radius;

        auto draw_circle = [&](float center_x, float center_z, float r, uint32_t color) {
            const int segments = 64;
            for (int i = 0; i < segments; ++i) {
                float a0 = 2.0f * 3.14159f * i / segments;
                float a1 = 2.0f * 3.14159f * (i + 1) / segments;

                float wx0 = center_x + r * std::cos(a0);
                float wz0 = center_z + r * std::sin(a0);
                float wy0 = get_terrain_height(wx0, wz0) + 3.0f;

                float wx1 = center_x + r * std::cos(a1);
                float wz1 = center_z + r * std::sin(a1);
                float wy1 = get_terrain_height(wx1, wz1) + 3.0f;

                glm::vec4 p0 = cam.view_projection * glm::vec4(wx0, wy0, wz0, 1.0f);
                glm::vec4 p1 = cam.view_projection * glm::vec4(wx1, wy1, wz1, 1.0f);

                if (p0.w > 0.1f && p1.w > 0.1f) {
                    float sx0 = (p0.x / p0.w * 0.5f + 0.5f) * sw;
                    float sy0 = (1.0f - (p0.y / p0.w * 0.5f + 0.5f)) * sh;
                    float sx1 = (p1.x / p1.w * 0.5f + 0.5f) * sw;
                    float sy1 = (1.0f - (p1.y / p1.w * 0.5f + 0.5f)) * sh;
                    ui_scene_.add_line(sx0, sy0, sx1, sy1, color, 2.0f);
                }
            }
        };

        // Outer radius (environment/monsters)
        draw_circle(cx, cz, radius, 0xFF44FF44);  // green

        // Inner exclusion / safe zone
        if (env_gen_.min_distance > 0.0f) {
            draw_circle(cx, cz, env_gen_.min_distance, 0xFF4488FF);  // blue
        }

        // Center crosshair
        {
            float cy = get_terrain_height(cx, cz) + 5.0f;
            glm::vec4 pc = cam.view_projection * glm::vec4(cx, cy, cz, 1.0f);
            if (pc.w > 0.1f) {
                float sx = (pc.x / pc.w * 0.5f + 0.5f) * sw;
                float sy = (1.0f - (pc.y / pc.w * 0.5f + 0.5f)) * sh;
                ui_scene_.add_line(sx - 8, sy, sx + 8, sy, 0xFFFFFFFF, 2.0f);
                ui_scene_.add_line(sx, sy - 8, sx, sy + 8, 0xFFFFFFFF, 2.0f);
            }
        }
    }

    // Status bar
    {
        auto pos = camera_.get_position();
        char buf[256];
        snprintf(buf, sizeof(buf), "Camera: (%.0f, %.0f, %.0f) | FPS: %.0f | Tool: %s",
                pos.x, pos.y, pos.z, fps(),
                active_tool_ ? active_tool_->name() : "None");
        ui_scene_.add_text(buf, 20, screen_height() - 30, 1.0f, 0xFFCCCCCC);
    }

    // Cursor position
    if (cursor_on_terrain_) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Cursor: (%.0f, %.1f, %.0f)",
                cursor_world_pos_.x, cursor_world_pos_.y, cursor_world_pos_.z);
        ui_scene_.add_text(buf, 20, screen_height() - 50, 1.0f, 0xFF88BBFF);
    }

    // Placement mode hint
    if (gen_placing_) {
        ui_scene_.add_text("Click terrain to place generation center (ESC to cancel)",
                          screen_width() / 2 - 200, screen_height() - 70, 1.0f, 0xFFFFAA44);
    }

    // Toast notification
    if (toast_timer_ > 0.0f && !toast_message_.empty()) {
        float alpha = std::min(toast_timer_, 1.0f);
        uint32_t color = (static_cast<uint32_t>(alpha * 255) << 24) | 0x00FFFF44;
        ui_scene_.add_text(toast_message_, screen_width() / 2 - 80, 40, 1.0f, color);
    }

    // Render frame
    auto camera_state = get_camera_state();
    render_frame(render_scene_, ui_scene_, camera_state, 0.016f);
}

void EditorApplication::build_imgui_ui() {
    // Toolbar window
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    // Tool buttons
    auto tool_button = [&](const char* label, ToolType type, EditorTool* tool, const char* key) {
        bool selected = (active_tool_type_ == type);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
        char buf[64];
        snprintf(buf, sizeof(buf), "%s (%s)", label, key);
        if (ImGui::Button(buf, ImVec2(-1, 0))) {
            active_tool_ = tool;
            active_tool_type_ = type;
        }
        if (selected) ImGui::PopStyleColor();
    };

    tool_button("Select", ToolType::Select, select_tool_.get(), "1");
    tool_button("Terrain", ToolType::Terrain, terrain_tool_.get(), "2");
    tool_button("Place", ToolType::Place, place_tool_.get(), "3");

    ImGui::Separator();

    // Active tool panel
    if (active_tool_) {
        active_tool_->build_imgui(*this);
    }

    ImGui::Separator();

    // Save button
    if (ImGui::Button("Save World (Ctrl+S)", ImVec2(-1, 0))) {
        save_world();
    }

    // Snap all entities to terrain
    if (ImGui::Button("Snap All to Ground", ImVec2(-1, 0))) {
        snap_entities_to_terrain();
        toast_message_ = "Snapped to terrain";
        toast_timer_ = 2.0f;
    }

    // Entity count
    {
        size_t count = 0;
        auto view = registry_.view<Transform>();
        for (auto e : view) { (void)e; count++; }
        ImGui::Text("Entities: %zu", count);
    }

    ImGui::End();

    // Procedural generation window
    build_generation_ui();

    // Camera info (small overlay)
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(screen_width()) - 250, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::Text("FPS: %.0f", fps());
    auto pos = camera_.get_position();
    ImGui::Text("Camera: %.0f, %.0f, %.0f", pos.x, pos.y, pos.z);
    if (cursor_on_terrain_) {
        ImGui::Text("Cursor: %.0f, %.1f, %.0f",
                    cursor_world_pos_.x, cursor_world_pos_.y, cursor_world_pos_.z);
    }
    ImGui::Text("RMB+WASD: Camera");
    ImGui::End();
}

// ============================================================================
// Entity Management
// ============================================================================

entt::entity EditorApplication::selected_entity() const {
    if (active_tool_type_ == ToolType::Select && select_tool_) {
        return select_tool_->selected();
    }
    return entt::null;
}

// ============================================================================
// Render Scene Building
// ============================================================================

void EditorApplication::build_render_scene() {
    auto view = registry_.view<Transform, EntityInfo>();
    for (auto entity : view) {
        add_entity_to_scene(entity);
    }
}

void EditorApplication::add_entity_to_scene(entt::entity entity) {
    auto& transform = registry_.get<Transform>(entity);
    auto& info = registry_.get<EntityInfo>(entity);

    auto* model_ptr = models().get_model(info.model_name);
    if (!model_ptr) return;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(transform.x, transform.y, transform.z));

    // Tilt to match terrain slope
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
    heightmap_.get_normal_world(transform.x, transform.z, nx, ny, nz);
    glm::vec3 terrain_normal(nx, ny, nz);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 tilt_axis = glm::cross(up, terrain_normal);
    float tilt_len = glm::length(tilt_axis);
    if (tilt_len > 0.001f) {
        tilt_axis /= tilt_len;
        float tilt_angle = std::acos(std::clamp(glm::dot(up, terrain_normal), -1.0f, 1.0f));
        model = glm::rotate(model, tilt_angle, tilt_axis);
    }

    // Yaw rotation (around the tilted up axis)
    model = glm::rotate(model, transform.rotation, glm::vec3(0.0f, 1.0f, 0.0f));

    float target_size = info.target_size;
    float model_size = model_ptr->max_dimension();
    float scale = (target_size * 1.5f) / model_size;
    model = glm::scale(model, glm::vec3(scale));

    float cx = (model_ptr->min_x + model_ptr->max_x) / 2.0f;
    float cy = model_ptr->min_y;
    float cz = (model_ptr->min_z + model_ptr->max_z) / 2.0f;
    model = model * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    // Color - golden highlight for selected entity
    uint32_t argb = info.color;
    if (entity == selected_entity()) {
        argb = 0xFFFFDD44; // golden highlight
    }

    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8) & 0xFF) / 255.0f;
    float b = (argb & 0xFF) / 255.0f;
    glm::vec4 tint(r, g, b, a);

    render_scene_.add_model(info.model_name, model, tint);
}

engine::scene::CameraState EditorApplication::get_camera_state() const {
    engine::scene::CameraState state;
    float aspect = static_cast<float>(screen_width()) / static_cast<float>(screen_height());
    state.view = camera_.get_view_matrix();
    state.projection = camera_.get_projection_matrix(aspect);
    state.view_projection = state.projection * state.view;
    state.position = camera_.get_position();
    return state;
}

// ============================================================================
// Save / Load
// ============================================================================

void EditorApplication::save_world() {
    if (WorldSave::save(save_dir_, heightmap_, registry_)) {
        toast_message_ = "World saved!";
        toast_timer_ = 3.0f;
        // Update mtime so we don't hot-reload our own save
        struct stat st;
        std::string path = save_dir_ + "/world_entities.json";
        if (stat(path.c_str(), &st) == 0) {
            last_entity_mtime_ = st.st_mtime;
        }
    } else {
        toast_message_ = "Save failed!";
        toast_timer_ = 3.0f;
    }
}

void EditorApplication::load_world() {
    if (WorldSave::load(save_dir_, heightmap_, registry_)) {
        set_heightmap(heightmap_);
        snap_entities_to_terrain();
        struct stat st;
        std::string path = save_dir_ + "/world_entities.json";
        if (stat(path.c_str(), &st) == 0) {
            last_entity_mtime_ = st.st_mtime;
        }
    }
}

void EditorApplication::snap_entities_to_terrain() {
    auto view = registry_.view<Transform>();
    int count = 0;
    for (auto entity : view) {
        auto& t = view.get<Transform>(entity);
        t.y = get_terrain_height(t.x, t.z);
        count++;
    }
    std::cout << "Snapped " << count << " entities to terrain" << std::endl;
}

// ============================================================================
// Hot-Reload
// ============================================================================

void EditorApplication::check_hot_reload() {
    std::string path = save_dir_ + "/world_entities.json";
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return;

    if (st.st_mtime != last_entity_mtime_ && last_entity_mtime_ != 0) {
        std::cout << "Hot-reload: world_entities.json changed externally, reloading..." << std::endl;
        last_entity_mtime_ = st.st_mtime;

        // Reload entities only (keep heightmap as-is)
        std::ifstream f(path);
        if (!f) return;

        nlohmann::json j;
        try {
            f >> j;
        } catch (...) {
            std::cerr << "Hot-reload: failed to parse JSON" << std::endl;
            return;
        }

        registry_.clear();
        for (auto& ej : j) {
            auto entity = registry_.create();

            float px = ej["position"][0].get<float>();
            float py = ej["position"][1].get<float>();
            float pz = ej["position"][2].get<float>();
            float rot = ej.value("rotation", 0.0f);
            registry_.emplace<Transform>(entity, px, py, pz, rot);

            EntityInfo info;
            info.model_name = ej["model"].get<std::string>();
            if (ej.contains("entity_type")) {
                info.type = server::config::entity_type_from_string(ej["entity_type"].get<std::string>());
            } else {
                info.type = static_cast<EntityType>(ej.value("type", 0));
            }
            info.target_size = ej.value("target_size", 30.0f);
            info.color = ej.value("color", (uint32_t)0xFFFFFFFF);
            registry_.emplace<EntityInfo>(entity, info);

            if (ej.contains("name")) {
                registry_.emplace<Name>(entity, ej["name"].get<std::string>());
            }
        }

        snap_entities_to_terrain();

        toast_message_ = "Hot-reloaded " + std::to_string(j.size()) + " entities";
        toast_timer_ = 3.0f;
        std::cout << "Hot-reload: loaded " << j.size() << " entities" << std::endl;
    }
}

// ============================================================================
// Procedural Generation Bake
// ============================================================================

void EditorApplication::build_generation_ui() {
    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Generation", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    // --- Placement mode ---
    if (gen_placing_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 1.0f));
        if (ImGui::Button("Click terrain...", ImVec2(-1, 0))) {
            gen_placing_ = false;  // cancel
        }
        ImGui::PopStyleColor();
        ImGui::TextDisabled("ESC to cancel");
    } else {
        if (ImGui::Button("Place Center", ImVec2(-1, 0))) {
            gen_placing_ = true;
        }
    }

    if (gen_center_set_) {
        ImGui::Text("Center: %.0f, %.0f", gen_center_x_, gen_center_z_);
        ImGui::SameLine();
        if (ImGui::SmallButton("X##reset")) {
            gen_center_x_ = config_.world().width / 2.0f;
            gen_center_z_ = config_.world().height / 2.0f;
            gen_center_set_ = false;
        }
    } else {
        ImGui::TextDisabled("No center placed");
    }

    ImGui::Separator();

    // Disable generate buttons if no center
    bool can_generate = gen_center_set_;
    if (!can_generate) ImGui::BeginDisabled();

    // --- Town ---
    if (ImGui::CollapsingHeader("Town")) {
        ImGui::Checkbox("Buildings", &town_gen_.include_buildings);
        ImGui::SameLine();
        ImGui::Checkbox("Walls", &town_gen_.include_walls);
        ImGui::SameLine();
        ImGui::Checkbox("NPCs", &town_gen_.include_npcs);

        if (town_gen_.include_walls) {
            ImGui::DragFloat("Wall Dist", &town_gen_.wall_distance, 5.0f, 200.0f, 1000.0f, "%.0f");
            ImGui::DragFloat("Log Gap", &town_gen_.log_spacing, 1.0f, 15.0f, 80.0f, "%.0f");
            ImGui::DragFloat("Gate W", &town_gen_.gate_width, 5.0f, 0.0f, 300.0f, "%.0f");
        }

        if (ImGui::Button("Generate Town", ImVec2(-1, 0))) {
            last_generated_.clear();
            generate_town_entities();
            snap_entities_to_terrain();
            toast_message_ = "Town: " + std::to_string(last_generated_.size()) + " entities";
            toast_timer_ = 3.0f;
        }
    }

    // --- Environment ---
    if (ImGui::CollapsingHeader("Environment")) {
        ImGui::DragFloat("Radius", &env_gen_.radius, 50.0f, 500.0f, 5000.0f, "%.0f");
        ImGui::DragFloat("Exclusion", &env_gen_.min_distance, 10.0f, 0.0f, 1000.0f, "%.0f");

        ImGui::Separator();
        ImGui::DragInt("Rocks", &env_gen_.rock_count, 1, 0, 500);
        ImGui::DragFloat("Rock Min", &env_gen_.rock_min_scale, 1.0f, 5.0f, 200.0f, "%.0f");
        ImGui::DragFloat("Rock Max", &env_gen_.rock_max_scale, 1.0f, 5.0f, 200.0f, "%.0f");

        ImGui::Separator();
        ImGui::DragInt("Trees", &env_gen_.tree_count, 1, 0, 500);
        ImGui::DragFloat("Tree Min", &env_gen_.tree_min_scale, 5.0f, 50.0f, 1000.0f, "%.0f");
        ImGui::DragFloat("Tree Max", &env_gen_.tree_max_scale, 5.0f, 50.0f, 1000.0f, "%.0f");
        ImGui::DragFloat("Tree Gap", &env_gen_.tree_min_spacing, 5.0f, 50.0f, 500.0f, "%.0f");
        ImGui::DragInt("Groves", &env_gen_.grove_count, 1, 0, 12);
        ImGui::DragInt("Grove Sz", &env_gen_.grove_size, 1, 4, 30);
        ImGui::DragInt("Seed##env", &env_gen_.seed, 1);

        if (ImGui::Button("Generate Env", ImVec2(-1, 0))) {
            last_generated_.clear();
            generate_environment_entities();
            snap_entities_to_terrain();
            toast_message_ = "Env: " + std::to_string(last_generated_.size()) + " entities";
            toast_timer_ = 3.0f;
        }
    }

    // --- Monsters ---
    if (ImGui::CollapsingHeader("Monsters")) {
        ImGui::DragInt("Count", &monster_gen_.count, 1, 1, 500);
        ImGui::DragFloat("Safe Zone", &monster_gen_.safe_zone_radius, 10.0f, 100.0f, 2000.0f, "%.0f");
        ImGui::DragFloat("Max Radius", &monster_gen_.max_radius, 50.0f, 500.0f, 5000.0f, "%.0f");
        ImGui::DragInt("Seed##mon", &monster_gen_.seed, 1);

        if (ImGui::Button("Generate Monsters", ImVec2(-1, 0))) {
            last_generated_.clear();
            generate_monster_entities();
            snap_entities_to_terrain();
            toast_message_ = "Monsters: " + std::to_string(last_generated_.size()) + " entities";
            toast_timer_ = 3.0f;
        }
    }

    if (!can_generate) ImGui::EndDisabled();

    ImGui::Separator();

    // --- Batch operations ---
    if (!can_generate) ImGui::BeginDisabled();
    if (ImGui::Button("Generate All", ImVec2(-1, 0))) {
        registry_.clear();
        last_generated_.clear();
        generate_town_entities();
        generate_environment_entities();
        generate_monster_entities();
        snap_entities_to_terrain();
        toast_message_ = "World: " + std::to_string(last_generated_.size()) + " entities";
        toast_timer_ = 3.0f;
    }
    if (!can_generate) ImGui::EndDisabled();

    // Clear last generation pass
    if (last_generated_.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Undo Generate", ImVec2(-1, 0))) {
        int removed = 0;
        for (auto e : last_generated_) {
            if (registry_.valid(e)) {
                registry_.destroy(e);
                removed++;
            }
        }
        last_generated_.clear();
        toast_message_ = "Removed " + std::to_string(removed) + " entities";
        toast_timer_ = 3.0f;
    }
    if (last_generated_.empty()) ImGui::EndDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button("Clear All", ImVec2(-1, 0))) {
        ImGui::OpenPopup("ConfirmClear");
    }
    ImGui::PopStyleColor();

    if (ImGui::BeginPopupModal("ConfirmClear", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete all entities?");
        if (ImGui::Button("Yes", ImVec2(80, 0))) {
            registry_.clear();
            toast_message_ = "All entities cleared";
            toast_timer_ = 3.0f;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void EditorApplication::generate_town_entities() {
    using namespace mmo::server;
    using namespace mmo::server::config;

    const float town_cx = gen_center_x_;
    const float town_cz = gen_center_z_;
    int spawned = 0;

    // Buildings from config
    if (town_gen_.include_buildings) {
        for (const auto& b : config_.buildings()) {
            auto entity = registry_.create();
            last_generated_.push_back(entity);
            float wx = town_cx + b.x;
            float wz = town_cz + b.y;
            float wy = get_terrain_height(wx, wz);
            registry_.emplace<Transform>(entity, wx, wy, wz, glm::radians(b.rotation));

            EntityInfo info;
            info.type = EntityType::Building;
            info.model_name = b.model;
            info.target_size = b.target_size;
            info.color = 0xFFBB9977;
            registry_.emplace<EntityInfo>(entity, info);
            registry_.emplace<Name>(entity, b.name);
            spawned++;
        }
    }

    // Wall palisade
    if (town_gen_.include_walls) {
        const float wd = town_gen_.wall_distance;
        const float ls = town_gen_.log_spacing;
        const float gw = town_gen_.gate_width;

        auto place_log = [&](float ox, float oz, float rot_deg) {
            auto entity = registry_.create();
            last_generated_.push_back(entity);
            float wx = town_cx + ox;
            float wz = town_cz + oz;
            float wy = get_terrain_height(wx, wz);
            registry_.emplace<Transform>(entity, wx, wy, wz, glm::radians(rot_deg));

            EntityInfo info;
            info.type = EntityType::Building;
            info.model_name = "wooden_log";
            info.target_size = get_building_target_size(BuildingType::WoodenLog);
            info.color = 0xFFBB9977;
            registry_.emplace<EntityInfo>(entity, info);
            registry_.emplace<Name>(entity, "Log");
            spawned++;
        };

        // South wall (with gate)
        for (float x = -wd + 60.0f; x <= wd - 60.0f; x += ls) {
            if (std::abs(x) < gw / 2.0f) continue;
            place_log(x, -wd, 0.0f);
        }
        // North wall (with gate)
        for (float x = -wd + 60.0f; x <= wd - 60.0f; x += ls) {
            if (std::abs(x) < gw / 2.0f) continue;
            place_log(x, wd, 0.0f);
        }
        // West wall (solid)
        for (float z = -wd + 60.0f; z <= wd - 60.0f; z += ls) {
            place_log(-wd, z, 90.0f);
        }
        // East wall (with gate)
        for (float z = -wd + 60.0f; z <= wd - 60.0f; z += ls) {
            if (std::abs(z) < gw / 2.0f) continue;
            place_log(wd, z, 90.0f);
        }
    }

    // Town NPCs from config
    if (town_gen_.include_npcs) {
        for (const auto& n : config_.town_npcs()) {
            auto entity = registry_.create();
            last_generated_.push_back(entity);
            float wx = town_cx + n.x;
            float wz = town_cz + n.y;
            float wy = get_terrain_height(wx, wz);
            registry_.emplace<Transform>(entity, wx, wy, wz, 0.0f);

            EntityInfo info;
            info.type = EntityType::TownNPC;
            info.model_name = n.model;
            info.target_size = get_character_target_size(EntityType::TownNPC);
            info.color = n.color;
            registry_.emplace<EntityInfo>(entity, info);
            registry_.emplace<Name>(entity, n.name);
            spawned++;
        }
    }

    std::cout << "[Editor] Generated town: " << spawned << " entities\n";
}

void EditorApplication::generate_environment_entities() {
    using namespace mmo::server;
    using namespace mmo::server::config;

    std::mt19937 rng(static_cast<unsigned>(env_gen_.seed));
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> rotation_dist(0.0f, 360.0f);

    float cx = gen_center_x_;
    float cz = gen_center_z_;
    float outer = env_gen_.radius;
    float inner = env_gen_.min_distance;
    int rocks_spawned = 0;

    auto spawn_env = [&](EnvironmentType type, float x, float z, float scale, float rot_deg) {
        auto entity = registry_.create();
        last_generated_.push_back(entity);
        float wy = get_terrain_height(x, z);
        registry_.emplace<Transform>(entity, x, wy, z, glm::radians(rot_deg));

        EntityInfo info;
        info.type = EntityType::Environment;
        info.model_name = get_environment_model_name(type);
        info.target_size = scale;
        info.color = is_tree_type(type) ? 0xFF228822u : 0xFF666666u;
        registry_.emplace<EntityInfo>(entity, info);
        registry_.emplace<Name>(entity, info.model_name);
    };

    // Rocks - uniformly distributed between inner and outer radius
    for (int i = 0; i < env_gen_.rock_count; ++i) {
        float angle = angle_dist(rng);
        float dist = inner + (rng() / static_cast<float>(rng.max())) * (outer - inner);
        float x = cx + std::cos(angle) * dist;
        float z = cz + std::sin(angle) * dist;
        float scale = env_gen_.rock_min_scale +
                      (rng() / static_cast<float>(rng.max())) * (env_gen_.rock_max_scale - env_gen_.rock_min_scale);
        float rotation = rotation_dist(rng);
        auto rock_type = static_cast<EnvironmentType>(rng() % 5);
        spawn_env(rock_type, x, z, scale, rotation);
        rocks_spawned++;
    }

    // Trees - with spacing enforcement
    std::vector<std::pair<float, float>> tree_positions;
    auto is_too_close = [&](float x, float z, float min_dist) {
        float min_dist_sq = min_dist * min_dist;
        for (const auto& pos : tree_positions) {
            float dx = x - pos.first;
            float dz = z - pos.second;
            if (dx * dx + dz * dz < min_dist_sq) return true;
        }
        return false;
    };

    // Scattered trees
    int scatter_count = env_gen_.tree_count;
    for (int i = 0; i < scatter_count; ++i) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            float angle = angle_dist(rng);
            float dist = inner + (rng() / static_cast<float>(rng.max())) * (outer - inner);
            float x = cx + std::cos(angle) * dist;
            float z = cz + std::sin(angle) * dist;

            if (!is_too_close(x, z, env_gen_.tree_min_spacing)) {
                float scale = env_gen_.tree_min_scale +
                              (rng() / static_cast<float>(rng.max())) * (env_gen_.tree_max_scale - env_gen_.tree_min_scale);
                float rotation = rotation_dist(rng);
                auto tree_type = static_cast<EnvironmentType>(
                    static_cast<int>(EnvironmentType::TreeOak) + (rng() % 2));
                spawn_env(tree_type, x, z, scale, rotation);
                tree_positions.push_back({x, z});
                break;
            }
        }
    }

    // Clustered groves
    for (int grove = 0; grove < env_gen_.grove_count; ++grove) {
        float grove_angle = grove * (2.0f * 3.14159f / std::max(1, env_gen_.grove_count)) +
                           (rng() / static_cast<float>(rng.max())) * 0.5f;
        float grove_dist = inner + (rng() / static_cast<float>(rng.max())) * (outer - inner) * 0.6f;
        float grove_x = cx + std::cos(grove_angle) * grove_dist;
        float grove_z = cz + std::sin(grove_angle) * grove_dist;

        int grove_tree_type = rng() % 2;

        for (int i = 0; i < env_gen_.grove_size; ++i) {
            for (int attempt = 0; attempt < 10; ++attempt) {
                float offset_angle = angle_dist(rng);
                float offset_dist = 50.0f + (rng() / static_cast<float>(rng.max())) * 150.0f;
                float x = grove_x + std::cos(offset_angle) * offset_dist;
                float z = grove_z + std::sin(offset_angle) * offset_dist;
                if (!is_too_close(x, z, env_gen_.tree_min_spacing)) {
                    float scale = env_gen_.tree_min_scale +
                                  (rng() / static_cast<float>(rng.max())) * (env_gen_.tree_max_scale - env_gen_.tree_min_scale) * 0.7f;
                    float rotation = rotation_dist(rng);
                    int final_type = (rng() % 10 < 7) ? grove_tree_type : (1 - grove_tree_type);
                    auto tree_type = static_cast<EnvironmentType>(
                        static_cast<int>(EnvironmentType::TreeOak) + final_type);
                    spawn_env(tree_type, x, z, scale, rotation);
                    tree_positions.push_back({x, z});
                    break;
                }
            }
        }
    }

    std::cout << "[Editor] Generated environment: " << rocks_spawned << " rocks + "
              << tree_positions.size() << " trees\n";
}

void EditorApplication::generate_monster_entities() {
    using namespace mmo::server;
    using namespace mmo::server::config;

    const float cx = gen_center_x_;
    const float cz = gen_center_z_;
    const float safe_r = monster_gen_.safe_zone_radius;
    const float max_r = monster_gen_.max_radius;

    unsigned seed = monster_gen_.seed != 0
        ? static_cast<unsigned>(monster_gen_.seed)
        : std::random_device{}();
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * 3.14159f);

    int spawned = 0;
    int max_attempts = monster_gen_.count * 20;
    for (int attempt = 0; attempt < max_attempts && spawned < monster_gen_.count; ++attempt) {
        float angle = angle_dist(rng);
        float dist = safe_r + (rng() / static_cast<float>(rng.max())) * (max_r - safe_r);
        float x = cx + std::cos(angle) * dist;
        float z = cz + std::sin(angle) * dist;

        // Clamp to world bounds
        if (x < 100.0f || x > config_.world().width - 100.0f ||
            z < 100.0f || z > config_.world().height - 100.0f) {
            continue;
        }

        auto entity = registry_.create();
        last_generated_.push_back(entity);
        float wy = get_terrain_height(x, z);
        registry_.emplace<Transform>(entity, x, wy, z, 0.0f);

        EntityInfo info;
        info.type = EntityType::NPC;
        info.model_name = "npc_enemy";
        info.target_size = get_character_target_size(EntityType::NPC);
        info.color = config_.monster().color;
        registry_.emplace<EntityInfo>(entity, info);
        registry_.emplace<Name>(entity, "Monster_" + std::to_string(spawned + 1));

        spawned++;
    }

    std::cout << "[Editor] Generated " << spawned << " monsters\n";
}

// ============================================================================
// Model Loading
// ============================================================================

bool EditorApplication::load_models(const std::string& assets_path) {
    auto& mdl = models();
    std::string models_path = assets_path + "/models/";

    // Load model manifest
    std::ifstream manifest_file("data/models.json");
    if (!manifest_file.is_open()) {
        std::cerr << "Failed to open data/models.json" << std::endl;
        return false;
    }

    nlohmann::json manifest;
    try {
        manifest_file >> manifest;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse data/models.json: " << e.what() << std::endl;
        return false;
    }

    int loaded = 0, failed = 0;
    for (auto& entry : manifest["models"]) {
        std::string id = entry["id"];
        std::string file = entry["file"];
        std::string fallback = entry.value("fallback", "");

        bool ok = mdl.load_model(id, models_path + file);
        if (!ok && !fallback.empty()) {
            ok = mdl.load_model(id, models_path + fallback);
        }

        if (ok) {
            loaded++;
        } else {
            failed++;
            std::cerr << "Warning: Failed to load model '" << id << "'" << std::endl;
        }
    }

    std::cout << "Models: " << loaded << " loaded, " << failed << " failed" << std::endl;
    return failed == 0;
}

} // namespace mmo::editor
